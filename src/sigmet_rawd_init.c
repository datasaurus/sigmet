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
 .	$Revision: 1.214 $ $Date: 2010/07/09 21:56:16 $
 */

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

/* Application and subcommand name */
static char *cmd;
static size_t cmd_len;			/* strlen(cmd) */
static char *cmd1;

/* If true, daemon has received a "stop" command */
static int stop = 0;

/* Process streams and files */
static int cl_io_fd;			/* File descriptor to read client command
					   and send results */
static FILE *cl_io;			/* Stream associated with cl_io_fd */
static pid_t vol_pid = -1;		/* Process providing a raw volume */

/* Where to put output and error messages */
#define SIGMET_RAWD_LOG "sigmet.log"
#define SIGMET_RAWD_ERR "sigmet.err"

/* Size for various strings */
#define LEN 1024

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
static int get_vol_i(char *);
static int new_vol_i(char *, struct stat *);

/* Subcommands */
#define NCMD 28
typedef int (callback)(int , char **);
static callback cmd_len_cb;
static callback pid_cb;
static callback types_cb;
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
    "cmd_len", "pid", "types", "good", "hread",
    "read", "list", "release", "unload", "flush", "volume_headers",
    "vol_hdr", "near_sweep", "ray_headers", "data", "bin_outline",
    "colors", "bintvls", "radar_lon", "radar_lat", "shift_az",
    "proj", "img_app", "img_sz", "alpha", "img_name", "img", "stop"
};
static callback *cb1v[NCMD] = {
    cmd_len_cb, pid_cb, types_cb, good_cb, hread_cb,
    read_cb, list_cb, release_cb, unload_cb, flush_cb, volume_headers_cb,
    vol_hdr_cb, near_sweep_cb, ray_headers_cb, data_cb, bin_outline_cb,
    DataType_SetColors_CB, bintvls_cb, radar_lon_cb, radar_lat_cb, shift_az_cb,
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
static FILE *vol_open(const char *);
static int flush(int);
static void unload(int);
static int img_name(struct Sigmet_Vol *, char *, int, char *);

/* Signal handling functions */
static int handle_signals(void);
static void handler(int);

#define SA_UN_SZ (sizeof(struct sockaddr_un))

int main(int argc, char *argv[])
{
    char *ddir;			/* Working directory for server */
    int bg = 1;			/* If true, run in foreground (do not fork) */
    pid_t pid;			/* Return from fork */
    int flags;			/* Flags for log files */
    mode_t mode;		/* Mode for log files */
    struct sockaddr_un d_io_sa;	/* Socket to read command and return results */
    struct sockaddr_un err_sa;	/* Socket to send status error info to client */
    size_t plen;		/* Length of socket address path */
    int err_fd;			/* File descriptor to send status and
				   error messages to client */
    FILE *err;			/* Stream associated with err_fd */
    struct sockaddr *sa_p;	/* &d_io_sa or &d_err_sa, for call to bind */
    int d_io_fd;		/* File descriptors for d_io and d_err */
    struct sig_vol *sv_p;	/* Member of vols */
    pid_t client_pid = -1;	/* Client */
    char *ang_u;		/* Angle unit */
    char buf[SIGMET_RAWD_ARGVX];/* Input line - has commands for the daemon */
    char *buf_e = buf + SIGMET_RAWD_ARGVX;
    char *b;			/* Point into buf */
    int y;			/* Loop index */
    char *dflt_proj[] = { "+proj=aeqd", "+ellps=sphere" }; /* Map projection */
    int i;

    cmd = argv[0];
    cmd_len = strlen(cmd);

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
	/* Put server in background */
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
    memset(&d_io_sa, '\0', SA_UN_SZ);
    d_io_sa.sun_family = AF_UNIX;
    plen = sizeof(d_io_sa.sun_path) - 1;
    strncpy(d_io_sa.sun_path, SIGMET_RAWD_IN, plen);
    sa_p = (struct sockaddr *)&d_io_sa;
    if ((d_io_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1
	    || bind(d_io_fd, sa_p, SA_UN_SZ) == -1
	    || listen(d_io_fd, SOMAXCONN) == -1) {
	fprintf(stderr, "%s: could not create io socket.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }

    printf("Daemon starting. Process id = %d\n", getpid());

    fflush(stdout);

    /* Wait for clients */
    while ( !stop && (cl_io_fd = accept(d_io_fd, NULL, 0)) != -1 ) {
	size_t l;		/* Number of bytes to read from command buffer */
	int argc1;		/* Number of arguments in received command line */
	char *argv1[SIGMET_RAWD_ARGCX]; /* Arguments from client command line */
	int a;			/* Index into argv1 */
	int success;		/* Result of callback */
	int i;			/* Loop index */

	/*
	   Client input is:
	   l		- number of bytes in client input
	   client pid	- native integer
	   argc		- native integer
	   arguments	- strings
	 */

	if ( !(cl_io = fdopen(cl_io_fd, "r+")) ) {
	    fprintf(stderr, "%s: failed to connect to client.\n", time_stamp());
	    close(cl_io_fd);
	    continue;
	}
	if ( read(cl_io_fd, &l, sizeof(size_t)) != sizeof(size_t) ) {
	    fprintf(stderr, "%s: failed to read command length.\n",
		    time_stamp());
	    close(cl_io_fd);
	    continue;
	}
	if ( l > SIGMET_RAWD_ARGVX ) {
	    fprintf(stderr, "%s: command with %lu bytes too large for buffer.\n",
		    time_stamp(), (unsigned long)l);
	    continue;
	}
	if ( read(cl_io_fd, buf, l) != l ) {
	    fprintf(stderr, "%s: failed to read command of %lu bytes. ",
		    time_stamp(), (unsigned long)l);
	    close(cl_io_fd);
	    continue;
	}

	/* Break command line into arguments */
	b = buf;
	client_pid = *(pid_t *)b;
	b += sizeof(client_pid);
	argc1 = *(int *)b;
	b += sizeof(argc1);
	if ( argc1 > SIGMET_RAWD_ARGCX ) {
	    fprintf(stderr, "%s: unable to parse command with %d arguments. "
		    "Limit is %d\n", time_stamp(), argc1, SIGMET_RAWD_ARGCX );
	    close(cl_io_fd);
	    continue;
	}
	for (a = 0, argv1[a] = b; b < buf_e && a < argc1; b++) {
	    if ( *b == '\0' ) {
		argv1[++a] = b + 1;
	    }
	}
	if ( b == buf_e ) {
	    fprintf(stderr, "%s: command line gives no destination.\n",
		    time_stamp());
	    continue;
	}

	/* Identify command */
	cmd1 = argv1[0];
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    success = 0;
	    Err_Append("No option or subcommand named");
	    Err_Append(cmd1);
	    Err_Append("\n");
	    Err_Append("Subcommand must be one of: ");
	    for (i = 0; i < NCMD; i++) {
		Err_Append(cmd1v[i]);
		Err_Append(" ");
	    }
	    Err_Append("\n");
	    fclose(cl_io);
	} else {
	    /* Run command. Callback will send "standard output" to cl_io. */
	    success = (cb1v[i])(argc1, argv1);
	    fclose(cl_io);
	}

	/* Send status and error messages to the second socket */
	memset(&err_sa, '\0', SA_UN_SZ);
	err_sa.sun_family = AF_UNIX;
	plen = sizeof(err_sa.sun_path);
	if ( snprintf(err_sa.sun_path, plen, "%d.err", client_pid) > plen) {
	    fprintf(stderr, "%s: could not create error socket path name "
		    "for process %d.\n", cmd, client_pid);
	    continue;
	}
	sa_p = (struct sockaddr *)&err_sa;
	if ( (err_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	    fprintf(stderr, "%s: could not create error socket for process %d.\n"
		    "%s\n", cmd, client_pid, strerror(errno));
	    continue;
	}
	if ( (connect(err_fd, sa_p, sizeof(struct sockaddr_un))) == -1 ) {
	    fprintf(stderr, "%s: could not connect error socket for process %d\n"
		    "%s\n", cmd, client_pid, strerror(errno));
	    continue;
	}
	if ( !(err = fdopen(err_fd, "w")) ) {
	    fprintf(stderr, "%s: could not create error stream for process %d\n"
		    "%s\n", cmd, client_pid, strerror(errno));
	    continue;
	}
	if ( success ) {
	    if ( fputc(EXIT_SUCCESS, err) == EOF ) {
		fprintf(err, "%s: could not send return code for %s.\n"
			"%s\n", time_stamp(), cmd1, strerror(errno) );
	    }
	} else {
	    if ( fputc(EXIT_FAILURE, err) == EOF
		    || fprintf(err, "%s failed.\n%s\n",
			cmd1, Err_Get()) == -1 ) {
		fprintf(stderr, "%s: could not send return code or error "
			"output for %s.\n%s\n",
			time_stamp(), cmd1, strerror(errno) );
	    }
	}
	if ( fclose(err) == EOF ) {
	    fprintf(stderr, "%s: could not close client error stream "
		    "for process %d\n%s\n", cmd, client_pid, strerror(errno));
	}

    }

    /* Should not end up here. Process should exit with "stop" command. */
    fprintf(stderr, "%s: unexpected exit.  %s\n", time_stamp(), strerror(errno));
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

static int cmd_len_cb(int argc, char *argv[])
{
    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    fprintf(cl_io, "%d\n", SIGMET_RAWD_ARGVX);
    return 1;
}

static int pid_cb(int argc, char *argv[])
{
    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    fprintf(cl_io, "%d\n", getpid());
    return 1;
}

static int types_cb(int argc, char *argv[])
{
    int y;

    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	fprintf(cl_io, "%s | %s\n",
		Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return 1;
}

/*
   Open volume file vol_nm, possibly via a pipe.
   Return file handle, or NULL if failure.
   If file is actually a process, global variable vol_pid gets the
   child's process id.
 */
static FILE *vol_open(const char *vol_nm)
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
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for gzip.  ");
	    goto error;
	}
	vol_pid = fork();
	switch (vol_pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		goto error;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( close(cl_io_fd) == -1 ) {
		    fprintf(stderr, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(stderr, "%s: gzip process failed\n%s\n",
			    time_stamp(), strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read gzip process.  ");
		    vol_pid = -1;
		    goto error;
		} else {
		    return in;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/* If filename ends with ".bz2", read from bunzip2 pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for bzip2.  ");
	    goto error;
	}
	vol_pid = fork();
	switch (vol_pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn bzip2 process.  ");
		goto error;
	    case 0:
		/* Child process - bzip2.  Send child stdout to pipe. */
		if ( close(cl_io_fd) == -1 ) {
		    fprintf(stderr, "%s: bzip2 child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(stderr, "%s: could not set up bzip2 process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from bzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read bzip2 process.  ");
		    vol_pid = -1;
		    goto error;
		} else {
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	/* Uncompressed file */
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(" for input.\n");
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

static int good_cb(int argc, char *argv[])
{
    char *vol_nm;
    FILE *in;
    int rslt;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    if ( !(in = vol_open(vol_nm)) ) {
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(". ");
	return 0;
    }
    rslt = Sigmet_GoodVol(in);
    fclose(in);
    if ( vol_pid != -1 ) {
	waitpid(vol_pid, NULL, 0);
	vol_pid = -1;
    }
    if ( !rslt ) {
	Err_Append("Could not navigate ");
	Err_Append(vol_nm);
	Err_Append(". ");
    }
    fclose(in);
    return rslt;
}

static int hread_cb(int argc, char *argv[])
{
    int loaded;			/* If true, volume is loaded */
    int trying;			/* If true, still attempting to read volume */
    int i;			/* Index into vols */
    struct stat sbuf;   	/* Information about file to read */
    char *vol_nm;		/* Sigmet raw file */
    FILE *in;			/* Stream from Sigmet raw file */

    if ( argc == 2 ) {
	vol_nm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }

    /* If volume is already loaded, increment user count return. */
    if ( (i = get_vol_i(vol_nm)) >= 0 ) {
	vols[i].users++;
	fprintf(cl_io, "%s loaded%s.\n",
		vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
	return 1;
    }

    /* Volume is not loaded. Try to find a slot in which to load it. */
    if ( (i = new_vol_i(vol_nm, &sbuf)) == -1 ) {
	Err_Append("Could not find a slot while attempting to (re)load ");
	Err_Append(vol_nm);
	Err_Append(". ");
	return 0;
    }

    /* Read headers */
    for (trying = 1, loaded = 0; trying && !loaded; ) {
	vol_pid = -1;
	if ( !(in = vol_open(vol_nm)) ) {
	    Err_Append("Could not open ");
	    Err_Append(vol_nm);
	    Err_Append(" for input. ");
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
		fprintf(stderr, "Allocation failure. Flushing.\n");
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
		fprintf(stderr, "%s: could not clean up afer input process for "
			"%s. %s. Continuing anyway.\n",
			time_stamp(), vol_nm, strerror(errno));
	    }
	    vol_pid = -1;
	}
    }
    if ( !loaded ) {
	Err_Append("Could not read headers from ");
	Err_Append(vol_nm);
	Err_Append(".\n");
    }
    vols[i].oqpd = 1;
    stat(vol_nm, &sbuf);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].users++;
    fprintf(cl_io, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int read_cb(int argc, char *argv[])
{
    int loaded;			/* If true, volume is loaded */
    int trying;			/* If true, still attempting to read volume */
    struct stat sbuf;   	/* Information about file to read */
    int i;			/* Index into vols */
    char *vol_nm;		/* Sigmet raw file */
    FILE *in;			/* Stream from Sigmet raw file */

    if ( argc == 2 ) {
	vol_nm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }

    /*
       If volume is not loaded, seek an unused slot in vols and attempt load.
       If volume loaded but truncated, free it and attempt reload.
       If volume is loaded and complete, increment user count return.
     */

    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	if ( (i = new_vol_i(vol_nm, &sbuf)) == -1 ) {
	    Err_Append("Could not find a slot while attempting to (re)load ");
	    Err_Append(vol_nm);
	    Err_Append(". ");
	    return 0;
	}
    } else if ( i >= 0 && vols[i].vol.truncated ) {
	unload(i);
    } else {
	vols[i].users++;
	fprintf(cl_io, "%s loaded.\n", vol_nm);
	return 1;
    }

    /* Volume was not loaded, or was freed because truncated. Read volume */
    for (trying = 1, loaded = 0; trying && !loaded; ) {
	vol_pid = -1;
	if ( !(in = vol_open(vol_nm)) ) {
	    Err_Append("Could not open ");
	    Err_Append(vol_nm);
	    Err_Append(" for input. ");
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
		fprintf(stderr, "%s: could not clean up afer input process for "
			"%s. %s. Continuing anyway.\n",
			time_stamp(), vol_nm, strerror(errno));
	    }
	    vol_pid = -1;
	}
    }
    if ( !loaded ) {
	Err_Append("Could not read volume from ");
	Err_Append(vol_nm);
	Err_Append(".\n");
    }
    vols[i].oqpd = 1;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    vols[i].users++;
    fprintf(cl_io, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int list_cb(int argc, char *argv[])
{
    int i;

    for (i = 0; i < N_VOLS; i++) {
	if ( vols[i].oqpd != 0 ) {
	    fprintf(cl_io, "%s users=%d %s\n",
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
	Err_Append("Could not get information about volume file ");
	Err_Append(vol_nm);
	Err_Append("\n");
	Err_Append(strerror(errno));
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
static int new_vol_i(char *vol_nm, struct stat *sbuf_p)
{
    int h, i;			/* Index into vols */

    if ( (h = hash(vol_nm, sbuf_p)) == -1 ) {
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
    return -1;
}

static int release_cb(int argc, char *argv[])
{
    char *vol_nm;
    int i;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
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
static int unload_cb(int argc, char *argv[])
{
    char *vol_nm;
    int i;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    i = get_vol_i(vol_nm);
    if ( i >= 0 ) {
	if ( vols[i].users <= 0 ) {
	    unload(i);
	} else {
	    Err_Append(vol_nm);
	    Err_Append(" in use.");
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
static int flush_cb(int argc, char *argv[])
{
    char *c_s;
    int c;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" count");
	return 0;
    }
    c_s = argv[1];
    if (sscanf(c_s, "%d", &c) != 1) {
	Err_Append("Expected an integer for count, got ");
	Err_Append(c_s);
	Err_Append(". ");
	return 0;
    }
    if ( !flush(c) ) {
	Err_Append("Unable to free ");
	Err_Append(c_s);
	Err_Append(" volumes. ");
	return 0;
    } else {
	return 1;
    }
}

static int volume_headers_cb(int argc, char *argv[])
{
    char *vol_nm;
    int i;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
	return 0;
    }
    Sigmet_PrintHdr(cl_io, &vols[i].vol);
    return 1;
}

static int vol_hdr_cb(int argc, char *argv[])
{
    char *vol_nm;
    int i;
    struct Sigmet_Vol vol;
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
	return 0;
    }
    vol = vols[i].vol;
    fprintf(cl_io, "site_name=\"%s\"\n", vol.ih.ic.su_site_name);
    fprintf(cl_io, "radar_lon=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol.ih.ic.longitude), 0.0) * DEG_PER_RAD);
    fprintf(cl_io, "radar_lat=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol.ih.ic.latitude), 0.0) * DEG_PER_RAD);
    fprintf(cl_io, "task_name=\"%s\"\n", vol.ph.pc.task_name);
    fprintf(cl_io, "types=\"");
    fprintf(cl_io, "%s", Sigmet_DataType_Abbrv(vol.types[0]));
    for (y = 1; y < vol.num_types; y++) {
	fprintf(cl_io, " %s", Sigmet_DataType_Abbrv(vol.types[y]));
    }
    fprintf(cl_io, "\"\n");
    fprintf(cl_io, "num_sweeps=%d\n", vol.ih.ic.num_sweeps);
    fprintf(cl_io, "num_rays=%d\n", vol.ih.ic.num_rays);
    fprintf(cl_io, "num_bins=%d\n", vol.ih.tc.tri.num_bins_out);
    fprintf(cl_io, "range_bin0=%d\n", vol.ih.tc.tri.rng_1st_bin);
    fprintf(cl_io, "bin_step=%d\n", vol.ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * vol.ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = vol.ih.tc.tdi.prf;
    mp = vol.ih.tc.tdi.m_prf_mode;
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
    fprintf(cl_io, "prf=%.2lf\n", prf);
    fprintf(cl_io, "prf_mode=%s\n", mp_s);
    fprintf(cl_io, "vel_ua=%.3lf\n", vel_ua);
    return 1;
}

static int near_sweep_cb(int argc, char *argv[])
{
    char *ang_s;	/* Sweep angle, degrees, as given on command line */
    double ang, da;
    char *vol_nm;
    int i;
    struct Sigmet_Vol vol;
    int s, nrst;

    if ( argc != 3 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" angle sigmet_volume");
	return 0;
    }
    ang_s = argv[1];
    vol_nm = argv[2];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	Err_Append("Expected floating point value for sweep angle, got ");
	Err_Append(ang_s);
	Err_Append(". ");
	return 0;
    }
    ang *= RAD_PER_DEG;
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
	return 0;
    }
    vol = vols[i].vol;
    if ( !vol.sweep_angle ) {
	Err_Append("Sweep angles not loaded for ");
	Err_Append(vol_nm);
	Err_Append(". Please load the entire volume");
	return 0;
    }
    for (da = DBL_MAX, s = 0; s < vol.ih.tc.tni.num_sweeps; s++) {
	if ( fabs(vol.sweep_angle[s] - ang) < da ) {
	    da = fabs(vol.sweep_angle[s] - ang);
	    nrst = s;
	}
    }
    fprintf(cl_io, "%d\n", nrst);
    return 1;
}

static int ray_headers_cb(int argc, char *argv[])
{
    char *vol_nm;
    int i;
    struct Sigmet_Vol vol;
    int s, r;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
	    fprintf(cl_io, "sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		Err_Append("Bad ray time.  ");
		return 0;
	    }
	    fprintf(cl_io, "%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    fprintf(cl_io, "az %7.3f %7.3f | ",
		    vol.ray_az0[s][r] * DEG_PER_RAD,
		    vol.ray_az1[s][r] * DEG_PER_RAD);
	    fprintf(cl_io, "tilt %6.3f %6.3f\n",
		    vol.ray_tilt0[s][r] * DEG_PER_RAD,
		    vol.ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }
    return 1;
}

static int data_cb(int argc, char *argv[])
{
    char *vol_nm;
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
	   data	vol_nm		(argc = 2)
	   data y vol_nm	(argc = 3)
	   data y s vol_nm	(argc = 4)
	   data y s r vol_nm	(argc = 5)
	   data y s r b vol_nm	(argc = 6)
     */

    y = s = r = b = all;
    type = DB_ERROR;
    if (argc > 2 && (type = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc > 3 && sscanf(argv[2], "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc > 4 && sscanf(argv[3], "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc > 5 && sscanf(argv[4], "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc > 6) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin] sigmet_volume");
	return 0;
    }
    vol_nm = argv[argc - 1];
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
	    Err_Append("Data type ");
	    Err_Append(abbrv);
	    Err_Append(" not in volume.\n");
	    return 0;
	}
    }
    if (s != all && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r != all && r >= (int)vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b != all && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }

    /* Write */
    if (y == all && s == all && r == all && b == all) {
	for (y = 0; y < vol.num_types; y++) {
	    type = vol.types[y];
	    abbrv = Sigmet_DataType_Abbrv(type);
	    for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
		fprintf(cl_io, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(cl_io, "ray %d: ", r);
		    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
			if (Sigmet_IsData(d)) {
			    fprintf(cl_io, "%f ", d);
			} else {
			    fprintf(cl_io, "nodat ");
			}
		    }
		    fprintf(cl_io, "\n");
		}
	    }
	}
    } else if (s == all && r == all && b == all) {
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    fprintf(cl_io, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		fprintf(cl_io, "ray %d: ", r);
		for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		    if (Sigmet_IsData(d)) {
			fprintf(cl_io, "%f ", d);
		    } else {
			fprintf(cl_io, "nodat ");
		    }
		}
		fprintf(cl_io, "\n");
	    }
	}
    } else if (r == all && b == all) {
	fprintf(cl_io, "%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(cl_io, "ray %d: ", r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(cl_io, "%f ", d);
		} else {
		    fprintf(cl_io, "nodat ");
		}
	    }
	    fprintf(cl_io, "\n");
	}
    } else if (b == all) {
	if (vol.ray_ok[s][r]) {
	    fprintf(cl_io, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(cl_io, "%f ", d);
		} else {
		    fprintf(cl_io, "nodat ");
		}
	    }
	    fprintf(cl_io, "\n");
	}
    } else {
	if (vol.ray_ok[s][r]) {
	    fprintf(cl_io, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
	    if (Sigmet_IsData(d)) {
		fprintf(cl_io, "%f ", d);
	    } else {
		fprintf(cl_io, "nodat ");
	    }
	    fprintf(cl_io, "\n");
	}
    }
    return 1;
}

static int bin_outline_cb(int argc, char *argv[])
{
    struct Sigmet_Vol vol;
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sweep ray bin");
	return 0;
    }
    s_s = argv[1];
    r_s = argv[2];
    b_s = argv[3];

    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (sscanf(r_s, "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (sscanf(b_s, "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if ( !Sigmet_BinOutl(&vol, s, r, b, corners) ) {
	Err_Append("Could not compute bin outlines.  ");
	return 0;
    }
    c = (use_deg ? DEG_RAD : 1.0);
    fprintf(cl_io, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;
}

static int bintvls_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
    struct Sigmet_Vol vol;	/* Volume from global vols array */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int i;			/* Volume index. */
    int y, s, r, b;		/* Indeces: data type, sweep, ray, bin */
    double *bnds;		/* bounds[type_t] */
    unsigned char n_bnds;	/* n_bounds[type_t] */
    int n;			/* Index from bnds */
    double d;			/* Data value */

    /* Parse command line */
    if ( argc != 4 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep volume");
	return 0;
    }
    abbrv = argv[1];
    s_s = argv[2];
    vol_nm = argv[3];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
	return 0;
    }
    if ( !DataType_GetColors(abbrv, &n_bnds, NULL, &bnds) ) {
	Err_Append("Cannot put gates into data intervals for ");
	Err_Append(abbrv);
	Err_Append(". Bounds not set");
	return 0;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
	return 0;
    }

    /* Determine which interval from bounds each bin value is in and print. */
    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( vol.ray_ok[s][r] ) {
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
		if ( Sigmet_IsData(d)
			&& (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    fprintf(cl_io, "%6d: %3d %5d\n", n, r, b);
		}
	    }
	}
    }

    return 1;
}

/* Change radar longitude to given value, which must be given in degrees */
static int radar_lon_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
    int i;			/* Volume index. */
    char *lon_s;		/* New longitude, degrees, in argv */
    double lon;			/* New longitude, degrees */

    /* Parse command line */
    if ( argc != 3 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" new_lon volume");
	return 0;
    }
    lon_s = argv[1];
    vol_nm = argv[2];
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	Err_Append("Expected float value for new longitude, got ");
	Err_Append(lon_s);
	Err_Append(". ");
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
	return 0;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vols[i].vol.ih.ic.longitude = Sigmet_RadBin4(lon);

    return 1;
}

/* Change radar latitude to given value, which must be given in degrees */
static int radar_lat_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
    int i;			/* Volume index. */
    char *lat_s;		/* New latitude, degrees, in argv */
    double lat;			/* New latitude, degrees */

    /* Parse command line */
    if ( argc != 3 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" new_lat volume");
	return 0;
    }
    lat_s = argv[1];
    vol_nm = argv[2];
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	Err_Append("Expected float value for new latitude, got ");
	Err_Append(lat_s);
	Err_Append(". ");
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
	return 0;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vols[i].vol.ih.ic.latitude = Sigmet_RadBin4(lat);

    return 1;
}

/* Change ray azimuths to given value, which must be given in degrees */
static int shift_az_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
    int i;			/* Volume index. */
    char *daz_s;		/* Amount to add to each azimuth, deg, in argv */
    double daz;			/* Amount to add to each azimuth, radians */
    double idaz;		/* Amount to add to each azimuth, binary angle */
    int s, r;			/* Loop indeces */

    /* Parse command line */
    if ( argc != 3 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" dz volume");
	return 0;
    }
    daz_s = argv[1];
    vol_nm = argv[2];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	Err_Append("Expected float value for azimuth shift, got ");
	Err_Append(daz_s);
	Err_Append(". ");
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
static int proj_cb(int argc, char *argv[])
{
    projPJ t_pj;

    if ( !(t_pj = pj_init(argc - 1, argv + 1)) ) {
	Err_Append("Unknown projection.");
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
static int img_sz_cb(int argc, char *argv[])
{
    char *w_pxl_s;

    if ( argc == 1 ) {
	fprintf(cl_io, "%d\n", w_pxl);
	return 1;
    } else if ( argc == 2 ) {
	w_pxl_s = argv[1];
	if ( sscanf(w_pxl_s, "%d", &w_pxl) != 1 ) {
	    Err_Append("Expected integer value for display width, got ");
	    Err_Append(w_pxl_s);
	    Err_Append(". ");
	    return 0;
	}
	h_pxl = w_pxl;
	return 1;
    } else {
	Err_Append("Usage: img_sz width_pxl");
	return 0;
    }
}

/* Identify image generator */
static int img_app_cb(int argc, char *argv[])
{
    char *img_app_s;
    struct stat sbuf;
    mode_t m = S_IXUSR | S_IXGRP | S_IXOTH;	/* Executable mode */

    if ( argc != 2 ) {
	Err_Append("Usage: img_app");
	return 0;
    }
    img_app_s = argv[1];
    if ( stat(img_app_s, &sbuf) == -1 ) {
	Err_Append("Could not get information about ");
	Err_Append(img_app_s);
	Err_Append(". ");
	Err_Append(strerror(errno));
	return 0;
    }
    if ( ((sbuf.st_mode & S_IFREG) != S_IFREG) || ((sbuf.st_mode & m) != m) ) {
	Err_Append(img_app_s);
	Err_Append(" is not an executable file. ");
	return 0;
    }
    if (snprintf(img_app, LEN, "%s", img_app_s) > LEN) {
	Err_Append("Could not store name of image application. ");
	return 0;
    }
    return 1;
}

/* Specify image alpha channel */
static int alpha_cb(int argc, char *argv[])
{
    char *alpha_s;

    if ( argc != 2 ) {
	Err_Append("Usage: alpha alpha");
	return 0;
    }
    alpha_s = argv[1];
    if ( sscanf(alpha_s, "%lf", &alpha) != 1 ) {
	Err_Append("Expected float value for alpha, got ");
	Err_Append(alpha_s);
	Err_Append(". ");
	return 0;
    }
    return 1;
}

/*
   Print name of the image that img_cb would create for volume vol,
   type abbrv, sweep s to buf, which must have space for LEN characters,
   including nul.  If something goes wrong, fill buf with nul's and return
   0.
 */
static int img_name(struct Sigmet_Vol *vol_p, char *abbrv, int s, char *buf)
{
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */

    memset(buf, 0, LEN);
    if ( s >= vol_p->ih.ic.num_sweeps || !vol_p->sweep_ok[s]
	    || !Tm_JulToCal(vol_p->sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	return 0;
    }
    if ( snprintf(buf, LEN, "%s_%02d%02d%02d%02d%02d%02.0f_%s_%.1f",
		vol_p->ih.ic.hw_site_name, yr, mo, da, h, mi, sec,
		abbrv, vol_p->sweep_angle[s] * DEG_PER_RAD) > LEN ) {
	memset(buf, 0, LEN);
	return 0;
    }
    return 1;
}

/* Print the name of the image that img would create */
static int img_name_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
    struct Sigmet_Vol vol;	/* Volume from global vols array */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int i;
    int y, s;			/* Indeces: data type, sweep */
    char img_fl_nm[LEN];	/* Name of image file */

    /* Parse command line */
    if ( argc != 4 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep volume");
	return 0;
    }
    abbrv = argv[1];
    s_s = argv[2];
    vol_nm = argv[3];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
	return 0;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
	return 0;
    }

    /* Print name of output file */
    if ( !img_name(&vol, abbrv, s, img_fl_nm) ) {
	Err_Append("Could not make image file name. ");
	return 0;
    }
    fprintf(cl_io, "%s.png\n", img_fl_nm);

    return 1;
}

static int img_cb(int argc, char *argv[])
{
    char *vol_nm;		/* Sigmet raw file */
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
    char kml_fl_nm[LEN];	/* KML output file name */
    FILE *kml_fl;		/* KML file */
    pid_t img_pid = -1;		/* Process id for image generator */
    FILE *out = NULL;		/* Where to send outlines to draw */
    struct XDRX_Stream xout;	/* XDR stream for out */
    jmp_buf err_jmp;		/* Handle output errors with setjmp, longjmp */
    char *item = NULL;		/* Item being written. Needed for error message. */
    pid_t p;			/* Return from waitpid */
    int status;			/* Exit status of image generator */
    int pfd[2];			/* Pipe for data */
    char err_buf[LEN];		/* Store image process output */
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
	Err_Append("Sweep drawing application not set");
	return 0;
    }

    /* Parse command line */
    if ( argc != 4 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep volume");
	return 0;
    }
    abbrv = argv[1];
    s_s = argv[2];
    vol_nm = argv[3];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
	return 0;
    }
    if ( !DataType_GetColors(abbrv, &n_clrs, &clrs, &bnds) ) {
	Err_Append("Cannot put gates into data intervals for ");
	Err_Append(abbrv);
	Err_Append(". Colors and bounds not set");
	return 0;
    }
    n_bnds = n_clrs + 1;
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    i = get_vol_i(vol_nm);
    if ( i == -1 ) {
	Err_Append(vol_nm);
	Err_Append(" not loaded or was unloaded due to being truncated."
		" Please (re)load with read command. ");
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
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
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
	Err_Append("West edge of map undefined in this projection. ");
	return 0;
    }
    left = edge.u;

    /* Right (east) side */
    GeogStep(radar.u, radar.v, 90.0 * RAD_PER_DEG, ray_len, &east, &d);
    edge.u = east;
    edge.v = d;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	Err_Append("East edge of map undefined in this projection. ");
	return 0;
    }
    rght = edge.u;

    /* Bottom (south) side */
    GeogStep(radar.u, radar.v, 180.0 * RAD_PER_DEG, ray_len, &d, &south);
    edge.u = d;
    edge.v = south;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	Err_Append("West edge of map undefined in this projection. ");
	return 0;
    }
    btm = edge.v;

    /* Top (north) side */
    GeogStep(radar.u, radar.v, 0.0, ray_len, &d, &north);
    edge.u = d;
    edge.v = north;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	Err_Append("West edge of map undefined in this projection. ");
	return 0;
    }
    top = edge.v;

    west *= DEG_PER_RAD;
    east *= DEG_PER_RAD;
    south *= DEG_PER_RAD;
    north *= DEG_PER_RAD;
    px_per_m = w_pxl / (rght - left);

    /* Make name of output file */
    if ( !img_name(&vol, abbrv, s, base_nm) 
	    || snprintf(img_fl_nm, LEN, "%s.png", base_nm) > LEN ) {
	Err_Append("Could not make image file name. ");
	return 0;
    }

    /* Launch the external drawing application and create a pipe to it. */
    if ( pipe(pfd) == -1 ) {
	Err_Append(strerror(errno));
	Err_Append("\nCould not create pipes for sweep drawing application.  ");
	return 0;
    }
    img_pid = fork();
    switch (img_pid) {
	case -1:
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not spawn sweep drawing application.  ");
	    return 0;
	case 0:
	    /*
	       Child.  Close stdout.
	       Read polygons from stdin (read side of data pipe).
	     */
	    if ( close(cl_io_fd) == -1 ) {
		fprintf(stderr, "%s: %s child could not close"
			" server streams", img_app, time_stamp());
		_exit(EXIT_FAILURE);
	    }
	    if ( dup2(pfd[0], STDIN_FILENO) == -1 || close(pfd[1]) == -1 ) {
		fprintf(stderr, "%s: could not set up %s process",
			img_app, time_stamp());
		_exit(EXIT_FAILURE);
	    }
	    execl(img_app, img_app, (char *)NULL);
	    _exit(EXIT_FAILURE);
	default:
	    /* This process.  Send polygon to write side of pipe, a.k.a. out. */
	    if ( close(pfd[0]) == -1 || !(out = fdopen(pfd[1], "w"))) {
		Err_Append(strerror(errno));
		Err_Append("\nCould not write to drawing application.  ");
		img_pid = -1;
		return 0;
	    }
    }
    XDRX_StdIO_Create(&xout, out, XDR_ENCODE);

    /* Come back here if write to image generator fails */
    switch (setjmp(err_jmp)) {
	case XDRX_OK:
	    /* Initializing */
	    break;
	case XDRX_ERR:
	    /* Fail */
	    Err_Append("Could not write ");
	    Err_Append(item);
	    Err_Append("Could not write image data. ");
	    goto error;
	    break;
	case XDRX_EOF:
	    /* Not used */
	    break;
    }

    /* Send global information about the image to drawing process */
    img_fl_nm_l = strlen(img_fl_nm);
    item = " image file name. ";
    XDRX_Put_UInt(img_fl_nm_l, &xout, err_jmp);
    XDRX_Put_String(img_fl_nm, img_fl_nm_l, &xout, err_jmp);
    item = " image dimensions. ";
    XDRX_Put_UInt(w_pxl, &xout, err_jmp);
    XDRX_Put_UInt(h_pxl, &xout, err_jmp);
    item = " image real bounds. ";
    XDRX_Put_Double(left, &xout, err_jmp);
    XDRX_Put_Double(rght, &xout, err_jmp);
    XDRX_Put_Double(top, &xout, err_jmp);
    XDRX_Put_Double(btm, &xout, err_jmp);
    item = " alpha channel value. ";
    XDRX_Put_Double(alpha, &xout, err_jmp);
    item = " colors. ";
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

		    item = " polygon color index. ";
		    XDRX_Put_UInt(n, &xout, err_jmp);
		    item = " polygon point count. ";
		    XDRX_Put_UInt(npts, &xout, err_jmp);
		    item = " bin corner coordinates. ";
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
    fclose(out);
    out = NULL;
    p = waitpid(img_pid, &status, 0);
    if ( p == img_pid ) {
	if ( WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE ) {
	    Err_Append("Image process failed for ");
	    Err_Append(img_fl_nm);
	    Err_Append("See ");
	    Err_Append(SIGMET_RAWD_ERR);
	    Err_Append(" for possible error messages. ");
	    goto error;
	} else if ( WIFSIGNALED(status) ) {
	    if ( snprintf(err_buf, LEN, "Image process exited on signal %d. ",
			WTERMSIG(status)) <= LEN ) {
		Err_Append(err_buf);
	    } else {
		Err_Append("Image process exited on signal. ");
	    }
	}
    } else {
	fprintf(stderr, "%s: could not get exit status for image generator "
		"while processing %s. %s. Continuing anyway.\n",
		time_stamp(), img_fl_nm,
		(p == -1) ? strerror(errno) : "Unknown error.");
    }

    /* Make kml file and return */
    if ( snprintf(kml_fl_nm, LEN, "%s.kml", base_nm) > LEN ) {
	Err_Append("Could not make kml file name. ");
	goto error;
    }
    if ( !(kml_fl = fopen(kml_fl_nm, "w")) ) {
	Err_Append("Could not open ");
	Err_Append(kml_fl_nm);
	Err_Append(" for output. ");
	goto error;
    }
    if ( s >= vol.ih.ic.num_sweeps || !vol.sweep_ok[s]
	    || !Tm_JulToCal(vol.sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	goto error;
    }
    fprintf(kml_fl, kml_tmpl,
	    vol.ih.ic.hw_site_name, vol.ih.ic.hw_site_name,
	    yr, mo, da, h, mi, sec,
	    abbrv, vol.sweep_angle[s] * DEG_PER_RAD,
	    img_fl_nm,
	    north, south, west, east);
    fclose(kml_fl);

    fprintf(cl_io, "%s\n", img_fl_nm);
    return 1;
error:
    if (out) {
	fclose(out);
    }
    if ( img_pid != -1 ) {
	kill(img_pid, SIGTERM);
	img_pid = -1;
    }
    return 0;
}

static int stop_cb(int argc, char *argv[])
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
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting on termination signal    \n", 35);
	    break;
	case SIGBUS:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting on bus error             \n", 35);
	    break;
	case SIGFPE:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting arithmetic exception     \n", 35);
	    break;
	case SIGILL:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting illegal instruction      \n", 35);
	    break;
	case SIGSEGV:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting invalid memory reference \n", 35);
	    break;
	case SIGSYS:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting on bad system call       \n", 35);
	    break;
	case SIGXCPU:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting: CPU time limit exceeded \n", 35);
	    break;
	case SIGXFSZ:
	    write(STDERR_FILENO, cmd, cmd_len);
	    write(STDERR_FILENO, " exiting: file size limit exceeded\n", 35);
	    break;
    }
    _exit(EXIT_FAILURE);
}
