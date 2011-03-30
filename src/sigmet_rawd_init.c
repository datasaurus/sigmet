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
 .	$Revision: 1.377 $ $Date: 2011/03/30 16:53:12 $
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
#include "io_std.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/*
   The volume provided by this daemon.
 */

static struct Sigmet_Vol Vol;		/* Sigmet volume struct. See sigmet.h */
static int Have_Vol;			/* If true, Vol contains a volume */
static int Mod;				/* If true, Vol has been modified
					   since reading by a local function. */
static char *Vol_Fl_Nm;			/* File that provided the volume */

/*
   Size for various strings
 */

#define LEN 4096

/*
   Callbacks for the subcommands. Subcommand is the word on the command
   line (sent to the socket) after "sigmet_raw". The callback is the
   subcommand name with a "_cb" suffix.
 */

#define NCMD 33
typedef int (callback)(int , char **);
static callback pid_cb;
static callback data_types_cb;
static callback new_data_type_cb;
static callback reload_cb;
static callback setcolors_cb;
static callback volume_headers_cb;
static callback vol_hdr_cb;
static callback near_sweep_cb;
static callback sweep_headers_cb;
static callback ray_headers_cb;
static callback new_field_cb;
static callback del_field_cb;
static callback size_cb;
static callback set_field_cb;
static callback add_cb;
static callback sub_cb;
static callback mul_cb;
static callback div_cb;
static callback log10_cb;
static callback incr_time_cb;
static callback data_cb;
static callback bdata_cb;
static callback bin_outline_cb;
static callback radar_lon_cb;
static callback radar_lat_cb;
static callback shift_az_cb;
static callback set_proj_cb;
static callback get_proj_cb;
static callback img_app_cb;
static callback img_sz_cb;
static callback alpha_cb;
static callback img_cb;
static callback dorade_cb;
static char *cmd1v[NCMD] = {
    "pid", "data_types", "new_data_type", "reload", "colors",
    "volume_headers", "vol_hdr", "near_sweep", "sweep_headers",
    "ray_headers", "new_field", "del_field", "size", "set_field", "add",
    "sub", "mul", "div", "log10", "incr_time", "data", "bdata",
    "bin_outline", "radar_lon", "radar_lat", "shift_az",
    "set_proj", "get_proj", "img_app", "img_sz", "alpha", "img", "dorade"
};
static callback *cb1v[NCMD] = {
    pid_cb, data_types_cb, new_data_type_cb, reload_cb, setcolors_cb,
    volume_headers_cb, vol_hdr_cb, near_sweep_cb, sweep_headers_cb,
    ray_headers_cb, new_field_cb, del_field_cb, size_cb, set_field_cb, add_cb,
    sub_cb, mul_cb, div_cb, log10_cb, incr_time_cb, data_cb, bdata_cb,
    bin_outline_cb, radar_lon_cb, radar_lat_cb, shift_az_cb,
    set_proj_cb, get_proj_cb, img_app_cb, img_sz_cb, alpha_cb, img_cb, dorade_cb
};

#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

/*
   Other local functions
 */

static char *time_stamp(void);
static int handle_signals(void);
static void handler(int);

/*
   This function loads the volume in Sigmet raw product file vol_fl_nm
   into memory in global struct Vol, defined above.  It creates a socket
   for communication with clients. Then it puts this process into the background
   and waits for client requests on the socket.  The daemon executes callbacks
   defined below in response to the client requests. It communicates with
   clients via fifo files. If anything goes wrong, it prints an error message
   to stderr and exits this process.
 */

void SigmetRaw_Load(char *vol_fl_nm)
{
    pid_t in_pid;		/* Process that provides a volume */
    FILE *in;			/* Stream that provides a volume */
    int status;			/* Result of a function */
    int flags;			/* Flags for log files */
    struct sockaddr_un sa;	/* Socket to read command and return exit status */
    struct sockaddr *sa_p;	/* &sa or &d_err_sa, for call to bind */
    int i_dmn;			/* File descriptors for daemon socket */
    pid_t pid;			/* Return from fork */
    FILE *d_log = NULL;		/* Daemon log */
    FILE *d_err = NULL;		/* Daemon error log */
    int stdout_fileno;		/* Save a reference to stdout so it stays
				   when there are no clients. */
    int stderr_fileno;		/* Save a reference to stderr so it stays
				   when there are no clients. */
    int cl_io_fd;		/* File descriptor to read client command
				   and send results */
    pid_t client_pid = -1;	/* Client process id */
    char *cmd_ln = NULL;	/* Command line from client */
    size_t cmd_ln_l;		/* strlen(cmd_ln) */
    size_t cmd_ln_lx = 0;	/* Given size of command line */
    int stop = 0;		/* If true, exit program */
    int xstatus = SIGMET_OK;	/* Exit status of process */
    size_t sz;

    /*
       Fail if this directory already has a socket.
     */

    if ( access(SIGMET_RAWD_IN, F_OK) == 0 ) {
	fprintf(stderr, "Daemon directory %s already exists. "
		"Is daemon already running?\n", SIGMET_RAWD_IN);
	exit(SIGMET_IO_FAIL);
    }

    /*
       Save a copies of stdout and stderr so they do not close when daemon
       detaches from a client.
     */

    if ( (stdout_fileno = dup(STDOUT_FILENO)) == -1 ) {
	fprintf(d_err, "Daemon could not save standard output stream.\n%s\n",
		strerror(errno));
	goto error;
    }
    if ( (stderr_fileno = dup(STDERR_FILENO)) == -1 ) {
	fprintf(d_err, "Daemon could not save standard error stream.\n%s\n",
		strerror(errno));
	goto error;
    }

    /*
       Read the volume.
     */

    Sigmet_DataType_Init();
    Sigmet_Vol_Init(&Vol);
    in_pid = -1;
    if ( !(in = Sigmet_VolOpen(vol_fl_nm, &in_pid)) ) {
	fprintf(stderr, "Could not open %s for input.\n%s\n",
		vol_fl_nm, Err_Get());
	xstatus = SIGMET_IO_FAIL;
	goto error;
    }
    switch (status = Sigmet_Vol_Read(in, &Vol)) {
	case SIGMET_OK:
	case SIGMET_IO_FAIL:	/* Possibly truncated volume o.k. */
	    break;
	case SIGMET_ALLOC_FAIL:
	    fprintf(stderr, "Could not allocate memory while reading %s.\n%s\n",
		    vol_fl_nm, Err_Get());
	    xstatus = SIGMET_ALLOC_FAIL;
	    goto error;
	case SIGMET_BAD_FILE:
	    fprintf(stderr, "Raw product file %s is corrupt.\n%s\n",
		    vol_fl_nm, Err_Get());
	    xstatus = SIGMET_BAD_FILE;
	    goto error;
	case SIGMET_BAD_ARG:
	    fprintf(stderr, "Internal failure while reading %s.\n%s\n",
		    vol_fl_nm, Err_Get());
	    xstatus = SIGMET_BAD_ARG;
	    goto error;
    }
    fclose(in);
    if (in_pid != -1) {
	waitpid(in_pid, NULL, 0);
    }
    Have_Vol = 1;
    Mod = 0;
    sz = strlen(vol_fl_nm) + 1;
    if ( !(Vol_Fl_Nm = CALLOC(sz, 1)) ) {
	fprintf(stderr, "Could not allocate space for volume file name.\n");
	xstatus = SIGMET_ALLOC_FAIL;
	goto error;
    }
    strlcpy(Vol_Fl_Nm, vol_fl_nm, sz);

    /*
       Create a socket to recieve client requests.
     */

    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s", SIGMET_RAWD_IN) >= SA_PLEN ) {
	fprintf(stderr, "Could not fit %s into socket address path.\n",
		SIGMET_RAWD_IN);
	xstatus = SIGMET_IO_FAIL;
	goto error;
    }
    sa_p = (struct sockaddr *)&sa;
    if ( (i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1
	    || bind(i_dmn, sa_p, SA_UN_SZ) == -1
	    || listen(i_dmn, SOMAXCONN) == -1 ) {
	fprintf(stderr, "Could not create io socket.\n%s\n", strerror(errno));
	xstatus = SIGMET_IO_FAIL;
	goto error;
    }
    if ( (flags = fcntl(i_dmn, F_GETFD)) == -1
	    || fcntl(i_dmn, F_SETFD, flags | FD_CLOEXEC) == -1 ) {
	fprintf(stderr, "%s: could not set flags on io socket.\n"
		"%s\n", time_stamp(), strerror(errno));
	xstatus = SIGMET_IO_FAIL;
	goto error;
    }

    /*
       Go to background.
     */

    switch (pid = fork()) {
	case -1:
	    fprintf(stderr, "Could not fork.\n%s\n", strerror(errno));
	    xstatus = SIGMET_NOT_INIT;
	    goto error;
	case 0:
	    /*
	       Child = daemon process. Continues.
	     */

	    if ( !handle_signals() ) {
		fprintf(stderr, "Could not set up signal management "
			"in daemon.");
		xstatus = SIGMET_NOT_INIT;
		goto error;
	    }

	    /*
	       Set up output.
	     */

	    if ( !(d_log = fopen(SIGMET_RAWD_LOG, "w")) ) {
		fprintf(stderr, "Daemon could not open log file: %s\n%s\n",
			SIGMET_RAWD_LOG, strerror(errno));
		goto error;
	    }
	    if ( !(d_err = fopen(SIGMET_RAWD_ERR, "w")) ) {
		fprintf(stderr, "Daemon could not open error file: %s\n%s\n",
			SIGMET_RAWD_ERR, strerror(errno));
		goto error;
	    }
	    fclose(stdin);

	    break;
	default:
	    /*
	       Parent. Exit. Child will go to background (attach to init).
	     */

	    exit(SIGMET_OK);
    }

    fprintf(d_log, "%s: sigmet_rawd daemon starting.\nProcess id = %d.\n"
	    "Socket = %s\n", time_stamp(), getpid(), sa.sun_path);
    fflush(d_log);

    /*
       Wait for clients until "unload" subcommand is received or this process
       is signaled.
     */

    while ( !stop && (cl_io_fd = accept(i_dmn, NULL, 0)) != -1 ) {
	int argc1;		/* Number of arguments in received command line */
	char *argv1[SIGMET_RAWD_ARGCX]; /* Arguments from client command line */
	int a;			/* Index into argv1 */
	char *cmd0;		/* Name of client */
	char *cmd1;		/* Subcommand */
	char out_nm[LEN];	/* Fifo to send standard output to client */
	char err_nm[LEN];	/* Fifo to send error output to client */
	int i_out;		/* File descriptor for writing to out_nm */
	int i_err;		/* File descriptor for writing to err_nm */
	int flags;		/* Return from fcntl, when config'ing cl_io_fd */
	int sstatus;		/* Result of callback */
	int i;			/* Loop index */
	char *c, *e;		/* Loop parameters */
	void *t;		/* Hold return from realloc */

	/*
	   Close socket stream on fork to any helper application.
	 */

	if ( (flags = fcntl(cl_io_fd, F_GETFD)) == -1
		|| fcntl(cl_io_fd, F_SETFD, flags | FD_CLOEXEC) == -1 ) {
	    fprintf(d_err, "%s: could not set flags on connection to client.\n"
		    "%s\n", time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/*
	   From socket stream, read client process id, client working directory,
	   argument count, command line length, and arguments
	 */

	if ( read(cl_io_fd, &client_pid, sizeof(pid_t)) == -1 ) {
	    fprintf(d_err, "%s: failed to read client process id.\n%s\n",
		    time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( read(cl_io_fd, &argc1, sizeof(int)) == -1 ) {
	    fprintf(d_err, "%s: failed to read length of command line "
		    "for process %d.\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( argc1 + 1 > SIGMET_RAWD_ARGCX ) {
	    fprintf(d_err, "%s: cannot parse %d command line arguments for "
		    "process %d. Maximum is %d.\n",
		    time_stamp(), argc1, client_pid, SIGMET_RAWD_ARGCX);
	    close(cl_io_fd);
	    continue;
	}
	if ( read(cl_io_fd, &cmd_ln_l, sizeof(size_t)) == -1 ) {
	    fprintf(d_err, "%s: failed to read length of command line "
		    "for process %d.\n", time_stamp(), client_pid);
	    close(cl_io_fd);
	    continue;
	}
	if ( cmd_ln_l > cmd_ln_lx ) {
	    if ( !(t = REALLOC(cmd_ln, cmd_ln_l + 1)) ) {
		fprintf(d_err, "%s: allocation failed for command line of "
			"%lu bytes for process %ld.\n",
			time_stamp(), (unsigned long)cmd_ln_l, (long)client_pid);
		close(cl_io_fd);
		continue;
	    }
	    cmd_ln = t;
	    cmd_ln_lx = cmd_ln_l + 1;
	}
	memset(cmd_ln, 0, cmd_ln_lx);
	if ( read(cl_io_fd, cmd_ln, cmd_ln_l) == -1 ) {
	    fprintf(d_err, "%s: failed to read command line for "
		    "process %d.%s\n", time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	*(cmd_ln + cmd_ln_l) = '\0';

	/*
	   Break command line into arguments at nul boundaries.
	 */

	for (a = 0, argv1[a] = c = cmd_ln, e = c + cmd_ln_l;
		c < e && a < argc1;
		c++) {
	    if ( *c == '\0' ) {
		argv1[++a] = c + 1;
	    }
	}
	argv1[a] = NULL;
	if ( a > argc1 ) {
	    fprintf(d_err, "%s: command line garbled for process %d.\n",
		    time_stamp(), client_pid);
	    continue;
	}

	/*
	   Open fifos to client. Client should have already created them.
	   Output fifo is named as process id with ".1" suffix.
	   Error fifo is named as process id with ".2" suffix.
	 */

	if ( snprintf(out_nm, LEN, "%d.1", client_pid) >= LEN ) {
	    fprintf(d_err, "%s: could not create name for result pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( (i_out = open(out_nm, O_WRONLY)) == -1
		|| dup2(i_out, STDOUT_FILENO) == -1 || close(i_out) == -1 ) {
	    fprintf(d_err, "%s: could not open pipe for standard output for "
		    "process %d\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    continue;
	}
	if ( snprintf(err_nm, LEN, "%d.2", client_pid) >= LEN ) {
	    fprintf(d_err, "%s: could not create name for error pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( (i_err = open(err_nm, O_WRONLY)) == -1
		|| dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
	    fprintf(d_err, "%s: could not open pipe for standard error for "
		    "process %d\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    continue;
	}

	/*
	   Identify subcommand. If subcommand is "unload", set the stop
	   flag, which will enable a break and exit at end of loop body.
	   Otherwise, invoke the subcommand's callback.
	 */

	cmd0 = argv1[0];
	cmd1 = argv1[1];
	if ( strcmp(cmd1, "unload") == 0 ) {
	    if ( argc1 == 2 ) {
		if ( Mod ) {
		    fprintf(stderr, "Volume in memory has been modified.\n"
			    "Use -f to force unload.\n");
		    sstatus = SIGMET_BAD_ARG;
		} else {
		    close(i_dmn);
		    stop = 1;
		    sstatus = SIGMET_OK;
		}
	    } else if ( argc1 == 3 && strcmp(argv1[2], "-f") == 0 ) {
		stop = 1;
		sstatus = SIGMET_OK;
	    } else {
		fprintf(stderr, "Usage: %s %s [-f]\n", cmd0, cmd1);
		sstatus = SIGMET_BAD_ARG;
	    }
	} else if ( (i = SigmetRaw_Cmd(cmd1)) == -1) {
	    fprintf(stderr, "No option or subcommand named %s. "
		    "Subcommand must be one of: ", cmd1);
	    for (i = 0; i < NCMD; i++) {
		fprintf(stderr, "%s ", cmd1v[i]);
	    }
	    fprintf(stderr, "\n");
	    sstatus = SIGMET_BAD_ARG;
	} else {
	    sstatus = (cb1v[i])(argc1, argv1);
	    if ( sstatus != SIGMET_OK ) {
		fprintf(stderr, "%s\n", Err_Get());
	    }
	}

	/*
	   Callback, if any, is done. Close fifo's. Client will delete them.
	   Send callback exit status, which will be a SIGMET_* return value
	   defined in sigmet.h, through the daemon socket and close
	   the client side of the socket. Break out of loop if subcommand
	   is "stop".
	 */

	fflush(stdout);
	if ( dup2(stdout_fileno, STDOUT_FILENO) == -1 ) {
	    fprintf(d_err, "%s: could not restore stdout after detaching "
		    "from client\n%s\n", time_stamp(), strerror(errno));
	    exit(SIGMET_IO_FAIL);
	}
	fflush(stderr);
	if ( dup2(stderr_fileno, STDOUT_FILENO) == -1 ) {
	    fprintf(d_err, "%s: could not restore stderr after detaching "
		    "from client\n%s\n", time_stamp(), strerror(errno));
	    exit(SIGMET_IO_FAIL);
	}
	if ( write(cl_io_fd, &sstatus, sizeof(int)) == -1 ) {
	    fprintf(d_err, "%s: could not send return code for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
	if ( close(cl_io_fd) == -1 ) {
	    fprintf(d_err, "%s: could not close socket for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
    }

    /*
       Out of loop. No longer waiting for clients.
     */

    unlink(SIGMET_RAWD_IN);
    FREE(cmd_ln);
    fprintf(d_log, "%s: exiting.\n", time_stamp());
    exit(xstatus);

error:
    fprintf(stderr, "%s: Could not spawn sigmet_raw daemon.\n",
	    time_stamp());
    if ( d_err ) {
	fprintf(d_err, "%s: Could not spawn sigmet_raw daemon.\n",
		time_stamp());
    }
    exit(xstatus);
}

/*
   This convenience function returns a character string with the current time
   in it. Caller should not modify the return value.
 */

static char *time_stamp(void)
{
    static char ts[LEN];
    time_t now;

    now = time(NULL);
    if ( strftime(ts, LEN, "%Y/%m/%d %H:%M:%S %Z", localtime(&now)) ) {
	return ts;
    } else {
	return "";
    }
}

static int pid_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("%d\n", getpid());
    return SIGMET_OK;
}

static int new_data_type_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *name, *desc, *unit;

    if ( argc != 5 ) {
	fprintf(stderr, "Usage: %s %s name descriptor unit\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    name = argv[2];
    desc = argv[3];
    unit = argv[4];
    switch (DataType_Add(name, desc, unit, DATA_TYPE_FLT, DataType_DblToDbl)) {
	case DATATYPE_ALLOC_FAIL:
	    return SIGMET_ALLOC_FAIL;
	case DATATYPE_INPUT_FAIL:
	    return SIGMET_IO_FAIL;
	case DATATYPE_BAD_ARG:
	    return SIGMET_BAD_ARG;
	case DATATYPE_SUCCESS:
	    return SIGMET_OK;
    }
    return SIGMET_OK;
}

static int data_types_cb(int argc, char *argv[])
{
    struct DataType *data_type;		/* Information about a data type */
    size_t n;
    char **abbrvs, **a;

    abbrvs = DataType_Abbrvs(&n);
    if ( abbrvs ) {
	for (a = abbrvs; a < abbrvs + n; a++) {
	    data_type = DataType_Get(*a);
	    assert(data_type);
	    printf("%s | %s | %s | ",
		    *a, data_type->descr, data_type->unit);
	    if ( Hash_Get(&Vol.types_tbl, *a) ) {
		printf("present\n");
	    } else {
		printf("unused\n");
	    }
	}
    }

    return SIGMET_OK;
}

static int reload_cb(int argc, char *argv[])
{
    struct Sigmet_Vol vol;
    pid_t in_pid;
    FILE *in;
    int status;

    if ( Mod ) {
	fprintf(stderr, "Cannot reload volume which has been modified.");
	return SIGMET_BAD_ARG;
    }
    Sigmet_Vol_Init(&vol);
    in_pid = 0;
    if ( !(in = Sigmet_VolOpen(Vol_Fl_Nm, &in_pid)) ) {
	fprintf(stderr, "Could not open %s for input.\n%s\n",
		Vol_Fl_Nm, Err_Get());
	return SIGMET_IO_FAIL;
    }
    switch (status = Sigmet_Vol_Read(in, &vol)) {
	case SIGMET_OK:
	case SIGMET_IO_FAIL:	/* Possibly truncated volume o.k. */
	    Sigmet_Vol_Free(&Vol);
	    Vol = vol;
	    break;
	case SIGMET_ALLOC_FAIL:
	    fprintf(stderr, "Could not allocate memory while reloading %s. "
		    "Volume remains as previously loaded.\n", Vol_Fl_Nm);
	case SIGMET_BAD_FILE:
	    fprintf(stderr, "Raw product file %s is corrupt. "
		    "Volume remains as previously loaded.\n", Vol_Fl_Nm);
	case SIGMET_BAD_ARG:
	    fprintf(stderr, "Internal failure while reading %s. "
		    "Volume remains as previously loaded.\n", Vol_Fl_Nm);
    }
    fclose(in);
    if (in_pid != 0) {
	waitpid(in_pid, NULL, 0);
    }
    return status;
}

static int setcolors_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;			/* Data type abbreviation */
    char *clr_fl_nm;			/* File with colors */
    int status;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type colors_file\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    clr_fl_nm = argv[3];
    status = DataType_SetColors(abbrv, clr_fl_nm);
    if ( status != DATATYPE_SUCCESS ) {
	fprintf(stderr, "%s\n", Err_Get());
    }
    switch (status) {
	case DATATYPE_SUCCESS:
	    return SIGMET_OK;
	case DATATYPE_INPUT_FAIL:
	    return SIGMET_IO_FAIL;
	case DATATYPE_ALLOC_FAIL:
	    return SIGMET_ALLOC_FAIL;
	case DATATYPE_BAD_ARG:
	    return SIGMET_BAD_ARG;
    }
    return SIGMET_OK;
}

static int volume_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    Sigmet_Vol_PrintHdr(stdout, &Vol);
    return SIGMET_OK;
}

static int vol_hdr_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("site_name=\"%s\"\n", Vol.ih.ic.su_site_name);
    printf("radar_lon=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(Vol.ih.ic.longitude), 0.0) * DEG_PER_RAD);
    printf("radar_lat=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(Vol.ih.ic.latitude), 0.0) * DEG_PER_RAD);
    printf("task_name=\"%s\"\n", Vol.ph.pc.task_name);
    printf("types=\"");
    if ( Vol.dat[0].data_type->abbrv ) {
	printf("%s", Vol.dat[0].data_type->abbrv);
    }
    for (y = 1; y < Vol.num_types; y++) {
	if ( Vol.dat[y].data_type->abbrv ) {
	    printf(" %s", Vol.dat[y].data_type->abbrv);
	}
    }
    printf("\"\n");
    printf("num_sweeps=%d\n", Vol.ih.ic.num_sweeps);
    printf("num_rays=%d\n", Vol.ih.ic.num_rays);
    printf("num_bins=%d\n", Vol.ih.tc.tri.num_bins_out);
    printf("range_bin0=%d\n", Vol.ih.tc.tri.rng_1st_bin);
    printf("bin_step=%d\n", Vol.ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * Vol.ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = Vol.ih.tc.tdi.prf;
    mp = Vol.ih.tc.tdi.m_prf_mode;
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
    printf("prf=%.2lf\n", prf);
    printf("prf_mode=%s\n", mp_s);
    printf("vel_ua=%.3lf\n", vel_ua);
    return SIGMET_OK;
}

static int near_sweep_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;			/* Sweep angle, degrees */
    double ang, da;
    int s, nrst;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s angle\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    ang_s = argv[2];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point for sweep angle, got %s\n",
		argv0, argv1, ang_s);
	return SIGMET_BAD_ARG;
    }
    ang *= RAD_PER_DEG;
    if ( !Vol.sweep_angle ) {
	fprintf(stderr, "%s %s: sweep angles not loaded. "
		"Is volume truncated?.\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    nrst = -1;
    for (da = DBL_MAX, s = 0; s < Vol.num_sweeps_ax; s++) {
	if ( fabs(Vol.sweep_angle[s] - ang) < da ) {
	    da = fabs(Vol.sweep_angle[s] - ang);
	    nrst = s;
	}
    }
    printf("%d\n", nrst);
    return SIGMET_OK;
}

static int sweep_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < Vol.ih.tc.tni.num_sweeps; s++) {
	printf("sweep %d ", s);
	if ( !Vol.sweep_ok[s] ) {
	    printf("bad\n");
	} else {
	    int yr, mon, da, hr, min;
	    double sec;

	    if ( Tm_JulToCal(Vol.sweep_time[s],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		printf("%04d/%02d/%02d %02d:%02d:%04.3f ",
			yr, mon, da, hr, min, sec);
	    } else {
		printf("bad time (%s). ", Err_Get());
	    }
	    printf("%7.3f\n", Vol.sweep_angle[s] * DEG_PER_RAD);
	}
    }
    return SIGMET_OK;
}

static int ray_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s, r;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < Vol.num_sweeps_ax; s++) {
	if ( Vol.sweep_ok[s] ) {
	    for (r = 0; r < (int)Vol.ih.ic.num_rays; r++) {
		int yr, mon, da, hr, min;
		double sec;

		if ( !Vol.ray_ok[s][r] ) {
		    continue;
		}
		printf("sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(Vol.ray_time[s][r],
			    &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(stderr, "%s %s: bad ray time\n%s\n",
			    argv0, argv1, Err_Get());
		    return SIGMET_BAD_TIME;
		}
		printf("%04d/%02d/%02d %02d:%02d:%04.3f | ",
			yr, mon, da, hr, min, sec);
		printf("az %7.3f %7.3f | ",
			Vol.ray_az0[s][r] * DEG_PER_RAD,
			Vol.ray_az1[s][r] * DEG_PER_RAD);
		printf("tilt %6.3f %6.3f\n",
			Vol.ray_tilt0[s][r] * DEG_PER_RAD,
			Vol.ray_tilt1[s][r] * DEG_PER_RAD);
	    }
	}
    }
    return SIGMET_OK;
}

static int new_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;			/* Data type abbreviation */
    char *d_s = NULL;			/* Initial value */
    double d;
    int status;				/* Result of a function */

    if ( argc == 3 ) {
	abbrv = argv[2];
    } else if ( argc == 4 ) {
	abbrv = argv[2];
	d_s = argv[3];
    } else {
	fprintf(stderr, "Usage: %s %s data_type [value]\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: No data type named %s. Please add with the "
		"new_data_type command.\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_NewField(&Vol, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add data type %s to volume\n%s\n",
		argv0, argv1, abbrv, Err_Get());
	return status;
    }
    if ( d_s ) {
	if ( sscanf(d_s, "%lf", &d) == 1 ) {
	    status = Sigmet_Vol_Fld_SetVal(&Vol, abbrv, d);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d, Err_Get());
		return status;
	    }
	} else if ( strcmp(d_s, "r_beam") == 0 ) {
	    status = Sigmet_Vol_Fld_SetRBeam(&Vol, abbrv);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d_s, Err_Get());
		return status;
	    }
	} else {
	    status = Sigmet_Vol_Fld_Copy(&Vol, abbrv, d_s);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d_s, Err_Get());
		return status;
	    }
	}
    }
    Mod = 1;
    return SIGMET_OK;
}

static int del_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;			/* Data type abbreviation */
    int status;				/* Result of a function */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: No data type named %s.\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_DelField(&Vol, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not remove data type %s from volume\n%s\n",
		argv0, argv1, abbrv, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Print volume memory usage.
 */

static int size_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("%lu\n", (unsigned long)Vol.size);
    return SIGMET_OK;
}

/*
   Set value for a field.
 */

static int set_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    d_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }

    /*
       Parse value and set in data array.
       "r_beam" => set bin value to distance along bin, in meters.
       Otherwise, value must be a floating point number.
     */

    if ( strcmp("r_beam", d_s) == 0 ) {
	if ( (status = Sigmet_Vol_Fld_SetRBeam(&Vol, abbrv)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to beam range in volume\n"
		    "%s\n", argv0, argv1, abbrv, Err_Get());
	    return status;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SetVal(&Vol, abbrv, d)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n",
		    argv0, argv1, abbrv, d, Err_Get());
	    return status;
	}
    } else {
	fprintf(stderr, "%s %s: field value must be a number or \"r_beam\"\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Add a scalar or another field to a field.
 */

static int add_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_AddVal(&Vol, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not add %s to %lf in volume\n%s\n",
		    argv0, argv1, abbrv, a, Err_Get());
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_AddFld(&Vol, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add %s to %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Subtract a scalar or another field from a field.
 */

static int sub_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SubVal(&Vol, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not subtract %lf from %s in "
		    "volume\n%s\n", argv0, argv1, a, abbrv, Err_Get());
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_SubFld(&Vol, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not subtract %s from %s in volume\n%s\n",
		argv0, argv1, a_s, abbrv, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Multiply a field by a scalar or another field
 */

static int mul_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_MulVal(&Vol, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not multiply %s by %lf in volume\n%s\n",
		    argv0, argv1, abbrv, a, Err_Get());
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_MulFld(&Vol, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not multiply %s by %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Divide a field by a scalar or another field
 */

static int div_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_DivVal(&Vol, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not divide %s by %lf in volume\n%s\n",
		    argv0, argv1, abbrv, a, Err_Get());
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_DivFld(&Vol, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not divide %s by %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

/*
   Replace a field with it's log10.
 */

static int log10_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( !DataType_Get(abbrv) ) {
	fprintf(stderr, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_Fld_Log10(&Vol, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute log10 of %s in volume\n%s\n",
		argv0, argv1, abbrv, Err_Get());
	return status;
    }
    Mod = 1;
    return SIGMET_OK;
}

static int incr_time_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *dt_s;
    double dt;				/* Time increment, seconds */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dt\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    dt_s = argv[2];
    if ( sscanf(dt_s, "%lf", &dt) != 1) {
	fprintf(stderr, "%s %s: expected float value for time increment, got "
		"%s\n", argv0, argv1, dt_s);
	return SIGMET_BAD_ARG;
    }
    if ( !Sigmet_Vol_IncrTm(&Vol, dt / 86400.0) ) {
	fprintf(stderr, "%s %s: could not increment time in volume\n%s\n",
		argv0, argv1, Err_Get());
	return SIGMET_BAD_TIME;
    }
    Mod = 1;
    return SIGMET_OK;
}

static int data_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s, y, r, b;
    char *abbrv;
    double d;
    struct Sigmet_DatArr *dat_p;
    int all = -1;

    /*
       Identify input and desired output
       Possible forms:
	   sigmet_ray data			(argc = 3)
	   sigmet_ray data data_type		(argc = 4)
	   sigmet_ray data data_type s		(argc = 5)
	   sigmet_ray data data_type s r	(argc = 6)
	   sigmet_ray data data_type s r b	(argc = 7)
     */

    abbrv = NULL;
    y = s = r = b = all;
    if ( argc >= 3 ) {
	abbrv = argv[2];
    }
    if ( argc >= 4 && sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 5 && sscanf(argv[4], "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 6 && sscanf(argv[5], "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 7 ) {
	fprintf(stderr, "Usage: %s %s [[[[data_type] sweep] ray] bin]\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }

    /*
       Validate.
     */

    if ( abbrv ) {
	if ( (dat_p = Hash_Get(&Vol.types_tbl, abbrv)) ) {
	    y = dat_p - Vol.dat;
	} else {
	    fprintf(stderr, "%s %s: no data type named %s\n",
		    argv0, argv1, abbrv);
	    return SIGMET_BAD_ARG;
	}
    }
    if ( s != all && s >= Vol.num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r != all && r >= (int)Vol.ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b != all && b >= Vol.ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < Vol.num_types; y++) {
	    for (s = 0; s < Vol.num_sweeps_ax; s++) {
		abbrv = Vol.dat[y].data_type->abbrv;
		printf("%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)Vol.ih.ic.num_rays; r++) {
		    if ( !Vol.ray_ok[s][r] ) {
			continue;
		    }
		    printf("ray %d: ", r);
		    for (b = 0; b < Vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_Vol_GetDat(&Vol, y, s, r, b);
			if ( Sigmet_IsData(d) ) {
			    printf("%f ", d);
			} else {
			    printf("nodat ");
			}
		    }
		    printf("\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < Vol.num_sweeps_ax; s++) {
	    printf("%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < Vol.ih.ic.num_rays; r++) {
		if ( !Vol.ray_ok[s][r] ) {
		    continue;
		}
		printf("ray %d: ", r);
		for (b = 0; b < Vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_Vol_GetDat(&Vol, y, s, r, b);
		    if ( Sigmet_IsData(d) ) {
			printf("%f ", d);
		    } else {
			printf("nodat ");
		    }
		}
		printf("\n");
	    }
	}
    } else if ( r == all && b == all ) {
	printf("%s. sweep %d\n", abbrv, s);
	for (r = 0; r < Vol.ih.ic.num_rays; r++) {
	    if ( !Vol.ray_ok[s][r] ) {
		continue;
	    }
	    printf("ray %d: ", r);
	    for (b = 0; b < Vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_Vol_GetDat(&Vol, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else if ( b == all ) {
	if ( Vol.ray_ok[s][r] ) {
	    printf("%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < Vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_Vol_GetDat(&Vol, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else {
	if ( Vol.ray_ok[s][r] ) {
	    printf("%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_Vol_GetDat(&Vol, y, s, r, b);
	    if ( Sigmet_IsData(d) ) {
		printf("%f ", d);
	    } else {
		printf("nodat ");
	    }
	    printf("\n");
	}
    }
    return SIGMET_OK;
}

/*
   Print sweep data as a binary stream.
   sigmet_ray bdata data_type s
   Each output ray will have num_output_bins floats.
   Missing values will be Sigmet_NoData().
 */

static int bdata_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_DatArr *dat_p;
    int s, y, r, b;
    char *abbrv;
    static float *ray_p;	/* Buffer to receive ray data */
    int num_bins_out;
    int status, n;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type sweep_index\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    if ( (dat_p = Hash_Get(&Vol.types_tbl, abbrv)) ) {
	y = dat_p - Vol.dat;
    } else {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( s >= Vol.num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    n = num_bins_out = Vol.ih.tc.tri.num_bins_out;
    if ( !ray_p && !(ray_p = CALLOC(num_bins_out, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate output buffer for ray.\n");
	return SIGMET_ALLOC_FAIL;
    }
    for (r = 0; r < Vol.ih.ic.num_rays; r++) {
	for (b = 0; b < num_bins_out; b++) {
	    ray_p[b] = Sigmet_NoData();
	}
	if ( Vol.ray_ok[s][r] ) {
	    status = Sigmet_Vol_GetRayDat(&Vol, y, s, r, &ray_p, &n);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "Could not get ray data for data type %s, "
			"sweep index %d, ray %d.\n", abbrv, s, r);
		return status;
	    }
	    if ( n > num_bins_out ) {
		fprintf(stderr, "Ray %d or sweep %d, data type %s has "
			"unexpected number of bins - %d instead of %d.\n",
			r, s, abbrv, n, num_bins_out);
		return SIGMET_BAD_VOL;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins_out, stdout)
		!= num_bins_out ) {
	    fprintf(stderr, "Could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n",
		    abbrv, s, r, strerror(errno));
	    return SIGMET_IO_FAIL;
	}
    }
    return SIGMET_OK;
}

static int bin_outline_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];

    if ( argc != 5 ) {
	fprintf(stderr, "Usage: %s %s sweep ray bin\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];

    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(r_s, "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(b_s, "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return SIGMET_BAD_ARG;
    }
    if ( s >= Vol.num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r >= Vol.ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b >= Vol.ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }
    if ( (status = Sigmet_Vol_BinOutl(&Vol, s, r, b, corners)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute bin outlines for bin %d %d %d "
		"in volume\n%s\n", argv0, argv1, s, r, b, Err_Get());
	return status;
    }
    printf("%f %f %f %f %f %f %f %f\n",
	    corners[0] * DEG_RAD, corners[1] * DEG_RAD,
	    corners[2] * DEG_RAD, corners[3] * DEG_RAD,
	    corners[4] * DEG_RAD, corners[5] * DEG_RAD,
	    corners[6] * DEG_RAD, corners[7] * DEG_RAD);

    return SIGMET_OK;
}

static int radar_lon_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *lon_s;			/* New longitude, degrees, in argv */
    double lon;				/* New longitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lon\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lon_s = argv[2];
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for new longitude, "
		"got %s\n", argv0, argv1, lon_s);
	return SIGMET_BAD_ARG;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    Vol.ih.ic.longitude = Sigmet_RadBin4(lon);
    Mod = 1;

    return SIGMET_OK;
}

static int radar_lat_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *lat_s;			/* New latitude, degrees, in argv */
    double lat;				/* New latitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lat\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lat_s = argv[2];
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for new latitude, "
		"got %s\n", argv0, argv1, lat_s);
	return SIGMET_BAD_ARG;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    Vol.ih.ic.latitude = Sigmet_RadBin4(lat);
    Mod = 1;

    return SIGMET_OK;
}

static int shift_az_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *daz_s;			/* Degrees to add to each azimuth */
    double daz;				/* Radians to add to each azimuth */
    unsigned long idaz;			/* Binary angle to add to each azimuth */
    int s, r;				/* Loop indeces */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dz\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    daz_s = argv[2];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(stderr, "%s %s: expected float value for azimuth shift, got %s\n",
		argv0, argv1, daz_s);
	return SIGMET_BAD_ARG;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    idaz = Sigmet_RadBin4(daz);
    switch (Vol.ih.tc.tni.scan_mode) {
	case RHI:
	    for (s = 0; s < Vol.num_sweeps_ax; s++) {
		Vol.ih.tc.tni.scan_info.rhi_info.az[s] += idaz;
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    for (s = 0; s < Vol.num_sweeps_ax; s++) {
		Vol.ih.tc.tni.scan_info.ppi_info.left_az += idaz;
		Vol.ih.tc.tni.scan_info.ppi_info.right_az += idaz;
	    }
	    break;
	case FILE_SCAN:
	    Vol.ih.tc.tni.scan_info.file_info.az0 += idaz;
	case MAN_SCAN:
	    break;
    }
    for (s = 0; s < Vol.num_sweeps_ax; s++) {
	for (r = 0; r < (int)Vol.ih.ic.num_rays; r++) {
	    Vol.ray_az0[s][r]
		= GeogLonR(Vol.ray_az0[s][r] + daz, 180.0 * RAD_PER_DEG);
	    Vol.ray_az1[s][r]
		= GeogLonR(Vol.ray_az1[s][r] + daz, 180.0 * RAD_PER_DEG);
	}
    }
    Mod = 1;
    return SIGMET_OK;
}

static int set_proj_cb(int argc, char *argv[])
{
    int status;
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( (status = SigmetRaw_SetProj(argc - 2, argv + 2)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not set projection\n%s\n",
		argv0, argv1, Err_Get());
	return status;
    }
    return SIGMET_OK;
}

static int get_proj_cb(int argc, char *argv[])
{
    char **p;

    for (p = SigmetRaw_GetProj(); *p; p++) {
	printf("%s ", *p);
    }
    printf("\n");
    return SIGMET_OK;
}

static int img_sz_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    unsigned w_pxl, h_pxl;		/* New image width and height, pixels */

    if ( argc == 2 ) {
	SigmetRaw_GetImgSz(&w_pxl, &h_pxl);
	printf("%u %u\n", w_pxl, h_pxl);
	return SIGMET_OK;
    } else if ( argc == 3 ) {
	char *w_pxl_s = argv[2];

	if ( sscanf(w_pxl_s, "%u", &w_pxl) != 1 ) {
	    fprintf(stderr, "%s %s: expected integer for display width, "
		    "got %s\n", argv0, argv1, w_pxl_s);
	    return SIGMET_BAD_ARG;
	}
	SigmetRaw_SetImgSz(w_pxl, w_pxl);
	return SIGMET_OK;
    } else if ( argc == 4 ) {
	char *w_pxl_s = argv[2];
	char *h_pxl_s = argv[3];

	if ( sscanf(w_pxl_s, "%u", &w_pxl) != 1 ) {
	    fprintf(stderr, "%s %s: expected integer for display width, "
		    "got %s\n", argv0, argv1, w_pxl_s);
	    return SIGMET_BAD_ARG;
	}
	if ( sscanf(h_pxl_s, "%u", &h_pxl) != 1 ) {
	    fprintf(stderr, "%s %s: expected integer for display height, "
		    "got %s\n", argv0, argv1, h_pxl_s);
	    return SIGMET_BAD_ARG;
	}
	SigmetRaw_SetImgSz(w_pxl, h_pxl);
	return SIGMET_OK;
    } else {
	fprintf(stderr, "Usage: %s %s [width_pxl] [height_pxl]\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
}

static int img_app_cb(int argc, char *argv[])
{
    int status;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *img_app_s;			/* Path name of image generator */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s img_app\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    img_app_s = argv[2];
    if ( (status = SigmetRaw_SetImgApp(img_app_s)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: Could not set image application to %s.\n%s\n",
		argv0, argv1, img_app_s, Err_Get());
	return status;
    }
    return SIGMET_OK;
}

static int alpha_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *alpha_s;
    double alpha;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s value\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    alpha_s = argv[2];
    if ( sscanf(alpha_s, "%lf", &alpha) != 1 ) {
	fprintf(stderr, "%s %s: expected float value for alpha value, got %s\n",
		argv0, argv1, alpha_s);
	return SIGMET_BAD_ARG;
    }
    SigmetRaw_SetImgAlpha(alpha);
    return SIGMET_OK;
}

static int img_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status = SIGMET_OK;		/* Return value of this function */
    char *s_s;				/* Sweep index, as a string */
    char *abbrv;			/* Data type abbreviation */
    struct DataType *data_type;		/* Information about the data type */
    int s;				/* Sweep index */
    char **proj_argv;			/* Projection command. */
    int yr, mo, da, h, mi;
    double sec;				/* Sweep time */
    double north, east, south, west;	/* Map edges */
    double edges[4];			/* {north, east, south, west} */
    char *base_nm; 			/* Base name for image and kml file */
    char img_fl_nm[LEN]; 		/* Image file name */
    unsigned w_pxl, h_pxl;		/* Width and height of image, in display
					   units */
    double alpha;			/* Image alpha channel */
    char *img_app;			/* External application to draw image */
    char kml_fl_nm[LEN];		/* KML output file name */
    FILE *kml_fl;			/* KML file */

    /*
       This format string creates a KML file.
     */

    char kml_tmpl[] = 
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
	"  <GroundOverlay>\n"
	"    <name>%s sweep</name>\n"
	"    <description>"
	"      %s at %02d/%02d/%02d %02d:%02d:%02.0f. Field: %s. %04.1f degrees"
	"    </description>\n"
	"    <LatLonBox>\n"
	"      <north>%f</north>\n"
	"      <south>%f</south>\n"
	"      <west>%f</west>\n"
	"      <east>%f</east>\n"
	"    </LatLonBox>\n"
	"  </GroundOverlay>\n"
	"</kml>\n";

    memset(img_fl_nm, 0, LEN);
    memset(kml_fl_nm, 0, LEN);

    /*
       Parse and validate arguments.
     */

    if ( argc != 5 ) {
	fprintf(stderr, "Usage: %s %s data_type sweep base_name\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    s_s = argv[3];
    base_nm = argv[4];
    img_app = SigmetRaw_GetImgApp();
    if ( !img_app || strlen(img_app) == 0 ) {
	fprintf(stderr, "%s %s: sweep drawing application not set\n",
		argv0, argv1);
	return SIGMET_NOT_INIT;
    }
    if ( !(data_type = DataType_Get(abbrv)) ) {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( !Tm_JulToCal(Vol.sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	fprintf(stderr, "%s %s: could not get time for sweep %d\n",
		argv0, argv1, s);
    }

    /*
       Fail if image file already exists. Assume png file with ".png" suffix.
     */

    if ( snprintf(img_fl_nm, LEN, "%s.png", base_nm) >= LEN ) {
	Err_Append("could not make image file name. ");
	Err_Append(base_nm);
	Err_Append(". ");
	return SIGMET_RNG_ERR;
    }
    if ( (access(img_fl_nm, F_OK)) == 0 ) {
	Err_Append(img_fl_nm);
	Err_Append(" already exists. ");
	return SIGMET_BAD_ARG;
    }

    proj_argv = SigmetRaw_GetProj();
    SigmetRaw_GetImgSz(&w_pxl, &h_pxl);
    alpha = SigmetRaw_GetImgAlpha();
    status = Sigmet_Vol_Img_PPI(&Vol, abbrv, s, img_app, proj_argv, w_pxl,
	    alpha, base_nm, edges);
    if ( status != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not make image file for data type %s, "
		"sweep %d.\n%s\n", argv0, argv1, abbrv, s, Err_Get());
	goto error;
    }
    north = edges[0];
    east = edges[1];
    south = edges[2];
    west = edges[3];

    /*
       Make kml file.
     */

    if ( snprintf(kml_fl_nm, LEN, "%s.kml", base_nm) >= LEN ) {
	Err_Append("could not make kml file name for ");
	Err_Append(img_fl_nm);
	Err_Append(". ");
	goto error;
    }
    if ( !(kml_fl = fopen(kml_fl_nm, "w")) ) {
	Err_Append("could not open ");
	Err_Append(kml_fl_nm);
	Err_Append(". ");
	goto error;
    }
    fprintf(kml_fl, kml_tmpl,
	    Vol.ih.ic.hw_site_name, Vol.ih.ic.hw_site_name,
	    yr, mo, da, h, mi, sec,
	    abbrv, Vol.sweep_angle[s] * DEG_PER_RAD,
	    north, south, west, east);
    fclose(kml_fl);

    printf("%s\n", img_fl_nm);
    return status;

error:
    unlink(img_fl_nm);
    unlink(kml_fl_nm);
    return status;
}

static int dorade_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;				/* Index of desired sweep,
					   or -1 for all */
    char *s_s;				/* String representation of s */
    int all = -1;
    int status;				/* Result of a function */
    struct Dorade_Sweep swp;

    if ( argc == 2 ) {
	s = all;
    } else if ( argc == 3 ) {
	s_s = argv[2];
	if ( strcmp(s_s, "all") == 0 ) {
	    s = all;
	} else if ( sscanf(s_s, "%d", &s) != 1 ) {
	    fprintf(stderr, "%s %s: expected integer for sweep index, got \"%s"
		    "\"\n", argv0, argv1, s_s);
	    return SIGMET_BAD_ARG;
	}
    } else {
	fprintf(stderr, "Usage: %s %s [s]\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( s >= Vol.num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( s == all ) {
	for (s = 0; s < Vol.num_sweeps_ax; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (status = Sigmet_Vol_ToDorade(&Vol, s, &swp)) != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not translate sweep %d of volume "
			"to DORADE format\n%s\n",
			argv0, argv1, s, Err_Get());
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(stderr, "%s %s: could not write DORADE file for sweep "
			"%d of volume\n%s\n", argv0, argv1, s, Err_Get());
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (status = Sigmet_Vol_ToDorade(&Vol, s, &swp)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not translate sweep %d of volume to "
		    "DORADE format\n%s\n", argv0, argv1, s, Err_Get());
	    goto error;
	}
	if ( !Dorade_Sweep_Write(&swp) ) {
	    fprintf(stderr, "%s %s: could not write DORADE file for sweep "
		    "%d of volume\n%s\n", argv0, argv1, s, Err_Get());
	    goto error;
	}
	Dorade_Sweep_Free(&swp);
    }

    return SIGMET_OK;

error:
    Dorade_Sweep_Free(&swp);
    return status;
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

    /*
       Signals to ignore
     */

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

    /*
       Generic action for termination signals
     */

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

/*
   For exit signals, delete the socket and print an error message.
 */

void handler(int signum)
{
    char *msg;
    ssize_t dum;

    msg = "sigmet_rawd daemon exiting                          \n";
    switch (signum) {
	case SIGTERM:
	    msg = "sigmet_rawd daemon exiting on termination signal    \n";
	    dum = write(STDOUT_FILENO, msg, 53);
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
    dum = write(STDERR_FILENO, msg, 53);
    _exit(EXIT_FAILURE);
}
