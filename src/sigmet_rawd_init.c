/*
 -	sigmet_rawd.c --
 -		This file defines data structures and functions for a
 -		daemon that reads, manipulates data from, and provides
 -		access to Sigmet raw volumes. See sigmet_rawd (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.239 $ $Date: 2010/08/09 22:23:59 $
 */

#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include "alloc.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "bisearch_lib.h"
#include "data_types.h"
#include "xdrx.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/* If true, daemon has received a "stop" command */
static int stop = 0;

/* Process streams and files */

/* Where to put output and error messages */
#define SIGMET_RAWD_LOG "sigmet.log"
#define SIGMET_RAWD_ERR "sigmet.err"

/* Size for various strings */
#define LEN 4096

/* If true, use degrees instead of radians */
static int use_deg = 0;

/* Sigmet volumes */
struct sig_vol {
    int oqpd;				/* True => struct contains a volume */
    struct Sigmet_Vol vol;		/* Sigmet volume */
    char vol_nm[LEN];			/* file that provided the volume */
    dev_t st_dev;			/* Device that provided vol */
    ino_t st_ino;			/* I-number of file that provided vol */
    int users;				/* Number of client sessions using vol */
};

#define N_VOLS 256
static struct sig_vol vols[N_VOLS];	/* Available volumes */

/* Find member of vols given a file * name */
static int hash(char *, struct stat *);
static pid_t vol_pid = -1;		/* Process providing a raw volume */
static int get_vol_i(char *);
static int new_vol_i(char *, struct stat *, FILE *);

/* Subcommands */
#define NCMD 27
typedef int (callback)(int , char **, char *, int, FILE *, int, FILE *);
static callback pid_cb;
static callback types_cb;
static callback setcolors_cb;
static callback good_cb;
static callback hread_cb;
static callback read_cb;
static callback list_cb;
static callback release_cb;
static callback unload_cb;
static callback flush_cb;
static callback volume_headers_cb;
static callback vol_hdr_cb;
static callback near_sweep_cb;
static callback ray_headers_cb;
static callback data_cb;
static callback bin_outline_cb;
static callback bintvls_cb;
static callback radar_lon_cb;
static callback radar_lat_cb;
static callback shift_az_cb;
static callback proj_cb;
static callback img_app_cb;
static callback img_sz_cb;
static callback alpha_cb;
static callback img_name_cb;
static callback img_cb;
static callback stop_cb;
static char *cmd1v[NCMD] = {
    "pid", "types", "colors", "good", "hread",
    "read", "list", "release", "unload", "flush", "volume_headers",
    "vol_hdr", "near_sweep", "ray_headers", "data", "bin_outline",
    "bintvls", "radar_lon", "radar_lat", "shift_az",
    "proj", "img_app", "img_sz", "alpha", "img_name", "img", "stop"
};
static callback *cb1v[NCMD] = {
    pid_cb, types_cb, setcolors_cb, good_cb, hread_cb,
    read_cb, list_cb, release_cb, unload_cb, flush_cb, volume_headers_cb,
    vol_hdr_cb, near_sweep_cb, ray_headers_cb, data_cb, bin_outline_cb,
    bintvls_cb, radar_lon_cb, radar_lat_cb, shift_az_cb,
    proj_cb, img_app_cb, img_sz_cb, alpha_cb, img_name_cb, img_cb, stop_cb
};

/* Cartographic projection */
#ifdef PROJ4
#include <proj_api.h>
static projPJ pj;
#endif

/* Configuration for output images */
static int w_pxl;			/* Width of image in display units,
					   pixels, points, cm */
static int h_pxl;			/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char img_app[LEN];		/* External application to draw sweeps */

/* Convenience functions */
static char *time_stamp(void);
static int abs_name(char *, char *, char *, size_t);
static FILE *vol_open(const char *, int, FILE *);
static int flush(int);
static void unload(int);
static int img_name(struct Sigmet_Vol *, char *, int, char *, FILE *);

/* Signal handling functions */
static int handle_signals(void);
static void handler(int);

/* Abbreviations */
#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

int main(int argc, char *argv[])
{
    char *cmd;			/* Name of this daemon */
    mode_t mode;		/* Mode for files */
    char *ddir;			/* Working directory for daemon */
    int bg = 1;			/* If true, run in foreground (do not fork) */
    pid_t pid;			/* Return from fork */
    int flags;			/* Flags for log files */
    struct sockaddr_un sa;	/* Socket to read command and return exit status */
    struct sockaddr *sa_p;	/* &sa or &d_err_sa, for call to bind */
    int i_dmn;			/* File descriptors for d_io and d_err */
    struct sig_vol *sv_p;	/* Member of vols */
    char *ang_u;		/* Angle unit */
    int y;			/* Loop index */
    char *dflt_proj[] = { "+proj=aeqd", "+ellps=sphere" }; /* Map projection */
    int i;
    int cl_io_fd;		/* File descriptor to read client command
				   and send results */
    pid_t client_pid = -1;	/* Client process id */
    char *cl_wd = NULL;		/* Client working directory */
    size_t cl_wd_l;		/* strlen(cl_wd) */
    size_t cl_wd_lx = 0;	/* Allocation at cl_wd */
    char *cmd_ln = NULL;	/* Command line from client */
    size_t cmd_ln_l;		/* strlen(cmd_ln) */
    size_t cmd_ln_lx = 0;	/* Given size of command line */

    cmd = argv[0];

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s: could not set up signal management.", cmd);
	exit(EXIT_FAILURE);
    }

    /* Usage: sigmet_rawd [-f] */
    if ( argc == 1 ) {
	printf("%s --\nVersion %s. Copyright (c) 2010 Gordon D. Carrie. "
		"All rights reserved.\n", cmd, SIGMET_VSN);
    } else if ( argc == 2 && strcmp(argv[1], "-f") == 0 ) {
	bg = 0;
    } else {
	fprintf(stderr, "Usage: %s [-f]\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Identify and go to working directory */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s: could not identify daemon directory. "
		"Please specify daemon directory with SIGMET_RAWD_DIR "
		"environment variable.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( chdir(ddir) == -1 ) {
	perror("Could not set working directory.");
	exit(EXIT_FAILURE);
    }
    if ( access(SIGMET_RAWD_IN, F_OK) == 0 ) {
	fprintf(stderr, "%s: daemon socket %s already exists. "
		"Is daemon already running?\n", cmd, SIGMET_RAWD_IN);
	exit(EXIT_FAILURE);
    }

    /* Initialize vols array */
    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	sv_p->oqpd = 0;
	Sigmet_InitVol(&sv_p->vol);
	sv_p->users = 0;
	sv_p->st_dev = 0;
	sv_p->st_ino = 0;
    }

    /* Initialize other variables */
    w_pxl = h_pxl = 600;
    if ( !(pj = pj_init(2, dflt_proj)) ) {
	fprintf(stderr, "%s: could not set default projection.\n", cmd);
	exit(EXIT_FAILURE);
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	if ( !DataType_Add(Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y)) ) {
	    fprintf(stderr, "%s: could not register data type %s\n",
		    cmd, Sigmet_DataType_Abbrv(y));
	    exit(EXIT_FAILURE);
	}
    }
    *img_app = '\0';

    /* Check for angle unit */
    if ((ang_u = getenv("ANGLE_UNIT")) != NULL) {
	if (strcmp(ang_u, "DEGREE") == 0) {
	    use_deg = 1;
	} else if (strcmp(ang_u, "RADIAN") == 0) {
	    use_deg = 0;
	} else {
	    fprintf(stderr, "%s: unknown angle unit %s.\n", cmd, ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    if ( bg ) {
	/* Put daemon in background */
	switch (pid = fork()) {
	    case -1:
		fprintf(stderr, "%s: could not fork.\n%s\n", cmd, strerror(errno));
		exit(EXIT_FAILURE);
	    case 0:
		/* Child = daemon process. */

		if ( !handle_signals() ) {
		    fprintf(stderr, "%s: could not set up signal management "
			    "in daemon.", cmd);
		    exit(EXIT_FAILURE);
		}

		/* Set up output. */
		flags = O_CREAT | O_TRUNC | O_WRONLY;
		mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
		if ( (i = open(SIGMET_RAWD_LOG, flags, mode)) == -1
			|| dup2(i, STDOUT_FILENO) == -1 || close(i) == -1 ) {
		    fprintf(stderr, "%s: could not open log file.", cmd);
		    _exit(EXIT_FAILURE);
		}
		if ( (i = open(SIGMET_RAWD_ERR, flags, mode)) == -1
			|| dup2(i, STDERR_FILENO) == -1 || close(i) == -1 ) {
		    fprintf(stderr, "%s: could not open error file.", cmd);
		    _exit(EXIT_FAILURE);
		}
		fclose(stdin);
		break;
	    default:
		/* Parent. Print information and exit. */
		printf("Starting sigmet_rawd. Process id = %d.\n"
			"Daemon working directory = %s\n", pid, ddir);
		exit(EXIT_SUCCESS);
	}
    }

    /* Create socket to communicate with clients */
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s", SIGMET_RAWD_IN) > SA_PLEN ) {
	fprintf(stderr, "%s (%d): could not fit %s into socket address path.\n",
		cmd, getpid(), SIGMET_RAWD_IN);
	exit(EXIT_FAILURE);
    }
    sa_p = (struct sockaddr *)&sa;
    if ((i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1
	    || bind(i_dmn, sa_p, SA_UN_SZ) == -1
	    || listen(i_dmn, SOMAXCONN) == -1) {
	fprintf(stderr, "%s: could not create io socket.\n%s\n",
		time_stamp(), strerror(errno));
	exit(EXIT_FAILURE);
    }

    printf("Daemon starting. Process id = %d\n", getpid());

    fflush(stdout);

    /* Wait for clients */
    while ( !stop && (cl_io_fd = accept(i_dmn, NULL, 0)) != -1 ) {
	int argc1;		/* Number of arguments in received command line */
	char *argv1[SIGMET_RAWD_ARGCX];
				/* Arguments from client command line */
	int a;			/* Index into argv1 */
	char *cmd0;		/* Name of client */
	char *cmd1;		/* Subcommand */
	char out_nm[LEN];	/* Fifo to send standard output to client */
	char err_nm[LEN];	/* Fifo to send error output to client */
	int i_out = -1;		/* Standard output to client */
	int i_err = -1;		/* Error output to client  */
	FILE *out, *err;	/* Standard streams for i_out and i_err */
	int flags;		/* Return from fcntl, when config'ing cl_io_fd */
	int status;		/* Result of callback, exit status for client */
	int i;			/* Loop index */
	char *c, *e;		/* Loop parameters */
	void *t;		/* Hold return from realloc */

	/* Close command stream if daemon spawns a child */
	if ( (flags = fcntl(cl_io_fd, F_GETFD)) == -1
		|| fcntl(cl_io_fd, F_SETFD, flags | FD_CLOEXEC) == -1 ) {
	    fprintf(stderr, "%s: could not set flags on connection to client.\n"
		    "%s\n", time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/* Read client process id */
	if ( read(cl_io_fd, &client_pid, sizeof(pid_t)) == -1 ) {
	    fprintf(stderr, "%s: failed to read client process id.\n%s\n",
		    time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/* Read client working directory */
	if ( read(cl_io_fd, &cl_wd_l, sizeof(size_t)) == -1 ) {
	    fprintf(stderr, "%s: failed to read client working directory for "
		    "process %d.\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( cl_wd_l + 1 > cl_wd_lx ) {
	    if ( !(t = REALLOC(cl_wd, cl_wd_l + 1)) ) {
		fprintf(stderr, "%s: allocation failed for working directory of "
			"%ld bytes for process %d.\n",
			time_stamp(), cl_wd_l, client_pid);
		close(cl_io_fd);
		continue;
	    }
	    cl_wd = t;
	    cl_wd_lx = cl_wd_l;
	}
	memset(cl_wd, 0, cl_wd_lx);
	if ( read(cl_io_fd, cl_wd, cl_wd_l) == -1 ) {
	    fprintf(stderr, "%s: failed to read client working directory for "
		    "process %d.\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( *cl_wd != '/' ) {
	    fprintf(stderr, "%s: client working directory must be absolute, "
		    "not %s, for process %d.\n%s\n",
		    time_stamp(), cl_wd, client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/* Read argument count */
	if ( read(cl_io_fd, &argc1, sizeof(int)) == -1 ) {
	    fprintf(stderr, "%s: failed to read length of command line "
		    "for process %d.\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( argc1 > SIGMET_RAWD_ARGCX ) {
	    fprintf(stderr, "%s: cannot parse %d command line arguments for "
		    "process %d. Maximum is %d.\n",
		    time_stamp(), argc1, client_pid, SIGMET_RAWD_ARGCX);
	    close(cl_io_fd);
	    continue;
	}

	/* Read command line */
	if ( read(cl_io_fd, &cmd_ln_l, sizeof(size_t)) == -1 ) {
	    fprintf(stderr, "%s: failed to read length of command line "
		    "for process %d.\n", time_stamp(), client_pid);
	    close(cl_io_fd);
	    continue;
	}
	if ( cmd_ln_l > cmd_ln_lx ) {
	    if ( !(t = REALLOC(cmd_ln, cmd_ln_l)) ) {
		fprintf(stderr, "%s: allocation failed for command line of "
			"%ld bytes for process %d.\n",
			time_stamp(), cmd_ln_l, client_pid);
		close(cl_io_fd);
		continue;
	    }
	    cmd_ln = t;
	    cmd_ln_lx = cmd_ln_l;
	}
	memset(cmd_ln, 0, cmd_ln_lx);
	if ( read(cl_io_fd, cmd_ln, cmd_ln_l) == -1 ) {
	    fprintf(stderr, "%s: failed to read command line for "
		    "process %d.%s\n", time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/* Break command line into arguments */
	for (a = 0, argv1[a] = c = cmd_ln, e = c + cmd_ln_l;
		c < e && a < argc1;
		c++) {
	    if ( *c == '\0' ) {
		argv1[++a] = c + 1;
	    }
	}
	if ( a > argc1 ) {
	    fprintf(stderr, "%s: command line garbled for process %d.\n",
		    time_stamp(), client_pid);
	    continue;
	}

	/* Open fifos to client */
	if ( snprintf(out_nm, LEN, "%d.1", client_pid) > LEN ) {
	    fprintf(stderr, "%s: could not create name for result pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( (i_out = open(out_nm, O_WRONLY)) == -1
		|| !(out = fdopen(i_out, "w"))) {
	    fprintf(stderr, "%s: could not open pipe for standard output for "
		    "process %d\n%s\n", time_stamp(), client_pid, strerror(errno));
	    continue;
	}
	if ( snprintf(err_nm, LEN, "%d.2", client_pid) > LEN ) {
	    fprintf(stderr, "%s: could not create name for error pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( (i_err = open(err_nm, O_WRONLY)) == -1
		|| !(err = fdopen(i_err, "w"))) {
	    fprintf(stderr, "%s: could not open pipe for error messages for "
		    "process %d\n%s\n", time_stamp(), client_pid, strerror(errno));
	    if ( fclose(out) == EOF ) {
		fprintf(stderr, "%s: could not close client error streams "
			"for process %d\n%s\n",
			time_stamp(), client_pid, strerror(errno));
	    }
	    continue;
	}

	/* Identify command */
	cmd0 = argv1[0];
	cmd1 = argv1[1];
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    /* No command. Make error message. */
	    status = EXIT_FAILURE;
	    fprintf(err, "No option or subcommand named %s. "
		    "Subcommand must be one of: ", cmd1);
	    for (i = 0; i < NCMD; i++) {
		fprintf(err, "%s ", cmd1v[i]);
	    }
	    fprintf(err, "\n");
	} else {
	    /* Found command. Run it. */
	    status = (cb1v[i])(argc1, argv1, cl_wd, i_out, out, i_err, err)
		? EXIT_SUCCESS : EXIT_FAILURE;
	    if ( status == EXIT_FAILURE ) {
		fprintf(err, "%s\n", Err_Get());
	    }
	}

	/* Send result and clean up */
	if ( fclose(out) == EOF || fclose(err) == EOF ) {
	    fprintf(stderr, "%s: could not close client error streams "
		    "for process %d\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	}
	if ( write(cl_io_fd, &status, sizeof(int)) == -1 ) {
	    fprintf(err, "%s: could not send return code for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
	if ( close(cl_io_fd) == -1 ) {
	    fprintf(err, "%s: could not close socket for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}

    }

    if ( !stop ) {
	fprintf(stderr, "%s: unexpected exit.\n%s\n",
		time_stamp(), strerror(errno));
    }
    unlink(SIGMET_RAWD_IN);
    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	Sigmet_FreeVol(&sv_p->vol);
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	DataType_Rm(Sigmet_DataType_Abbrv(y));
    }

    return 0;
}

/* Get a character string with the current time */
static char *time_stamp(void)
{
    static char ts[LEN];
    time_t now;

    now = time(NULL);
    if (strftime(ts, LEN, "%Y/%m/%d %H:%M:%S %Z", localtime(&now))) {
	return ts;
    } else {
	return "";
    }
}

/*
   Make rel absolute. If it is relative, append it to root, which must be absolute.
   Store resulting name in abs, which should have space for l characters.
   Return 1 on success. If failure, print an error message to err and return 0.
 */
static int abs_name(char *root, char *rel, char *abs, size_t l)
{
    int status;

    if ( *rel != '/' ) {
	if ( *root != '/' ) {
	    Err_Append("Root path must be absolute, not ");
	    Err_Append(root);
	    Err_Append(". ");
	    return 0;
	}
	status = snprintf(abs, l, "%s/%s", root, rel);
    } else {
	status = snprintf(abs, l, "%s", rel);
    }
    if ( status > l ) {
	Err_Append("File name too long.\n");
	return 0;
    } else if ( status == -1 ) {
	Err_Append(strerror(errno));
	return 0;
    }
    return 1;
}

static int pid_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if (argc != 2) {
	fprintf(err, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    fprintf(out, "%d\n", getpid());
    return 1;
}

static int types_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;

    if (argc != 2) {
	fprintf(err, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	fprintf(out, "%s | %s\n",
		Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return 1;
}

static int setcolors_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;		/* Data type abbreviation */
    char *clr_fl_nm;		/* File with colors */
    int status;

    /* Parse command line */
    if (argc != 4) {
	fprintf(err, "Usage: %s %s type colors_file\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    clr_fl_nm = argv[3];
    status = DataType_SetColors(abbrv, clr_fl_nm);
    if ( !status ) {
	fprintf(err, "%s\n", Err_Get());
    }
    return status;
}

/*
   Open volume file vol_nm, possibly via a pipe.
   Return file handle, or NULL if failure.
   If file is actually a process, global variable vol_pid gets the
   child's process id.
 */
static FILE *vol_open(const char *vol_nm, int i_err, FILE *err)
{
    FILE *in;		/* Return value */
    char *sfx;		/* Filename suffix */
    int pfd[2];		/* Pipe for data */

    pfd[0] = pfd[1] = -1;
    in = NULL;

    sfx = strrchr(vol_nm , '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/* If filename ends with ".gz", read from gunzip pipe */
	if ( pipe(pfd) == -1 ) {
	    fprintf(err, "Could not create pipe for gzip\n%s\n", strerror(errno));
	    goto error;
	}
	vol_pid = fork();
	switch (vol_pid) {
	    case -1:
		fprintf(err, "Could not spawn gzip\n");
		goto error;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(err, "%s: gzip process failed\n%s\n",
			    time_stamp(), strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    fprintf(err, "%s: gzip process could not access error "
			    "stream\n%s\n", time_stamp(), strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(err, "Could not read gzip process\n%s\n",
			    strerror(errno));
		    vol_pid = -1;
		    goto error;
		} else {
		    return in;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/* If filename ends with ".bz2", read from bunzip2 pipe */
	if ( pipe(pfd) == -1 ) {
	    fprintf(err, "Could not create pipe for bzip2\n%s\n", strerror(errno));
	    goto error;
	}
	vol_pid = fork();
	switch (vol_pid) {
	    case -1:
		fprintf(err, "Could not spawn bzip2\n");
		goto error;
	    case 0:
		/* Child process - bzip2.  Send child stdout to pipe. */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(err, "%s: could not set up bzip2 process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    fprintf(err, "%s: bzip2 process could not access error "
			    "stream\n%s\n", time_stamp(), strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from bzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(err, "Could not read bzip2 process\n%s\n",
			    strerror(errno));
		    vol_pid = -1;
		    goto error;
		} else {
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	/* Uncompressed file */
	fprintf(err, "Could not open %s\n%s\n", vol_nm, strerror(errno));
	return NULL;
    }
    return in;

error:
	if ( vol_pid != -1 ) {
	    kill(vol_pid, SIGTERM);
	    vol_pid = -1;
	}
	if ( in ) {
	    fclose(in);
	    pfd[0] = -1;
	}
	if ( pfd[0] != -1 ) {
	    close(pfd[0]);
	}
	if ( pfd[1] != -1 ) {
	    close(pfd[1]);
	}
	return NULL;
}

static int good_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    FILE *in;
    int rslt;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Could not assess %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ( !(in = vol_open(vol_nm, i_err, err)) ) {
	fprintf(err, "%s %s could not open %s\n", argv0, argv1, vol_nm);
	return 0;
    }
    rslt = Sigmet_GoodVol(in);
    fclose(in);
    if ( vol_pid != -1 ) {
	waitpid(vol_pid, NULL, 0);
	vol_pid = -1;
    }
    return rslt;
}

static int hread_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int loaded;			/* If true, volume is loaded */
    int trying;			/* If true, still attempting to read volume */
    int i;			/* Index into vols */
    struct stat sbuf;   	/* Information about file to read */
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    FILE *in;			/* Stream from Sigmet raw file */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name: %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }

    /* If volume is already loaded, increment user count return. */
    if ( (i = get_vol_i(vol_nm)) >= 0 ) {
	vols[i].users++;
	fprintf(out, "%s loaded%s.\n",
		vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
	return 1;
    }

    /* Volume is not loaded. Try to find a slot in which to load it. */
    if ( (i = new_vol_i(vol_nm, &sbuf, err)) == -1 ) {
	fprintf(err, "%s %s: could not find a slot while attempting "
		"to (re)load %s\n%s\n", argv0, argv1, vol_nm, Err_Get());
	return 0;
    }

    /* Read headers */
    for (trying = 1, loaded = 0; trying && !loaded; ) {
	vol_pid = -1;
	if ( !(in = vol_open(vol_nm, i_err, err)) ) {
	    fprintf(err, "%s %s: could not open %s for input.\n",
		    argv0, argv1, vol_nm);
	    return 0;
	}
	switch (Sigmet_ReadHdr(in, &vols[i].vol)) {
	    case READ_OK:
		/* Success. Break out. */
		loaded = 1;
		trying = 0;
		break;
	    case MEM_FAIL:
		/* Try to free some memory and try again */
		if ( !flush(1) ) {
		    trying = 0;
		}
		break;
	    case INPUT_FAIL:
	    case BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		unload(i);
		trying = 0;
		break;
	}
	while (fgetc(in) != EOF) {
	    continue;
	}
	fclose(in);
	if (vol_pid != -1) {
	    if ( waitpid(vol_pid, NULL, 0) == -1 ) {
		fprintf(err, "%s: could not clean up afer input process for "
			"%s. %s. Continuing anyway.\n",
			time_stamp(), vol_nm, strerror(errno));
	    }
	    vol_pid = -1;
	}
    }
    if ( !loaded ) {
	fprintf(err, "%s %s: could not read headers from %s\n",
		argv0, argv1, vol_nm);
	return 0;
    }
    vols[i].oqpd = 1;
    stat(vol_nm, &sbuf);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].users++;
    fprintf(out, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int read_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int loaded;			/* If true, volume is loaded */
    int trying;			/* If true, still attempting to read volume */
    struct stat sbuf;   	/* Information about file to read */
    int i;			/* Index into vols */
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    FILE *in;			/* Stream from Sigmet raw file */

    if ( argc != 3 ) {
	fprintf(err, "%s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }

    /*
       If volume is not loaded, seek an unused slot in vols and attempt load.
       If volume loaded but truncated, free it and attempt reload.
       If volume is loaded and complete, increment user count return.
     */

    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	if ( (i = new_vol_i(vol_nm, &sbuf, err)) == -1 ) {
	    fprintf(err, "%s %s: could not find a slot while attempting "
		    "to (re)load %s\n%s\n", argv0, argv1, vol_nm, Err_Get());
	    return 0;
	}
    } else if ( i >= 0 && vols[i].vol.truncated ) {
	unload(i);
    } else {
	vols[i].users++;
	fprintf(out, "%s loaded.\n", vol_nm);
	return 1;
    }

    /* Volume was not loaded, or was freed because truncated. Read volume */
    for (trying = 1, loaded = 0; trying && !loaded; ) {
	vol_pid = -1;
	if ( !(in = vol_open(vol_nm, i_err, err)) ) {
	    fprintf(err, "%s %s: could not open %s for input.\n",
		    argv0, argv1, vol_nm);
	    return 0;
	}
	switch (Sigmet_ReadVol(in, &vols[i].vol)) {
	    case READ_OK:
		/* Success. Break out. */
		loaded = 1;
		trying = 0;
		break;
	    case MEM_FAIL:
		/* Try to free some memory. If unable to free memory, fail. */
		if ( !flush(1) ) {
		    trying = 0;
		}
		break;
	    case INPUT_FAIL:
	    case BAD_VOL:
		/* Read failed. Disable this slot and fail. */
		unload(i);
		trying = 0;
		break;
	}
	while (fgetc(in) != EOF) {
	    continue;
	}
	fclose(in);
	if (vol_pid != -1) {
	    if ( waitpid(vol_pid, NULL, 0) == -1 ) {
		fprintf(err, "%s: could not clean up afer input process for "
			"%s. %s. Continuing anyway.\n",
			time_stamp(), vol_nm, strerror(errno));
	    }
	    vol_pid = -1;
	}
    }
    if ( !loaded ) {
	fprintf(err, "%s %s: could not get valid volume from %s\n",
		argv0, argv1, vol_nm);
	return 0;
    }
    vols[i].oqpd = 1;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    vols[i].users++;
    fprintf(out, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int list_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    int i;

    for (i = 0; i < N_VOLS; i++) {
	if ( vols[i].oqpd != 0 ) {
	    fprintf(out, "%s users=%d %s\n",
		    vols[i].vol_nm, vols[i].users,
		    vols[i].vol.truncated ? " truncated" : "complete");
	}
    }
    return 1;
}

/*
   Create a hash index for a volume file name, or -1 if vol_nm is not a file.
   As a side effect, stat information for the file is copied to sbuf_p.
 */
static int hash(char *vol_nm, struct stat *sbuf_p)
{
    struct stat sbuf;		/* Information about file */

    assert(N_VOLS == 256);
    if ( stat(vol_nm, &sbuf) == -1 ) {
	return -1;
    }
    sbuf_p->st_dev = sbuf.st_dev;
    sbuf_p->st_ino = sbuf.st_ino;

    /*
       Hash index is the last four bits from the device identifier next to the
       last four bits from the inumber
     */
    return ((sbuf.st_dev & 0x0f) << 4) | (sbuf.st_ino & 0x0f);
}

/*
   This function returns the index from vols of the volume obtained
   from Sigmet raw product file vol_nm, or -1 is the volume is not
   loaded. 
 */
static int get_vol_i(char *vol_nm)
{
    struct stat sbuf;		/* Information about file to read */
    int h, i;			/* Index into vols */

    if ( (h = hash(vol_nm, &sbuf)) == -1 ) {
	return -1;
    }

    /*
       Hash is not necessarily the index for the volume in vols array.
       Walk the array until we actually reach the volume from vol_nm.
     */

    for (i = h; i + 1 != h; i = (i + 1) % N_VOLS) {
	if ( vols[i].oqpd
		&& vols[i].st_dev == sbuf.st_dev
		&& vols[i].st_ino == sbuf.st_ino) {
	    return i;
	}
    }
    return -1;
}

/*
   This function obtains the index at which a volume from vol_nm should
   be stored.  IT ASSUMES THE VOLUME HAS NOT ALREADY BEEN LOADED. Index
   returned will be the first empty slot after the index returned
   by hash. Any unused volumes found while searching will be purged.
   As a side effect, stat information for the volume is copied to sbuf_p.
   Functions that read volumes need it to create an identify for the volume.
   If there are no available slots, this function returns -1.
 */
static int new_vol_i(char *vol_nm, struct stat *sbuf_p, FILE *err)
{
    int h, i;			/* Index into vols */

    if ( (h = hash(vol_nm, sbuf_p)) == -1 ) {
	fprintf(err, "Could not get information about file %s\n%s\n",
		vol_nm, strerror(errno));
	return -1;
    }

    /* Search vols array for an empty slot */
    for (i = h; i + 1 != h; i = (i + 1) % N_VOLS) {
	if ( !vols[i].oqpd ) {
	    return i;
	} else if ( vols[i].users <= 0 ) {
	    unload(i);
	    return i;
	}
    }
    fprintf(err, "Could not find a slot for a new volume while attempting "
	    "to load %s\n", vol_nm);
    return -1;
}

static int release_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i >= 0 && vols[i].users > 0 ) {
	vols[i].users--;
    }
    return 1;
}

/*
   Remove a volume and its entry. Noisily return error if volume in use. Quietly
   do nothing if volume does not exist.
 */
static int unload_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i >= 0 ) {
	if ( vols[i].users <= 0 ) {
	    unload(i);
	} else {
	    fprintf(err, "%s %s: %s in use\n", argv0, argv1, vol_nm);
	    return 0;
	}
    }
    return 1;
}

/* Unload a volume */
static void unload(int i)
{
    if ( i < 0 || i >= N_VOLS ) {
	return;
    }
    Sigmet_FreeVol(&vols[i].vol);
    vols[i].oqpd = 0;
    vols[i].users = 0;
    vols[i].st_dev = 0;
    vols[i].st_ino = 0;
}

/* Remove c unused volumes, if possible. */
static int flush(int c)
{
    int i;

    for (i = 0; i < N_VOLS && c > 0; i++) {
	if ( vols[i].oqpd && vols[i].users <= 0 ) {
	    unload(i);
	    if ( --c == 0 ) {
		break;
	    }
	}
    }
    if ( c > 0 ) {
	return 0;
    }
    return 1;
}

/* This command removes some unused volumes, if possible. */
static int flush_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *c_s;
    int c;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s count\n", argv0, argv1);
	return 0;
    }
    c_s = argv[2];
    if (sscanf(c_s, "%d", &c) != 1) {
	fprintf(err, "%s %s: expected an integer for count, got %s\n",
		argv0, argv1, c_s);
	return 0;
    }
    if ( !flush(c) ) {
	fprintf(err, "%s %s: could not flush %d volumes\n", argv0, argv1, c);
	return 0;
    } else {
	return 1;
    }
}

static int volume_headers_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    Sigmet_PrintHdr(out, &vols[i].vol);
    return 1;
}

static int vol_hdr_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;
    struct Sigmet_Vol vol;
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    vol = vols[i].vol;
    fprintf(out, "site_name=\"%s\"\n", vol.ih.ic.su_site_name);
    fprintf(out, "radar_lon=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol.ih.ic.longitude), 0.0) * DEG_PER_RAD);
    fprintf(out, "radar_lat=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol.ih.ic.latitude), 0.0) * DEG_PER_RAD);
    fprintf(out, "task_name=\"%s\"\n", vol.ph.pc.task_name);
    fprintf(out, "types=\"");
    fprintf(out, "%s", Sigmet_DataType_Abbrv(vol.types[0]));
    for (y = 1; y < vol.num_types; y++) {
	fprintf(out, " %s", Sigmet_DataType_Abbrv(vol.types[y]));
    }
    fprintf(out, "\"\n");
    fprintf(out, "num_sweeps=%d\n", vol.ih.ic.num_sweeps);
    fprintf(out, "num_rays=%d\n", vol.ih.ic.num_rays);
    fprintf(out, "num_bins=%d\n", vol.ih.tc.tri.num_bins_out);
    fprintf(out, "range_bin0=%d\n", vol.ih.tc.tri.rng_1st_bin);
    fprintf(out, "bin_step=%d\n", vol.ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * vol.ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = vol.ih.tc.tdi.prf;
    mp = vol.ih.tc.tdi.m_prf_mode;
    vel_ua = -1.0;
    switch (mp) {
	case ONE_ONE:
	    mp_s = "1:1";
	    vel_ua = 0.25 * wavlen * prf;
	    break;
	case TWO_THREE:
	    mp_s = "2:3";
	    vel_ua = 2 * 0.25 * wavlen * prf;
	    break;
	case THREE_FOUR:
	    mp_s = "3:4";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
	case FOUR_FIVE:
	    mp_s = "4:5";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
    }
    fprintf(out, "prf=%.2lf\n", prf);
    fprintf(out, "prf_mode=%s\n", mp_s);
    fprintf(out, "vel_ua=%.3lf\n", vel_ua);
    return 1;
}

static int near_sweep_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;		/* Sweep angle, degrees, from command line */
    double ang, da;
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;
    struct Sigmet_Vol vol;
    int s, nrst;

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s angle sigmet_volume\n", argv0, argv1);
	return 0;
    }
    ang_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(err, "%s %s: expected floating point for sweep angle, got %s\n",
		argv0, argv1, ang_s);
	return 0;
    }
    ang *= RAD_PER_DEG;
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    vol = vols[i].vol;
    if ( !vol.sweep_angle ) {
	fprintf(err, "%s %s: sweep angles not loaded for %s. "
		"(Is volume truncated?) Please load the entire volume.\n",
		argv0, argv1, vol_nm);
	return 0;
    }
    nrst = -1;
    for (da = DBL_MAX, s = 0; s < vol.ih.tc.tni.num_sweeps; s++) {
	if ( fabs(vol.sweep_angle[s] - ang) < da ) {
	    da = fabs(vol.sweep_angle[s] - ang);
	    nrst = s;
	}
    }
    fprintf(out, "%d\n", nrst);
    return 1;
}

static int ray_headers_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;
    struct Sigmet_Vol vol;
    int s, r;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    vol = vols[i].vol;
    for (s = 0; s < vol.ih.tc.tni.num_sweeps; s++) {
	for (r = 0; r < (int)vol.ih.ic.num_rays; r++) {
	    int yr, mon, da, hr, min;
	    double sec;

	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(out, "sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		fprintf(err, "%s %s: bad ray time\n", argv0, argv1);
		return 0;
	    }
	    fprintf(out, "%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    fprintf(out, "az %7.3f %7.3f | ",
		    vol.ray_az0[s][r] * DEG_PER_RAD,
		    vol.ray_az1[s][r] * DEG_PER_RAD);
	    fprintf(out, "tilt %6.3f %6.3f\n",
		    vol.ray_tilt0[s][r] * DEG_PER_RAD,
		    vol.ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }
    return 1;
}

static int data_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;
    struct Sigmet_Vol vol;
    int s, y, r, b;
    char *abbrv;
    float d;
    enum Sigmet_DataType type;
    int all = -1;

    abbrv = Sigmet_DataType_Abbrv(DB_ERROR);

    /*
       Identify input and desired output
       Possible forms:
	   data	vol_nm		(argc = 3)
	   data y vol_nm	(argc = 4)
	   data y s vol_nm	(argc = 5)
	   data y s r vol_nm	(argc = 6)
	   data y s r b vol_nm	(argc = 7)
     */

    y = s = r = b = all;
    type = DB_ERROR;
    if (argc > 3 && (type = Sigmet_DataType(argv[2])) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return 0;
    }
    if (argc > 4 && sscanf(argv[3], "%d", &s) != 1) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return 0;
    }
    if (argc > 5 && sscanf(argv[4], "%d", &r) != 1) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return 0;
    }
    if (argc > 6 && sscanf(argv[5], "%d", &b) != 1) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return 0;
    }
    if (argc > 7) {
	fprintf(err, "Usage: %s %s [type] [sweep] [ray] sigmet_volume\n",
		argv0, argv1);
	return 0;
    }
    vol_nm_r = argv[argc - 1];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    vol = vols[i].vol;

    if (type != DB_ERROR) {
	/*
	   User has specified a data type.  Search for it in the volume,
	   and set y to the specified type (instead of all).
	 */
	abbrv = Sigmet_DataType_Abbrv(type);
	for (y = 0; y < vol.num_types; y++) {
	    if (type == vol.types[y]) {
		break;
	    }
	}
	if (y == vol.num_types) {
	    fprintf(err, "%s %s: data type %s not in %s\n",
		    argv0, argv1, abbrv, vol_nm);
	    return 0;
	}
    }
    if (s != all && s >= vol.ih.ic.num_sweeps) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }
    if (r != all && r >= (int)vol.ih.ic.num_rays) {
	fprintf(err, "%s %s: ray index %d out of range for %s\n",
		argv0, argv1, r, vol_nm);
	return 0;
    }
    if (b != all && b >= vol.ih.tc.tri.num_bins_out) {
	fprintf(err, "%s %s: bin index %d out of range for %s\n",
		argv0, argv1, b, vol_nm);
	return 0;
    }

    /* Write */
    if (y == all && s == all && r == all && b == all) {
	for (y = 0; y < vol.num_types; y++) {
	    type = vol.types[y];
	    abbrv = Sigmet_DataType_Abbrv(type);
	    for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
		fprintf(out, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(out, "ray %d: ", r);
		    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
			if (Sigmet_IsData(d)) {
			    fprintf(out, "%f ", d);
			} else {
			    fprintf(out, "nodat ");
			}
		    }
		    fprintf(out, "\n");
		}
	    }
	}
    } else if (s == all && r == all && b == all) {
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    fprintf(out, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		fprintf(out, "ray %d: ", r);
		for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		    if (Sigmet_IsData(d)) {
			fprintf(out, "%f ", d);
		    } else {
			fprintf(out, "nodat ");
		    }
		}
		fprintf(out, "\n");
	    }
	}
    } else if (r == all && b == all) {
	fprintf(out, "%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(out, "ray %d: ", r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else if (b == all) {
	if (vol.ray_ok[s][r]) {
	    fprintf(out, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else {
	if (vol.ray_ok[s][r]) {
	    fprintf(out, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
	    if (Sigmet_IsData(d)) {
		fprintf(out, "%f ", d);
	    } else {
		fprintf(out, "nodat ");
	    }
	    fprintf(out, "\n");
	}
    }
    return 1;
}

static int bin_outline_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;
    struct Sigmet_Vol vol;
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if (argc != 6) {
	fprintf(err, "Usage: %s %s sweep ray bin sigmet_volume\n", argv0, argv1);
	return 0;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];
    vol_nm_r = argv[5];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    vol = vols[i].vol;

    if (sscanf(s_s, "%d", &s) != 1) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    if (sscanf(r_s, "%d", &r) != 1) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return 0;
    }
    if (sscanf(b_s, "%d", &b) != 1) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }
    if (r >= vol.ih.ic.num_rays) {
	fprintf(err, "%s %s: ray index %d out of range for %s\n",
		argv0, argv1, r, vol_nm);
	return 0;
    }
    if (b >= vol.ih.tc.tri.num_bins_out) {
	fprintf(err, "%s %s: bin index %d out of range for %s\n",
		argv0, argv1, b, vol_nm);
	return 0;
    }
    if ( !Sigmet_BinOutl(&vol, s, r, b, corners) ) {
	fprintf(err, "%s %s: could not compute bin outlines "
		"for bin %d %d %d in %s\n", argv0, argv1, s, r, b, vol_nm);
	return 0;
    }
    c = (use_deg ? DEG_RAD : 1.0);
    fprintf(out, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;
}

static int bintvls_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol vol;	/* Volume from global vols array */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int i;			/* Volume index. */
    int y, s, r, b;		/* Indeces: data type, sweep, ray, bin */
    unsigned char n_clrs;	/* number of colors for the data type */
    double *bnds;		/* bounds[type_t] */
    unsigned char n_bnds;	/* number of bounds = n_clrs + 1 */
    int n;			/* Index from bnds */
    double d;			/* Data value */

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return 0;
    }
    if ( !DataType_GetColors(abbrv, &n_clrs, NULL, &bnds) ) {
	fprintf(err, "%s %s: cannot get data intervals for %s\n",
		argv0, argv1, abbrv);
	return 0;
    }
    n_bnds = n_clrs + 1;
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }

    /* Make sure volume, type, and sweep are valid */
    vol = vols[i].vol;
    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }

    /* Determine which interval from bounds each bin value is in and print. */
    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( vol.ray_ok[s][r] ) {
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
		if ( Sigmet_IsData(d)
			&& (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    fprintf(out, "%6d: %3d %5d\n", n, r, b);
		}
	    }
	}
    }

    return 1;
}

/* Change radar longitude to given value, which must be given in degrees */
static int radar_lon_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;			/* Volume index. */
    char *lon_s;		/* New longitude, degrees, in argv */
    double lon;			/* New longitude, degrees */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s new_lon sigmet_volume\n", argv0, argv1);
	return 0;
    }
    lon_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for new longitude, "
		"got %s\n", argv0, argv1, lon_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vols[i].vol.ih.ic.longitude = Sigmet_RadBin4(lon);

    return 1;
}

/* Change radar latitude to given value, which must be given in degrees */
static int radar_lat_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;			/* Volume index. */
    char *lat_s;		/* New latitude, degrees, in argv */
    double lat;			/* New latitude, degrees */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s new_lat sigmet_volume\n", argv0, argv1);
	return 0;
    }
    lat_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for new latitude, "
		"got %s\n", argv0, argv1, lat_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vols[i].vol.ih.ic.latitude = Sigmet_RadBin4(lat);

    return 1;
}

/* Change ray azimuths to given value, which must be given in degrees */
static int shift_az_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int i;			/* Volume index. */
    char *daz_s;		/* Amount to add to each azimuth, deg, in argv */
    double daz;			/* Amount to add to each azimuth, radians */
    double idaz;		/* Amount to add to each azimuth, binary angle */
    int s, r;			/* Loop indeces */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s dz sigmet_volume\n", argv0, argv1);
	return 0;
    }
    daz_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(err, "%s %s: expected float value for azimuth shift, got %s\n",
		argv0, argv1, daz_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    idaz = Sigmet_RadBin4(daz);
    switch (vols[i].vol.ih.tc.tni.scan_mode) {
	case RHI:
	    for (s = 0; s < vols[i].vol.ih.ic.num_sweeps; s++) {
		vols[i].vol.ih.tc.tni.scan_info.rhi_info.az[s] += idaz;
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    for (s = 0; s < vols[i].vol.ih.ic.num_sweeps; s++) {
		vols[i].vol.ih.tc.tni.scan_info.ppi_info.left_az += idaz;
		vols[i].vol.ih.tc.tni.scan_info.ppi_info.right_az += idaz;
	    }
	    break;
	case FILE_SCAN:
	    vols[i].vol.ih.tc.tni.scan_info.file_info.az0 += idaz;
	case MAN_SCAN:
	    break;
    }
    for (s = 0; s < vols[i].vol.ih.tc.tni.num_sweeps; s++) {
	for (r = 0; r < (int)vols[i].vol.ih.ic.num_rays; r++) {
	    vols[i].vol.ray_az0[s][r]
		= GeogLonR(vols[i].vol.ray_az0[s][r] + daz, 180.0 * RAD_PER_DEG);
	    vols[i].vol.ray_az1[s][r]
		= GeogLonR(vols[i].vol.ray_az1[s][r] + daz, 180.0 * RAD_PER_DEG);
	}
    }
    return 1;
}

#ifdef PROJ4
static int proj_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    projPJ t_pj;

    if ( !(t_pj = pj_init(argc - 2, argv + 2)) ) {
	int a;

	fprintf(err, "%s %s: unknown projection\n", argv0, argv1);
	for (a = argc + 2; a < argc; a++) {
	    fprintf(err, "%s ", argv[a]);
	}
	fprintf(err, "\n");
	return 0;
    }
    if ( pj ) {
	pj_free(pj);
    }
    pj = t_pj;
    return 1;
}
#endif

/* Specify image width in pixels */
static int img_sz_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *w_pxl_s;

    if ( argc == 2 ) {
	fprintf(out, "%d\n", w_pxl);
	return 1;
    } else if ( argc == 3 ) {
	w_pxl_s = argv[2];
	if ( sscanf(w_pxl_s, "%d", &w_pxl) != 1 ) {
	    fprintf(err, "%s %s: expected integer for display width, got %s\n",
		    argv0, argv1, w_pxl_s);
	    return 0;
	}
	h_pxl = w_pxl;
	return 1;
    } else {
	fprintf(err, "Usage: %s %s width_pxl\n", argv0, argv1);
	return 0;
    }
}

/* Identify image generator */
static int img_app_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *img_app_s;
    struct stat sbuf;
    mode_t m = S_IXUSR | S_IXGRP | S_IXOTH;	/* Executable mode */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s img_app\n", argv0, argv1);
	return 0;
    }
    img_app_s = argv[2];
    if ( stat(img_app_s, &sbuf) == -1 ) {
	fprintf(err, "%s %s: could not get information about %s.\n%s\n",
		argv0, argv1, img_app_s, strerror(errno));
	return 0;
    }
    if ( ((sbuf.st_mode & S_IFREG) != S_IFREG) || ((sbuf.st_mode & m) != m) ) {
	fprintf(err, "%s %s: %s is not executable.\n",
		argv0, argv1, img_app_s);
	return 0;
    }
    if ( snprintf(img_app, LEN, "%s", img_app_s) > LEN ) {
	fprintf(err, "%s %s: name of image application too long.\n",
		argv0, argv1);
	return 0;
    }
    return 1;
}

/* Specify image alpha channel */
static int alpha_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *alpha_s;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s value\n", argv0, argv1);
	return 0;
    }
    alpha_s = argv[2];
    if ( sscanf(alpha_s, "%lf", &alpha) != 1 ) {
	fprintf(err, "%s %s: expected float value for alpha value, got %s\n",
		argv0, argv1, alpha_s);
	return 0;
    }
    return 1;
}

/*
   Print name of the image that img_cb would create for volume vol,
   type abbrv, sweep s to buf, which must have space for LEN characters,
   including nul.  Returned name has no suffix (e.g. "png" or "kml").
   If something goes wrong, fill buf with nul's and return 0.
 */
static int img_name(struct Sigmet_Vol *vol_p, char *abbrv, int s, char *buf,
	FILE *err)
{
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */

    memset(buf, 0, LEN);
    if ( s >= vol_p->ih.ic.num_sweeps || !vol_p->sweep_ok[s]
	    || !Tm_JulToCal(vol_p->sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	fprintf(err, "Sweep %d not valid in volume\n", s);
	return 0;
    }
    if ( snprintf(buf, LEN, "%s_%02d%02d%02d%02d%02d%02.0f_%s_%.1f",
		vol_p->ih.ic.hw_site_name, yr, mo, da, h, mi, sec,
		abbrv, vol_p->sweep_angle[s] * DEG_PER_RAD) > LEN ) {
	memset(buf, 0, LEN);
	fprintf(err, "Image file name too long\n");
	return 0;
    }
    return 1;
}

/* Print the name of the image that img would create */
static int img_name_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol vol;	/* Volume from global vols array */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int i;
    int y, s;			/* Indeces: data type, sweep */
    char img_fl_nm[LEN];	/* Name of image file */

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return 0;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }

    /* Make sure volume, type, and sweep are valid */
    vol = vols[i].vol;
    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }

    /* Print name of output file */
    if ( !img_name(&vol, abbrv, s, img_fl_nm, err) ) {
	fprintf(err, "%s %s: could not make image file name\n", argv0, argv1);
	return 0;
    }
    fprintf(out, "%s.png\n", img_fl_nm);

    return 1;
}

static int img_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol vol;	/* Volume from global vols array */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int i;			/* Volume index. */
    int y, s, r, b;		/* Indeces: data type, sweep, ray, bin */
    double left;		/* Map coordinate of left side */
    double rght;		/* Map coordinate of right side */
    double btm;			/* Map coordinate of bottom */
    double top;			/* Map coordinate of top */
    struct DataType_Color *clrs; /* colors array */
    unsigned char n_clrs;	/* Number of colors = number of bounds - 1 */
    double *bnds;		/* bounds for each color */
    unsigned char n_bnds;	/* number of bounds = n_clrs + 1 */
    double d;			/* Data value */
    projUV radar;		/* Radar location lon-lat or x-y */
    projUV edge;		/* Point on the edge of the display area, needed
				   to compute display bounds */
    double cnrs_ll[8];		/* Corners of a gate, lon-lat */
    double *ll;			/* Element from cnrs_ll */
    double ray_len;		/* Ray length, meters or great circle radians */
    double west, east;		/* Display area longitude limits */
    double south, north;	/* Display area latitude limits */
    projUV cnrs_uv[4];		/* Corners of a gate, lon-lat or x-y */
    projUV *uv;			/* Element from cnrs_uv */
    int n;			/* Loop index */
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */
    char base_nm[LEN]; 		/* Output file name */
    char img_fl_nm[LEN]; 	/* Image output file name */
    size_t img_fl_nm_l;		/* strlen(img_fl_nm) */
    int flags;                  /* Image file creation flags */
    mode_t mode;                /* Image file permissions */
    int i_img_fl;		/* Image file descriptor, not used */
    char kml_fl_nm[LEN];	/* KML output file name */
    FILE *kml_fl;		/* KML file */
    pid_t img_pid = -1;		/* Process id for image generator */
    FILE *img_out = NULL;	/* Where to send outlines to draw */
    struct XDRX_Stream xout;	/* XDR stream for img_out */
    jmp_buf err_jmp;		/* Handle output errors with setjmp, longjmp */
    char *item = NULL;		/* Item being written. Needed for error message. */
    pid_t p;			/* Return from waitpid */
    int status;			/* Exit status of image generator */
    int pfd[2];			/* Pipe for data */
    double px_per_m;		/* Display units per map unit */

    /* KML template for positioning result */
    char kml_tmpl[] = 
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
	"  <GroundOverlay>\n"
	"    <name>%s sweep</name>\n"
	"    <description>"
	"      %s at %02d/%02d/%02d %02d:%02d:%02.0f. Field: %s. %04.1f degrees"
	"    </description>\n"
	"    <Icon>%s</Icon>\n"
	"    <LatLonBox>\n"
	"      <north>%f</north>\n"
	"      <south>%f</south>\n"
	"      <west>%f</west>\n"
	"      <east>%f</east>\n"
	"    </LatLonBox>\n"
	"  </GroundOverlay>\n"
	"</kml>\n";

    if ( strlen(img_app) == 0 ) {
	fprintf(err, "%s %s: sweep drawing application not set\n",
		argv0, argv1);
	return 0;
    }

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return 0;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return 0;
    }
    if ( !DataType_GetColors(abbrv, &n_clrs, &clrs, &bnds) || n_clrs == 0
	    || !clrs || !bnds ) {
	fprintf(err, "%s %s: colors and bounds not set for %s\n",
		argv0, argv1, abbrv);
	return 0;
    }
    n_bnds = n_clrs + 1;
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	fprintf(err, "%s %s: %s not loaded or was unloaded due to being truncated."
		" Please (re)load with read command.\n", argv0, argv1, vol_nm);
	return 0;
    }

    /* Make sure volume, type, and sweep are valid */
    vol = vols[i].vol;
    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return 0;
    }

    /*
       To obtain longitude, latitude limits of display area, put radar
       at center, and step ray length to the north, south, west, and east.
       GeogStep expects step of great circle radians, not meters.  Convert
       meters to radians by multiplying by:
       pi radian / 180 deg * 1 deg / 60 nmi * 1 nmi / 1852 meters 
       This conversion is based on the International Standard Nautical Mile,
       which is 1852 meters.
     */

    radar.u = Sigmet_Bin4Rad(vol.ih.ic.longitude);
    radar.v = Sigmet_Bin4Rad(vol.ih.ic.latitude);
    ray_len = 0.01 * (vol.ih.tc.tri.rng_1st_bin
	    + (vol.ih.tc.tri.num_bins_out + 1) * vol.ih.tc.tri.step_out);
    ray_len = ray_len * M_PI / (180.0 * 60.0 * 1852.0);

    /* Left (west) side */
    GeogStep(radar.u, radar.v, 270.0 * RAD_PER_DEG, ray_len, &west, &d);
    edge.u = west;
    edge.v = d;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	fprintf(err, "%s %s: west edge of map not defined in current projection\n",
		argv0, argv1);
	return 0;
    }
    left = edge.u;

    /* Right (east) side */
    GeogStep(radar.u, radar.v, 90.0 * RAD_PER_DEG, ray_len, &east, &d);
    edge.u = east;
    edge.v = d;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	fprintf(err, "%s %s: east edge of map not defined in current projection\n",
		argv0, argv1);
	return 0;
    }
    rght = edge.u;

    /* Bottom (south) side */
    GeogStep(radar.u, radar.v, 180.0 * RAD_PER_DEG, ray_len, &d, &south);
    edge.u = d;
    edge.v = south;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	fprintf(err, "%s %s: south edge of map not defined in current "
		"projection\n", argv0, argv1);
	return 0;
    }
    btm = edge.v;

    /* Top (north) side */
    GeogStep(radar.u, radar.v, 0.0, ray_len, &d, &north);
    edge.u = d;
    edge.v = north;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	fprintf(err, "%s %s: north edge of map not defined in current "
		"projection\n",
		argv0, argv1);
	return 0;
    }
    top = edge.v;

    west *= DEG_PER_RAD;
    east *= DEG_PER_RAD;
    south *= DEG_PER_RAD;
    north *= DEG_PER_RAD;
    px_per_m = w_pxl / (rght - left);

    /* Create image file. Fail if it exists */
    if ( !img_name(&vol, abbrv, s, base_nm, err) 
	    || snprintf(img_fl_nm, LEN, "%s/%s.png", cl_wd, base_nm) > LEN ) {
	fprintf(err, "%s %s: could not make image file name\n", argv0, argv1);
	return 0;
    }
    flags = O_CREAT | O_EXCL | O_WRONLY;
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if ( (i_img_fl = open(img_fl_nm, flags, mode)) == -1 ) {
        fprintf(err, "%s %s: could not create image file %s\n%s\n",
                argv0, argv1, img_fl_nm, strerror(errno));
        return 0;
    }
    if ( close(i_img_fl) == -1 ) {
        fprintf(err, "%s %s: could not close image file %s\n%s\n",
                argv0, argv1, img_fl_nm, strerror(errno));
	unlink(img_fl_nm);
        return 0;
    }

    /* Launch the external drawing application and create a pipe to it. */
    if ( pipe(pfd) == -1 ) {
	fprintf(err, "%s %s: could not connect to image drawing application\n%s\n",
		argv0, argv1, strerror(errno));
	unlink(img_fl_nm);
	return 0;
    }
    img_pid = fork();
    switch (img_pid) {
	case -1:
	    fprintf(err, "%s %s: could not spawn image drawing application\n%s\n",
		    argv0, argv1, strerror(errno));
	    unlink(img_fl_nm);
	    return 0;
	case 0:
	    /*
	       Child.  Close stdout.
	       Read polygons from stdin (read side of data pipe).
	     */
	    if ( dup2(pfd[0], STDIN_FILENO) == -1
		    || close(pfd[0]) == -1 || close(pfd[1]) == -1 ) {
		fprintf(err, "%s: could not set up %s process",
			time_stamp(), img_app);
		_exit(EXIT_FAILURE);
	    }
	    if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		fprintf(err, "%s: image process could not access error "
			"stream\n%s\n", time_stamp(), strerror(errno));
		_exit(EXIT_FAILURE);
	    }
	    execl(img_app, img_app, (char *)NULL);
	    _exit(EXIT_FAILURE);
	default:
	    /*
	       This process.  Send polygon to write side of pipe, a.k.a. img_out.
	     */
	    if ( close(pfd[0]) == -1 || !(img_out = fdopen(pfd[1], "w"))) {
		fprintf(err, "%s %s: could not write to image drawing "
			"application\n%s\n", argv0, argv1, strerror(errno));
		unlink(img_fl_nm);
		img_pid = -1;
		return 0;
	    }
    }
    XDRX_StdIO_Create(&xout, img_out, XDR_ENCODE);

    /* Come back here if write to image generator fails */
    switch (setjmp(err_jmp)) {
	case XDRX_OK:
	    /* Initializing */
	    break;
	case XDRX_ERR:
	    /* Fail */
	    fprintf(err, "%s %s: could not write %s for image %s\n",
		    argv0, argv1, item, img_fl_nm);
	    goto error;
	    break;
	case XDRX_EOF:
	    /* Not used */
	    break;
    }

    /* Send global information about the image to drawing process */
    img_fl_nm_l = strlen(img_fl_nm);
    item = "image file name";
    XDRX_Put_UInt(img_fl_nm_l, &xout, err_jmp);
    XDRX_Put_String(img_fl_nm, img_fl_nm_l, &xout, err_jmp);
    item = "image dimensions";
    XDRX_Put_UInt(w_pxl, &xout, err_jmp);
    XDRX_Put_UInt(h_pxl, &xout, err_jmp);
    item = "image real bounds";
    XDRX_Put_Double(left, &xout, err_jmp);
    XDRX_Put_Double(rght, &xout, err_jmp);
    XDRX_Put_Double(top, &xout, err_jmp);
    XDRX_Put_Double(btm, &xout, err_jmp);
    item = "alpha channel value";
    XDRX_Put_Double(alpha, &xout, err_jmp);
    item = "colors";
    XDRX_Put_UInt(n_clrs, &xout, err_jmp);
    for (n = 0; n < n_clrs; n++) {
	XDRX_Put_UInt(clrs[n].red, &xout, err_jmp);
	XDRX_Put_UInt(clrs[n].green, &xout, err_jmp);
	XDRX_Put_UInt(clrs[n].blue, &xout, err_jmp);
    }

    /* Determine which interval from bounds each bin value is in. */
    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( vol.ray_ok[s][r] ) {
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
		if ( Sigmet_IsData(d) && (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    int undef = 0;	/* If true, gate is outside map */
		    size_t npts = 4;	/* Number of vertices */

		    if ( !Sigmet_BinOutl(&vol, s, r, b, cnrs_ll) ) {
			continue;
		    }
		    for (ll = cnrs_ll, uv = cnrs_uv; uv < cnrs_uv + 4; uv++) {
			uv->u = *ll++;
			uv->v = *ll++;
			*uv = pj_fwd(*uv, pj);
			if ( uv->u == HUGE_VAL || uv->v == HUGE_VAL ) {
			    undef = 1;
			    break;
			}
		    }
		    if ( undef ) {
			continue;
		    }

		    item = "polygon color index";
		    XDRX_Put_UInt(n, &xout, err_jmp);
		    item = "polygon point count";
		    XDRX_Put_UInt(npts, &xout, err_jmp);
		    item = "bin corner coordinates";
		    XDRX_Put_Double(cnrs_uv[0].u, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[0].v, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[1].u, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[1].v, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[2].u, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[2].v, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[3].u, &xout, err_jmp);
		    XDRX_Put_Double(cnrs_uv[3].v, &xout, err_jmp);
		}
	    }
	}
    }
    XDRX_Destroy(&xout);
    if ( fclose(img_out) == EOF ) {
	fprintf(err, "%s: could not close pipe to %d for image file %s.\n%s\n",
		time_stamp(), img_pid, img_fl_nm, strerror(errno));
    }
    img_out = NULL;
    p = waitpid(img_pid, &status, 0);
    if ( p == img_pid ) {
	if ( WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE ) {
	    fprintf(err, "%s %s: image process failed for %s\n",
		    argv0, argv1, img_fl_nm);
	    goto error;
	} else if ( WIFSIGNALED(status) ) {
	    fprintf(err, "%s %s: image process for %s exited on signal %d. ",
			argv0, argv1, img_fl_nm, WTERMSIG(status));
	    goto error;
	}
    } else {
	fprintf(err, "%s: could not get exit status for image generator "
		"while processing %s. %s. Continuing anyway.\n",
		time_stamp(), img_fl_nm,
		(p == -1) ? strerror(errno) : "Unknown error.");
    }

    /* Make kml file and return */
    if ( snprintf(kml_fl_nm, LEN, "%s/%s.kml", cl_wd, base_nm) > LEN ) {
	fprintf(err, "%s %s: could not make kml file name for %s\n",
		argv0, argv1, img_fl_nm);
	goto error;
    }
    if ( !(kml_fl = fopen(kml_fl_nm, "w")) ) {
	fprintf(err, "%s %s: could not open %s for output\n",
		argv0, argv1, kml_fl_nm);
	goto error;
    }
    if ( s >= vol.ih.ic.num_sweeps || !vol.sweep_ok[s]
	    || !Tm_JulToCal(vol.sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	fprintf(err, "%s %s: invalid sweep\n", argv0, argv1);
	goto error;
    }
    fprintf(kml_fl, kml_tmpl,
	    vol.ih.ic.hw_site_name, vol.ih.ic.hw_site_name,
	    yr, mo, da, h, mi, sec,
	    abbrv, vol.sweep_angle[s] * DEG_PER_RAD,
	    img_fl_nm,
	    north, south, west, east);
    fclose(kml_fl);

    fprintf(out, "%s\n", img_fl_nm);
    return 1;
error:
    if (img_out) {
	if ( fclose(img_out) == EOF ) {
	    fprintf(err, "%s: could not close pipe to %d "
		    "for image file %s.\n%s\n",
		    time_stamp(), img_pid, img_fl_nm, strerror(errno));
	}
    }
    if ( img_pid != -1 ) {
	kill(img_pid, SIGTERM);
	waitpid(img_pid, NULL, 0);
	img_pid = -1;
    }
    unlink(img_fl_nm);
    return 0;
}

static int stop_cb(int argc, char *argv[], char *cl_wd, int i_out, FILE *out,
	int i_err, FILE *err)
{
    struct sig_vol *sv_p;
    int y;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	Sigmet_FreeVol(&sv_p->vol);
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	DataType_Rm(Sigmet_DataType_Abbrv(y));
    }
    if ( unlink(SIGMET_RAWD_IN) == -1 ) {
	fprintf(stderr, "%s: could not delete input pipe.\n", time_stamp());
    }
    printf("%s: received stop command, exiting.\n", time_stamp());
    stop = 1;
    return 1;
}

/*
   Basic signal management.

   Reference --
       Rochkind, Marc J., "Advanced UNIX Programming, Second Edition",
       2004, Addison-Wesley, Boston.
 */
static int handle_signals(void)
{
    sigset_t set;
    struct sigaction act;
    
    if ( sigfillset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    memset(&act, 0, sizeof(struct sigaction));
    if ( sigfillset(&act.sa_mask) == -1 ) {
	perror(NULL);
	return 0;
    }

    /* Signals to ignore */
    act.sa_handler = SIG_IGN;
    if ( sigaction(SIGHUP, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGINT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGQUIT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGPIPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    /* Generic action for termination signals */
    act.sa_handler = handler;
    if ( sigaction(SIGTERM, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGFPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGSYS, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXCPU, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXFSZ, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigemptyset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    return 1;
}

/* For exit signals, print an error message if possible */
static void handler(int signum)
{
    switch (signum) {
	case SIGTERM:
	    write(STDERR_FILENO, " exiting on termination signal    \n", 35);
	    break;
	case SIGBUS:
	    write(STDERR_FILENO, " exiting on bus error             \n", 35);
	    break;
	case SIGFPE:
	    write(STDERR_FILENO, " exiting arithmetic exception     \n", 35);
	    break;
	case SIGILL:
	    write(STDERR_FILENO, " exiting illegal instruction      \n", 35);
	    break;
	case SIGSEGV:
	    write(STDERR_FILENO, " exiting invalid memory reference \n", 35);
	    break;
	case SIGSYS:
	    write(STDERR_FILENO, " exiting on bad system call       \n", 35);
	    break;
	case SIGXCPU:
	    write(STDERR_FILENO, " exiting: CPU time limit exceeded \n", 35);
	    break;
	case SIGXFSZ:
	    write(STDERR_FILENO, " exiting: file size limit exceeded\n", 35);
	    break;
    }
    unlink(SIGMET_RAWD_IN);
    _exit(EXIT_FAILURE);
}
