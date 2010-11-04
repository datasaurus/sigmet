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
 .	$Revision: 1.295 $ $Date: 2010/11/02 21:40:41 $
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

/* Where to put output and error messages */
#define SIGMET_RAWD_LOG "sigmet.log"
#define SIGMET_RAWD_ERR "sigmet.err"

/* Size for various strings */
#define LEN 4096

/* Subcommands */
#define NCMD 25
typedef enum Sigmet_CB_Return (callback)(int , char **, char *, int, FILE *,
	int, FILE *);
static callback pid_cb;
static callback types_cb;
static callback setcolors_cb;
static callback good_cb;
static callback list_cb;
static callback keep_cb;
static callback release_cb;
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
static callback dorade_cb;
static char *cmd1v[NCMD] = {
    "pid", "types", "colors", "good", "list", "keep", "release",
    "flush", "volume_headers", "vol_hdr", "near_sweep", "ray_headers",
    "data", "bin_outline", "bintvls", "radar_lon", "radar_lat", "shift_az",
    "proj", "img_app", "img_sz", "alpha", "img_name", "img", "dorade"
};
static callback *cb1v[NCMD] = {
    pid_cb, types_cb, setcolors_cb, good_cb, list_cb, keep_cb, release_cb,
    flush_cb, volume_headers_cb, vol_hdr_cb, near_sweep_cb, ray_headers_cb,
    data_cb, bin_outline_cb, bintvls_cb, radar_lon_cb, radar_lat_cb, shift_az_cb,
    proj_cb, img_app_cb, img_sz_cb, alpha_cb, img_name_cb, img_cb, dorade_cb
};

/* Convenience functions */
static char *time_stamp(void);
static int abs_name(char *, char *, char *, size_t);
static int img_name(struct Sigmet_Vol *, char *, int, char *);

/* Signal handling functions */
static int handle_signals(void);
static void handler(int);

/* Abbreviations */
#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

static char *ddir;		/* Working directory for daemon */

int main(int argc, char *argv[])
{
    char *argv0;		/* Name of this daemon */
    pid_t pid = getpid();
    int flags;			/* Flags for log files */
    mode_t mode;		/* Mode for files */
    struct sockaddr_un sa;	/* Socket to read command and return exit status */
    struct sockaddr *sa_p;	/* &sa or &d_err_sa, for call to bind */
    int i_dmn;			/* File descriptors for daemon socket */
    int y;			/* Loop index */
    int cl_io_fd;		/* File descriptor to read client command
				   and send results */
    pid_t client_pid = -1;	/* Client process id */
    char *cl_wd = NULL;		/* Client working directory */
    size_t cl_wd_l;		/* strlen(cl_wd) */
    size_t cl_wd_lx = 0;	/* Allocation at cl_wd */
    char *cmd_ln = NULL;	/* Command line from client */
    size_t cmd_ln_l;		/* strlen(cmd_ln) */
    size_t cmd_ln_lx = 0;	/* Given size of command line */
    int i;

    argv0 = argv[0];
    pid = getpid();

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.",
		argv0, pid);
	exit(EXIT_FAILURE);
    }

    /* Usage: sigmet_rawd */
    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	exit(EXIT_FAILURE);
    }

    /* Initialize volume table */
    SigmetRaw_VolInit();

    /* Initialize other variables */
    if ( !SigmetRaw_ProjInit() ) {
	fprintf(stderr, "%s (%d): could not set default projection.\n",
		argv0, pid);
	exit(EXIT_FAILURE);
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	enum DataType_Return status;

	status = DataType_Add(Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
	if ( status != DATATYPE_SUCCESS ) {
	    fprintf(stderr, "%s (%d): could not register data type %s\n%s\n",
		    argv0, pid, Sigmet_DataType_Abbrv(y), Err_Get());
	    exit(EXIT_FAILURE);
	}
    }

    /* Identify and go to working directory */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s (%d): SIGMET_RAWD_DIR not set.\n", argv0, pid);
	exit(EXIT_FAILURE);
    }
    mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
    if ( mkdir(ddir, mode) == -1 && errno != EEXIST) {
	perror("Could not create daemon working directory.");
	exit(EXIT_FAILURE);
    }
    if ( chdir(ddir) == -1 ) {
	perror("Could not set daemon working directory.");
	exit(EXIT_FAILURE);
    }

    /* Create socket to communicate with clients */
    if ( access(SIGMET_RAWD_IN, F_OK) == 0 ) {
	fprintf(stderr, "%s (%d): daemon socket %s already exists. "
		"Is daemon already running?\n", argv0, pid, SIGMET_RAWD_IN);
	exit(EXIT_FAILURE);
    }
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s", SIGMET_RAWD_IN) >= SA_PLEN ) {
	fprintf(stderr, "%s (%d): could not fit %s into socket address path.\n",
		argv0, pid, SIGMET_RAWD_IN);
	goto error;
    }
    sa_p = (struct sockaddr *)&sa;
    if ((i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1
	    || bind(i_dmn, sa_p, SA_UN_SZ) == -1
	    || listen(i_dmn, SOMAXCONN) == -1) {
	fprintf(stderr, "%s (%d): could not create io socket.\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }

    /* Set up log and error output. */
    flags = O_CREAT | O_TRUNC | O_WRONLY;
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    if ( (i = open(SIGMET_RAWD_LOG, flags, mode)) == -1
	    || dup2(i, STDOUT_FILENO) == -1 || close(i) == -1 ) {
	fprintf(stderr, "%s (%d): could not open log file.", argv0, pid);
	goto error;
    }
    if ( (i = open(SIGMET_RAWD_ERR, flags, mode)) == -1
	    || dup2(i, STDERR_FILENO) == -1 || close(i) == -1 ) {
	fprintf(stderr, "%s (%d): could not open error file.", argv0, pid);
	goto error;
    }
    fclose(stdin);

    printf("%s: sigmet_rawd daemon starting.\nProcess id = %d.\n"
	    "Socket = %s/%s\n", time_stamp(), pid, ddir, sa.sun_path);
    fflush(stdout);

    /* Wait for clients */
    while ( (cl_io_fd = accept(i_dmn, NULL, 0)) != -1 ) {
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
	enum Sigmet_CB_Return sstatus; /* Result of callback */
	int i;			/* Loop index */
	char *c, *e;		/* Loop parameters */
	void *t;		/* Hold return from realloc */
	int stop = 0;		/* If true, exit program */

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
			"%lu bytes for process %ld.\n",
			time_stamp(), (unsigned long)cl_wd_l, (long)client_pid);
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
			"%lu bytes for process %ld.\n",
			time_stamp(), (unsigned long)cmd_ln_l, (long)client_pid);
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
	if ( snprintf(out_nm, LEN, "%d.1", client_pid) >= LEN ) {
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
	if ( snprintf(err_nm, LEN, "%d.2", client_pid) >= LEN ) {
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
	if ( strcmp(cmd1, "stop") == 0 ) {
	    /* Request that daemon stop. */
	    sstatus = SIGMET_CB_SUCCESS;
	    stop = 1;
	} else if ( (i = SigmetRaw_Cmd(cmd1)) == -1) {
	    /* No command. Make error message. */
	    sstatus = SIGMET_CB_FAIL;
	    fprintf(err, "No option or subcommand named %s. "
		    "Subcommand must be one of: ", cmd1);
	    for (i = 0; i < NCMD; i++) {
		fprintf(err, "%s ", cmd1v[i]);
	    }
	    fprintf(err, "\n");
	} else {
	    /* Found command. Run it. */
	    sstatus = (cb1v[i])(argc1, argv1, cl_wd, i_out, out, i_err, err);
	    if ( sstatus != SIGMET_CB_SUCCESS ) {
		fprintf(err, "%s\n", Err_Get());
	    }
	}

	/* Send result and clean up */
	if ( fclose(out) == EOF || fclose(err) == EOF ) {
	    fprintf(stderr, "%s: could not close client error streams "
		    "for process %d\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	}
	if ( write(cl_io_fd, &sstatus, sizeof(enum Sigmet_CB_Return)) == -1 ) {
	    fprintf(err, "%s: could not send return code for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
	if ( close(cl_io_fd) == -1 ) {
	    fprintf(err, "%s: could not close socket for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}

	if (stop) {
	    break;
	}
    }

    /* No longer waiting for clients */
    unlink(SIGMET_RAWD_IN);
    SigmetRaw_VolFree();
    for (y = 0; y < SIGMET_NTYPES; y++) {
	DataType_Rm(Sigmet_DataType_Abbrv(y));
    }
    FREE(cl_wd);
    FREE(cmd_ln);
    fprintf(stderr, "%s: exiting.\n", time_stamp());
    exit(EXIT_SUCCESS);

error:
    unlink(SIGMET_RAWD_IN);
    SigmetRaw_VolFree();
    for (y = 0; y < SIGMET_NTYPES; y++) {
	DataType_Rm(Sigmet_DataType_Abbrv(y));
    }
    fprintf(stderr, "%s: sigmet_rawd failed.\n", time_stamp());
    exit(EXIT_FAILURE);
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
    if ( status >= l ) {
	Err_Append("File name too long.\n");
	return 0;
    } else if ( status == -1 ) {
	Err_Append(strerror(errno));
	return 0;
    }
    return 1;
}

static enum Sigmet_CB_Return pid_cb(int argc, char *argv[], char *cl_wd, int i_out,
	FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if (argc != 2) {
	fprintf(err, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    fprintf(out, "%d\n", getpid());
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return types_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;

    if (argc != 2) {
	fprintf(err, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	fprintf(out, "%s | %s\n",
		Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return setcolors_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;		/* Data type abbreviation */
    char *clr_fl_nm;		/* File with colors */
    int status;

    /* Parse command line */
    if (argc != 4) {
	fprintf(err, "Usage: %s %s type colors_file\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    abbrv = argv[2];
    clr_fl_nm = argv[3];
    status = DataType_SetColors(abbrv, clr_fl_nm);
    if ( status != DATATYPE_SUCCESS ) {
	fprintf(err, "%s\n", Err_Get());
    }
    switch (status) {
	case DATATYPE_SUCCESS:
	    return SIGMET_CB_SUCCESS;
	case DATATYPE_INPUT_FAIL:
	    return SIGMET_CB_INPUT_FAIL;
	case DATATYPE_MEM_FAIL:
	    return SIGMET_CB_MEM_FAIL;
	case DATATYPE_FAIL:
	    return SIGMET_CB_FAIL;
    }
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return good_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Could not assess %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    return SigmetRaw_GoodVol(vol_nm, i_err, err);
}

static enum Sigmet_CB_Return list_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    SigmetRaw_VolList(out);
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return keep_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    SigmetRaw_Keep(vol_nm);
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return release_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    SigmetRaw_Release(vol_nm);
    return SIGMET_CB_SUCCESS;
}

/* This command removes unused volumes */
static enum Sigmet_CB_Return flush_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    return SigmetRaw_Flush() ? SIGMET_CB_SUCCESS : SIGMET_CB_FAIL;
}

static enum Sigmet_CB_Return volume_headers_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadHdr */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadHdr(vol_nm, err, i_err, &vol_p);
    if ( status == SIGMET_CB_SUCCESS ) {
	Sigmet_PrintHdr(out, vol_p);
    }
    return status;
}

static enum Sigmet_CB_Return vol_hdr_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadHdr */
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadHdr(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    fprintf(out, "site_name=\"%s\"\n", vol_p->ih.ic.su_site_name);
    fprintf(out, "radar_lon=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD);
    fprintf(out, "radar_lat=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.latitude), 0.0) * DEG_PER_RAD);
    fprintf(out, "task_name=\"%s\"\n", vol_p->ph.pc.task_name);
    fprintf(out, "types=\"");
    fprintf(out, "%s", Sigmet_DataType_Abbrv(vol_p->types[0]));
    for (y = 1; y < vol_p->num_types; y++) {
	fprintf(out, " %s", Sigmet_DataType_Abbrv(vol_p->types[y]));
    }
    fprintf(out, "\"\n");
    fprintf(out, "num_sweeps=%d\n", vol_p->ih.ic.num_sweeps);
    fprintf(out, "num_rays=%d\n", vol_p->ih.ic.num_rays);
    fprintf(out, "num_bins=%d\n", vol_p->ih.tc.tri.num_bins_out);
    fprintf(out, "range_bin0=%d\n", vol_p->ih.tc.tri.rng_1st_bin);
    fprintf(out, "bin_step=%d\n", vol_p->ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * vol_p->ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = vol_p->ih.tc.tdi.prf;
    mp = vol_p->ih.tc.tdi.m_prf_mode;
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
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return near_sweep_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;		/* Sweep angle, degrees, from command line */
    double ang, da;
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    int s, nrst;

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s angle sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    ang_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(err, "%s %s: expected floating point for sweep angle, got %s\n",
		argv0, argv1, ang_s);
	return SIGMET_CB_FAIL;
    }
    ang *= RAD_PER_DEG;
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    if ( !vol_p->sweep_angle ) {
	fprintf(err, "%s %s: sweep angles not loaded for %s. "
		"Is volume truncated?.\n", argv0, argv1, vol_nm);
	return SIGMET_CB_FAIL;
    }
    nrst = -1;
    for (da = DBL_MAX, s = 0; s < vol_p->num_sweeps_ax; s++) {
	if ( fabs(vol_p->sweep_angle[s] - ang) < da ) {
	    da = fabs(vol_p->sweep_angle[s] - ang);
	    nrst = s;
	}
    }
    fprintf(out, "%d\n", nrst);
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return ray_headers_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    int s, r;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[2];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    if ( vol_p->truncated ) {
	fprintf(err, "%s %s: %s is truncated.\n",
		argv0, argv1, vol_nm);
	return SIGMET_CB_FAIL;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
	    int yr, mon, da, hr, min;
	    double sec;

	    if ( !vol_p->ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(out, "sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol_p->ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		fprintf(err, "%s %s: bad ray time\n%s\n", argv0, argv1, Err_Get());
		return SIGMET_CB_FAIL;
	    }
	    fprintf(out, "%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    fprintf(out, "az %7.3f %7.3f | ",
		    vol_p->ray_az0[s][r] * DEG_PER_RAD,
		    vol_p->ray_az1[s][r] * DEG_PER_RAD);
	    fprintf(out, "tilt %6.3f %6.3f\n",
		    vol_p->ray_tilt0[s][r] * DEG_PER_RAD,
		    vol_p->ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return data_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    int s, y, r, b;
    char *abbrv;
    double d;
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
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, argv[2]);
	return SIGMET_CB_FAIL;
    }
    if (argc > 4 && sscanf(argv[3], "%d", &s) != 1) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_CB_FAIL;
    }
    if (argc > 5 && sscanf(argv[4], "%d", &r) != 1) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return SIGMET_CB_FAIL;
    }
    if (argc > 6 && sscanf(argv[5], "%d", &b) != 1) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return SIGMET_CB_FAIL;
    }
    if (argc > 7) {
	fprintf(err, "Usage: %s %s [type] [sweep] [ray] sigmet_volume\n",
		argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    vol_nm_r = argv[argc - 1];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }

    if (type != DB_ERROR) {
	/*
	   User has specified a data type.  Search for it in the volume,
	   and set y to the specified type (instead of all).
	 */
	abbrv = Sigmet_DataType_Abbrv(type);
	for (y = 0; y < vol_p->num_types; y++) {
	    if (type == vol_p->types[y]) {
		break;
	    }
	}
	if (y == vol_p->num_types) {
	    fprintf(err, "%s %s: data type %s not in %s\n",
		    argv0, argv1, abbrv, vol_nm);
	    return SIGMET_CB_FAIL;
	}
    }
    if ( s != all && s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if (r != all && r >= (int)vol_p->ih.ic.num_rays) {
	fprintf(err, "%s %s: ray index %d out of range for %s\n",
		argv0, argv1, r, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if (b != all && b >= vol_p->ih.tc.tri.num_bins_out) {
	fprintf(err, "%s %s: bin index %d out of range for %s\n",
		argv0, argv1, b, vol_nm);
	return SIGMET_CB_FAIL;
    }

    /* Write */
    if (y == all && s == all && r == all && b == all) {
	for (y = 0; y < vol_p->num_types; y++) {
	    type = vol_p->types[y];
	    abbrv = Sigmet_DataType_Abbrv(type);
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		fprintf(out, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		    if ( !vol_p->ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(out, "ray %d: ", r);
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
			d = Sigmet_VolDat(vol_p, y, s, r, b);
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
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    fprintf(out, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		    if ( !vol_p->ray_ok[s][r] ) {
			continue;
		    }
		fprintf(out, "ray %d: ", r);
		for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		    d = Sigmet_VolDat(vol_p, y, s, r, b);
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
	for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	    if ( !vol_p->ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(out, "ray %d: ", r);
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_VolDat(vol_p, y, s, r, b);
		if (Sigmet_IsData(d)) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else if (b == all) {
	if (vol_p->ray_ok[s][r]) {
	    fprintf(out, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_VolDat(vol_p, y, s, r, b);
		if (Sigmet_IsData(d)) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else {
	if (vol_p->ray_ok[s][r]) {
	    fprintf(out, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_VolDat(vol_p, y, s, r, b);
	    if (Sigmet_IsData(d)) {
		fprintf(out, "%f ", d);
	    } else {
		fprintf(out, "nodat ");
	    }
	    fprintf(out, "\n");
	}
    }
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return bin_outline_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    char *s_s, *r_s, *b_s, *u_s;
    int use_rad = 1;		/* If true, use radians */
    int use_deg = 0;		/* If true, use degrees */
    int s, r, b;
    double corners[8];
    double c;

    if (argc != 7) {
	fprintf(err, "Usage: %s %s sweep ray bin unit sigmet_volume\n",
		argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];
    u_s = argv[5];
    vol_nm_r = argv[6];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }

    if (sscanf(s_s, "%d", &s) != 1) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_CB_FAIL;
    }
    if (sscanf(r_s, "%d", &r) != 1) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return SIGMET_CB_FAIL;
    }
    if (sscanf(b_s, "%d", &b) != 1) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return SIGMET_CB_FAIL;
    }
    if ( strcmp(u_s, "deg") == 0 || strcmp(u_s, "degree") == 0 ) {
	use_rad = 0;
	use_deg = 1;
    } else if ( strcmp(u_s, "rad") == 0 || strcmp(u_s, "rad") == 0 ) {
	use_rad = 1;
	use_deg = 0;
    } else {
	fprintf(err, "Unknown angle unit %s. Angle unit must be \"degree\" "
		"or \"radian\"\n", u_s);
	return SIGMET_CB_FAIL;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if (r >= vol_p->ih.ic.num_rays) {
	fprintf(err, "%s %s: ray index %d out of range for %s\n",
		argv0, argv1, r, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if (b >= vol_p->ih.tc.tri.num_bins_out) {
	fprintf(err, "%s %s: bin index %d out of range for %s\n",
		argv0, argv1, b, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( !Sigmet_BinOutl(vol_p, s, r, b, corners) ) {
	fprintf(err, "%s %s: could not compute bin outlines for bin %d %d %d in "
		"%s\n%s\n", argv0, argv1, s, r, b, vol_nm, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ( use_deg ) {
	c = DEG_RAD;
    } else if ( use_rad ) {
	c = 1.0;
    }
    fprintf(out, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return bintvls_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int y, s, r, b;		/* Indeces: data type, sweep, ray, bin */
    unsigned char n_clrs;	/* number of colors for the data type */
    double *bnds;		/* bounds[type_t] */
    unsigned char n_bnds;	/* number of bounds = n_clrs + 1 */
    int n;			/* Index from bnds */
    double d;			/* Data value */

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_CB_FAIL;
    }
    if ( DataType_GetColors(abbrv, &n_clrs, NULL, &bnds) != DATATYPE_SUCCESS ) {
	fprintf(err, "%s %s: cannot get data intervals for %s\n",
		argv0, argv1, abbrv);
	return SIGMET_CB_FAIL;
    }
    n_bnds = n_clrs + 1;
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }

    /* Make sure volume, type, and sweep are valid */
    for (y = 0; y < vol_p->num_types; y++) {
	if (type_t == vol_p->types[y]) {
	    break;
	}
    }
    if (y == vol_p->num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( !vol_p->sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }

    /* Determine which interval from bounds each bin value is in and print. */
    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	if ( vol_p->ray_ok[s][r] ) {
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_VolDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d)
			&& (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    fprintf(out, "%6d: %3d %5d\n", n, r, b);
		}
	    }
	}
    }

    return SIGMET_CB_SUCCESS;
}

/* Change radar longitude to given value, which must be given in degrees */
static enum Sigmet_CB_Return radar_lon_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadHdr */
    char *lon_s;		/* New longitude, degrees, in argv */
    double lon;			/* New longitude, degrees */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s new_lon sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    lon_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for new longitude, "
		"got %s\n", argv0, argv1, lon_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadHdr(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.longitude = Sigmet_RadBin4(lon);

    return SIGMET_CB_SUCCESS;
}

/* Change radar latitude to given value, which must be given in degrees */
static enum Sigmet_CB_Return radar_lat_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadHdr */
    char *lat_s;		/* New latitude, degrees, in argv */
    double lat;			/* New latitude, degrees */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s new_lat sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    lat_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for new latitude, "
		"got %s\n", argv0, argv1, lat_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadHdr(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.latitude = Sigmet_RadBin4(lat);

    return SIGMET_CB_SUCCESS;
}

/* Change ray azimuths to given value, which must be given in degrees */
static enum Sigmet_CB_Return shift_az_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    char *daz_s;		/* Amount to add to each azimuth, deg, in argv */
    double daz;			/* Amount to add to each azimuth, radians */
    double idaz;		/* Amount to add to each azimuth, binary angle */
    int s, r;			/* Loop indeces */

    /* Parse command line */
    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s dz sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    daz_s = argv[2];
    vol_nm_r = argv[3];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(err, "%s %s: expected float value for azimuth shift, got %s\n",
		argv0, argv1, daz_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    idaz = Sigmet_RadBin4(daz);
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.rhi_info.az[s] += idaz;
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.ppi_info.left_az += idaz;
		vol_p->ih.tc.tni.scan_info.ppi_info.right_az += idaz;
	    }
	    break;
	case FILE_SCAN:
	    vol_p->ih.tc.tni.scan_info.file_info.az0 += idaz;
	case MAN_SCAN:
	    break;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
	    vol_p->ray_az0[s][r]
		= GeogLonR(vol_p->ray_az0[s][r] + daz, 180.0 * RAD_PER_DEG);
	    vol_p->ray_az1[s][r]
		= GeogLonR(vol_p->ray_az1[s][r] + daz, 180.0 * RAD_PER_DEG);
	}
    }
    SigmetRaw_Keep(vol_nm);
    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return proj_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( !(SigmetRaw_SetProj(argc - 2, argv + 2)) ) {
	fprintf(err, "%s %s: could not set projection\n%s\n",
		argv0, argv1, Err_Get());
	return SIGMET_CB_FAIL;
    }
    return SIGMET_CB_SUCCESS;
}

/* Specify image width in pixels */
static enum Sigmet_CB_Return img_sz_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    unsigned w_pxl, h_pxl;

    if ( argc == 2 ) {
	SigmetRaw_GetImgSz(&w_pxl, &h_pxl);
	fprintf(out, "%u %u\n", w_pxl, h_pxl);
	return SIGMET_CB_SUCCESS;
    } else if ( argc == 3 ) {
	char *w_pxl_s = argv[2];

	if ( sscanf(w_pxl_s, "%u", &w_pxl) != 1 ) {
	    fprintf(err, "%s %s: expected integer for display width, got %s\n",
		    argv0, argv1, w_pxl_s);
	    return SIGMET_CB_FAIL;
	}
	SigmetRaw_SetImgSz(w_pxl, w_pxl);
	return SIGMET_CB_SUCCESS;
    } else if ( argc == 4 ) {
	char *w_pxl_s = argv[2];
	char *h_pxl_s = argv[3];

	if ( sscanf(w_pxl_s, "%u", &w_pxl) != 1 ) {
	    fprintf(err, "%s %s: expected integer for display width, got %s\n",
		    argv0, argv1, w_pxl_s);
	    return SIGMET_CB_FAIL;
	}
	if ( sscanf(h_pxl_s, "%u", &h_pxl) != 1 ) {
	    fprintf(err, "%s %s: expected integer for display height, got %s\n",
		    argv0, argv1, h_pxl_s);
	    return SIGMET_CB_FAIL;
	}
	SigmetRaw_SetImgSz(w_pxl, h_pxl);
	return SIGMET_CB_SUCCESS;
    } else {
	fprintf(err, "Usage: %s %s [width_pxl] [height_pxl]\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
}

/* Identify image generator */
static enum Sigmet_CB_Return img_app_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *img_app_s;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s img_app\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    img_app_s = argv[2];
    if ( !SigmetRaw_SetImgApp(img_app_s) ) {
	fprintf(err, "%s %s: Could not set image application to %s.\n%s\n",
		argv0, argv1, img_app_s, Err_Get());
	return SIGMET_CB_FAIL;
    }
    return SIGMET_CB_SUCCESS;
}

/* Specify image alpha channel */
static enum Sigmet_CB_Return alpha_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *alpha_s;
    double alpha;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s value\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    alpha_s = argv[2];
    if ( sscanf(alpha_s, "%lf", &alpha) != 1 ) {
	fprintf(err, "%s %s: expected float value for alpha value, got %s\n",
		argv0, argv1, alpha_s);
	return SIGMET_CB_FAIL;
    }
    SigmetRaw_SetImgAlpha(alpha);
    return SIGMET_CB_SUCCESS;
}

/*
   Print name of the image that img_cb would create for volume vol_p,
   type abbrv, sweep s to buf, which must have space for LEN characters,
   including nul.  Returned name has no suffix (e.g. "png" or "kml").
   If something goes wrong, fill buf with nul's and return 0.
 */
static int img_name(struct Sigmet_Vol *vol_p, char *abbrv, int s, char *buf)
{
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */

    memset(buf, 0, LEN);
    if ( s >= vol_p->ih.ic.num_sweeps || !vol_p->sweep_ok[s]
	    || !Tm_JulToCal(vol_p->sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	Err_Append("Invalid sweep. ");
	return 0;
    }
    if ( snprintf(buf, LEN, "%s_%02d%02d%02d%02d%02d%02.0f_%s_%.1f",
		vol_p->ih.ic.hw_site_name, yr, mo, da, h, mi, sec,
		abbrv, vol_p->sweep_angle[s] * DEG_PER_RAD) >= LEN ) {
	memset(buf, 0, LEN);
	Err_Append("Image file name too long. ");
	return 0;
    }
    return 1;
}

/* Print the name of the image that img would create */
static enum Sigmet_CB_Return img_name_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadHdr */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
    int y, s;			/* Indeces: data type, sweep */
    char img_fl_nm[LEN];	/* Name of image file */

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_CB_FAIL;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadHdr(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }

    /* Make sure volume, type, and sweep are valid */
    for (y = 0; y < vol_p->num_types; y++) {
	if (type_t == vol_p->types[y]) {
	    break;
	}
    }
    if (y == vol_p->num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( !vol_p->sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }

    /* Print name of output file */
    if ( !img_name(vol_p, abbrv, s, img_fl_nm) ) {
	fprintf(err, "%s %s: could not make image file name\n%s\n",
		argv0, argv1, Err_Get());
	return SIGMET_CB_FAIL;
    }
    fprintf(out, "%s.png\n", img_fl_nm);

    return SIGMET_CB_SUCCESS;
}

static enum Sigmet_CB_Return img_cb(int argc, char *argv[], char *cl_wd, int i_out,
	FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    struct Sigmet_Vol *vol_p;	/* Volume structure */
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    char *s_s;			/* Sweep index, as a string */
    char *abbrv;		/* Data type abbreviation */
    enum Sigmet_DataType type_t;/* Sigmet data type enumerator. See sigmet (3) */
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
    projPJ pj;			/* Geographic projection */
    projUV cnrs_uv[4];		/* Corners of a gate, lon-lat or x-y */
    projUV *uv;			/* Element from cnrs_uv */
    int n;			/* Loop index */
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */
    char base_nm[LEN]; 		/* Output file name */
    unsigned w_pxl, h_pxl;	/* Width and height of image, in display units */
    double alpha;		/* Image alpha channel */
    char img_fl_nm[LEN]; 	/* Image output file name */
    size_t img_fl_nm_l;		/* strlen(img_fl_nm) */
    int flags;                  /* Image file creation flags */
    mode_t mode;                /* Image file permissions */
    int i_img_fl;		/* Image file descriptor, not used */
    char *img_app;		/* External application to draw image */
    char kml_fl_nm[LEN];	/* KML output file name */
    FILE *kml_fl;		/* KML file */
    pid_t img_pid = -1;		/* Process id for image generator */
    FILE *img_out = NULL;	/* Where to send outlines to draw */
    struct XDRX_Stream xout;	/* XDR stream for img_out */
    jmp_buf err_jmp;		/* Handle output errors with setjmp, longjmp */
    char *item = NULL;		/* Item being written. Needed for error message. */
    pid_t p;			/* Return from waitpid */
    int si;			/* Exit status of image generator */
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

    if ( !(pj = SigmetRaw_GetProj()) ) {
	fprintf(err, "%s %s: geographic projection not set\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    alpha = SigmetRaw_GetImgAlpha();
    img_app = SigmetRaw_GetImgApp();
    if ( !img_app || strlen(img_app) == 0 ) {
	fprintf(err, "%s %s: sweep drawing application not set\n",
		argv0, argv1);
	return SIGMET_CB_FAIL;
    }

    /* Parse command line */
    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s type sweep sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    abbrv = argv[2];
    s_s = argv[3];
    vol_nm_r = argv[4];
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_CB_FAIL;
    }
    if ( DataType_GetColors(abbrv, &n_clrs, &clrs, &bnds) != DATATYPE_SUCCESS
	    || n_clrs == 0 || !clrs || !bnds ) {
	fprintf(err, "%s %s: colors and bounds not set for %s\n",
		argv0, argv1, abbrv);
	return SIGMET_CB_FAIL;
    }
    n_bnds = n_clrs + 1;
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }

    /* Make sure volume, type, and sweep are valid */
    for (y = 0; y < vol_p->num_types; y++) {
	if (type_t == vol_p->types[y]) {
	    break;
	}
    }
    if (y == vol_p->num_types) {
	fprintf(err, "%s %s: data type %s not in %s\n",
		argv0, argv1, abbrv, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( !vol_p->sweep_ok[s] ) {
	fprintf(err, "%s %s: sweep %d not valid in %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( !Tm_JulToCal(vol_p->sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	fprintf(err, "%s %s: invalid sweep time\n%s\n", argv0, argv1, Err_Get());
	goto error;
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

    radar.u = Sigmet_Bin4Rad(vol_p->ih.ic.longitude);
    radar.v = Sigmet_Bin4Rad(vol_p->ih.ic.latitude);
    ray_len = 0.01 * (vol_p->ih.tc.tri.rng_1st_bin
	    + (vol_p->ih.tc.tri.num_bins_out + 1) * vol_p->ih.tc.tri.step_out);
    ray_len = ray_len * M_PI / (180.0 * 60.0 * 1852.0);

    /* Left (west) side */
    GeogStep(radar.u, radar.v, 270.0 * RAD_PER_DEG, ray_len, &west, &d);
    edge.u = west;
    edge.v = d;
    edge = pj_fwd(edge, pj);
    if ( edge.u == HUGE_VAL || edge.v == HUGE_VAL ) {
	fprintf(err, "%s %s: west edge of map not defined in current projection\n",
		argv0, argv1);
	return SIGMET_CB_FAIL;
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
	return SIGMET_CB_FAIL;
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
	return SIGMET_CB_FAIL;
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
	return SIGMET_CB_FAIL;
    }
    top = edge.v;

    west *= DEG_PER_RAD;
    east *= DEG_PER_RAD;
    south *= DEG_PER_RAD;
    north *= DEG_PER_RAD;

    SigmetRaw_GetImgSz(&w_pxl, &h_pxl);
    px_per_m = w_pxl / (rght - left);

    /* Create image file. Fail if it exists */
    if ( !img_name(vol_p, abbrv, s, base_nm) ) {
	fprintf(err, "%s %s: could not make image file name\n%s\n",
		argv0, argv1, Err_Get());
	return SIGMET_CB_FAIL;
    }
    if (snprintf(img_fl_nm, LEN, "%s/%s.png", cl_wd, base_nm) >= LEN) {
	fprintf(err, "%s %s: could not make image file name. "
		"%s/%s.png too long.\n", argv0, argv1, cl_wd, base_nm);
	return SIGMET_CB_FAIL;
    }
    flags = O_CREAT | O_EXCL | O_WRONLY;
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if ( (i_img_fl = open(img_fl_nm, flags, mode)) == -1 ) {
        fprintf(err, "%s %s: could not create image file %s\n%s\n",
                argv0, argv1, img_fl_nm, strerror(errno));
        return SIGMET_CB_FAIL;
    }
    if ( close(i_img_fl) == -1 ) {
        fprintf(err, "%s %s: could not close image file %s\n%s\n",
                argv0, argv1, img_fl_nm, strerror(errno));
	unlink(img_fl_nm);
        return SIGMET_CB_FAIL;
    }

    /* Launch the external drawing application and create a pipe to it. */
    if ( pipe(pfd) == -1 ) {
	fprintf(err, "%s %s: could not connect to image drawing application\n%s\n",
		argv0, argv1, strerror(errno));
	unlink(img_fl_nm);
	return SIGMET_CB_FAIL;
    }
    img_pid = fork();
    switch (img_pid) {
	case -1:
	    fprintf(err, "%s %s: could not spawn image drawing application\n%s\n",
		    argv0, argv1, strerror(errno));
	    unlink(img_fl_nm);
	    return SIGMET_CB_FAIL;
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
		return SIGMET_CB_FAIL;
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
	    fprintf(err, "%s %s: could not write %s for image %s\n%s\n",
		    argv0, argv1, item, img_fl_nm, Err_Get());
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
    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	if ( vol_p->ray_ok[s][r] ) {
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_VolDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) && (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    int undef = 0;	/* If true, gate is outside map */
		    size_t npts = 4;	/* Number of vertices */

		    if ( !Sigmet_BinOutl(vol_p, s, r, b, cnrs_ll) ) {
			Err_Get();	/* Flush */
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
    p = waitpid(img_pid, &si, 0);
    if ( p == img_pid ) {
	if ( WIFEXITED(si) && WEXITSTATUS(si) == EXIT_FAILURE ) {
	    fprintf(err, "%s %s: image process failed for %s\n",
		    argv0, argv1, img_fl_nm);
	    goto error;
	} else if ( WIFSIGNALED(si) ) {
	    fprintf(err, "%s %s: image process for %s exited on signal %d. ",
			argv0, argv1, img_fl_nm, WTERMSIG(si));
	    goto error;
	}
    } else {
	fprintf(err, "%s: could not get exit status for image generator "
		"while processing %s. %s. Continuing anyway.\n",
		time_stamp(), img_fl_nm,
		(p == -1) ? strerror(errno) : "Unknown error.");
    }

    /* Make kml file and return */
    if ( snprintf(kml_fl_nm, LEN, "%s/%s.kml", cl_wd, base_nm) >= LEN ) {
	fprintf(err, "%s %s: could not make kml file name for %s\n",
		argv0, argv1, img_fl_nm);
	goto error;
    }
    if ( !(kml_fl = fopen(kml_fl_nm, "w")) ) {
	fprintf(err, "%s %s: could not open %s for output\n",
		argv0, argv1, kml_fl_nm);
	goto error;
    }
    fprintf(kml_fl, kml_tmpl,
	    vol_p->ih.ic.hw_site_name, vol_p->ih.ic.hw_site_name,
	    yr, mo, da, h, mi, sec,
	    abbrv, vol_p->sweep_angle[s] * DEG_PER_RAD,
	    img_fl_nm,
	    north, south, west, east);
    fclose(kml_fl);

    fprintf(out, "%s\n", img_fl_nm);
    return SIGMET_CB_SUCCESS;
error:
    if (img_out) {
	if ( fclose(img_out) == EOF ) {
	    fprintf(err, "%s: could not close pipe to %d "
		    "for image file %s.\n%s\n",
		    time_stamp(), img_pid, img_fl_nm, strerror(errno));
	}
    }
    unlink(img_fl_nm);
    return SIGMET_CB_FAIL;
}

static enum Sigmet_CB_Return dorade_cb(int argc, char *argv[], char *cl_wd,
	int i_out, FILE *out, int i_err, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *vol_nm_r;		/* Path to Sigmet volume from command line */
    char vol_nm[LEN];		/* Absolute path to Sigmet volume */
    int s;			/* Index of desired sweep, or -1 for all */
    char *s_s;			/* String representation of s */
    int all = -1;
    struct Sigmet_Vol *vol_p;
    enum Sigmet_CB_Return status; /* Result of SigmetRaw_ReadVol */
    struct Dorade_Sweep swp;

    if ( argc == 3 ) {
	s = all;
	vol_nm_r = argv[2];
    } else if ( argc == 4 ) {
	s_s = argv[2];
	if ( strcmp(s_s, "all") == 0 ) {
	    s = all;
	} else if ( sscanf(s_s, "%d", &s) != 1 ) {
	    fprintf(err, "%s %s: expected integer for sweep index, got \"%s\"\n",
		    argv0, argv1, s_s);
	    return SIGMET_CB_FAIL;
	}
	vol_nm_r = argv[3];
    } else {
	fprintf(err, "Usage: %s %s [s] sigmet_volume\n", argv0, argv1);
	return SIGMET_CB_FAIL;
    }
    if ( !abs_name(cl_wd, vol_nm_r, vol_nm, LEN) ) {
	fprintf(err, "%s %s: Bad volume name %s\n%s\n",
		argv0, argv1, vol_nm_r, Err_Get());
	return SIGMET_CB_FAIL;
    }
    status = SigmetRaw_ReadVol(vol_nm, err, i_err, &vol_p);
    if ( status != SIGMET_CB_SUCCESS ) {
	    return status;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for %s\n",
		argv0, argv1, s, vol_nm);
	return SIGMET_CB_FAIL;
    }
    if ( s == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( !Sigmet_ToDorade(vol_p, s, &swp) ) {
		fprintf(err, "%s %s: could not translate sweep %d of %s to "
			"DORADE format\n%s\n", argv0, argv1, s, vol_nm, Err_Get());
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp, cl_wd) ) {
		fprintf(err, "%s %s: could not write DORADE file for sweep "
			"%d of %s\n%s\n", argv0, argv1, s, vol_nm, Err_Get());
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( !Sigmet_ToDorade(vol_p, s, &swp) ) {
	    fprintf(err, "%s %s: could not translate sweep %d of %s to "
		    "DORADE format\n%s\n", argv0, argv1, s, vol_nm, Err_Get());
	    goto error;
	}
	if ( !Dorade_Sweep_Write(&swp, cl_wd) ) {
	    fprintf(err, "%s %s: could not write DORADE file for sweep "
		    "%d of %s\n%s\n", argv0, argv1, s, vol_nm, Err_Get());
	    goto error;
	}
	Dorade_Sweep_Free(&swp);
    }

    return SIGMET_CB_SUCCESS;

error:
    Dorade_Sweep_Free(&swp);
    return SIGMET_CB_FAIL;
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
void handler(int signum)
{
    char *msg;

    unlink(SIGMET_RAWD_IN);

    /* Print information about signal and exit. */
    switch (signum) {
	case SIGTERM:
	    msg = "sigmet_rawd daemon exiting on termination signal    \n";
	    write(STDOUT_FILENO, msg, 53);
	    _exit(EXIT_SUCCESS);
	case SIGFPE:
	    msg = "sigmet_rawd daemon exiting arithmetic exception     \n";
	    break;
	case SIGSYS:
	    msg = "sigmet_rawd daemon exiting on bad system call       \n";
	    break;
	case SIGXCPU:
	    msg = "sigmet_rawd daemon exiting: CPU time limit exceeded \n";
	    break;
	case SIGXFSZ:
	    msg = "sigmet_rawd daemon exiting: file size limit exceeded\n";
	    break;
    }
    write(STDERR_FILENO, msg, 53);
    _exit(EXIT_FAILURE);
}
