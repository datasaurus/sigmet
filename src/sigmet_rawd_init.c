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
 .	$Revision: 1.182 $ $Date: 2010/03/25 21:05:57 $
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include "alloc.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "bisearch_lib.h"
#include "data_types.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/* Size for various strings */
#define LEN 1024

/* Maximum number of arguments in input command */
#define ARGCX 512

/* If true, send full error messages. */
static int verbose = 0;

/* If true, use degrees instead of radians */
static int use_deg = 0;

/* Sigmet volumes */
struct sig_vol {
    int oqpd;			/* Occupied. True => struct contains a volume */
    struct Sigmet_Vol vol;	/* Sigmet volume */
    char vol_nm[LEN];		/* A link to the file that provided the volume */
    dev_t st_dev;		/* Device that provided vol */
    ino_t st_ino;		/* I-number of file that provided vol */
    int users;			/* Number of client sessions using vol */
};

#define N_VOLS 256
static struct sig_vol vols[N_VOLS];	/* Available volumes */

/* Find member of vols given a file * name */
static int hash(char *, struct stat *);
static int get_vol_i(char *);
static int new_vol_i(char *, struct stat *);

/* Process streams and files */
static int i_cmd0;		/* Where to get commands */
static int i_cmd1;		/* Unused outptut (see explanation below). */
static FILE *rslt1;		/* Where to send standard output */
static FILE *rslt2;		/* Where to send standard error */
pid_t client_pid;		/* Process id of client */

/* These variables determine whether and when a slow client will be killed. */
static int tmgout = 1;		/* If true, time out blocking clients. */
static int tmoadj = 1;		/* If true, adjust timeout periodically. */
static unsigned tmout = 60;	/* Max seconds client can block daemon */

/* Input line - has commands for the daemon */
#define BUF_L 512
static char buf[BUF_L];
static char *buf_e = buf + BUF_L;

/* Application and subcommand name */
static char *cmd;
static char *cmd1;

/* Subcommands */
#define NCMD 27
typedef int (callback)(int , char **);
static callback cmd_len_cb;
static callback verbose_cb;
static callback pid_cb;
static callback timeout_cb;
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
static callback proj_cb;
static callback img_app_cb;
static callback geom_cb;
static callback alpha_cb;
static callback img_name_cb;
static callback img_cb;
static callback stop_cb;
static char *cmd1v[NCMD] = {
    "cmd_len", "verbose", "pid", "timeout", "types", "good", "hread",
    "read", "list", "release", "unload", "flush", "volume_headers",
    "vol_hdr", "near_sweep", "ray_headers", "data", "bin_outline",
    "colors", "bintvls", "proj", "img_app", "geom", "alpha",
    "img_name", "img", "stop"
};
static callback *cb1v[NCMD] = {
    cmd_len_cb, verbose_cb, pid_cb, timeout_cb, types_cb, good_cb, hread_cb,
    read_cb, list_cb, release_cb, unload_cb, flush_cb, volume_headers_cb,
    vol_hdr_cb, near_sweep_cb, ray_headers_cb, data_cb, bin_outline_cb,
    DataType_SetColors_CB, bintvls_cb, proj_cb, img_app_cb, geom_cb, alpha_cb,
    img_name_cb, img_cb, stop_cb
};

/* Cartographic projection */
#ifdef PROJ4
#include <proj_api.h>
static projPJ pj;
#endif

/* KML template for positioning result */
static char kml_tmpl[] = 
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

/* Configuration for output images */
static double w_phys;			/* Width of physical (on the ground) area
					   displayed in output image in meters. */
static int w_dpy;			/* Width of image in display units,
					   pixels, points, cm */
static int h_dpy;			/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char img_app[LEN];		/* External application to draw sweeps */

/* Point in device coordinates (pixels) */
struct dvpoint {
    int x, y;
};

/* Convenience functions */
static char *time_stamp(void);
static FILE *vol_open(const char *, pid_t *);
static unsigned new_dchk(void);
static int flush(int);
static void unload(int);

/* Signal handling functions */
static int handle_signals(void);
static void alarm_handler(int);
static void handler(int);

int main(int argc, char *argv[])
{
    cmd = argv[0];
    char *ddir;			/* Working directory for server */
    struct sig_vol *sv_p;	/* Member of vols */
    char *ang_u;		/* Angle unit */
    char *b;			/* Point into buf */
    ssize_t r;			/* Return value from read */
    size_t l;			/* Number of bytes to read from command buffer */
    unsigned dchk;		/* After dchk clients, time some, adjust tmout */
    time_t start = 0, end = 1;	/* When a client session starts, ends */
    double dt;			/* Time taken by a client */
    double dtx = -1.0;		/* Time taken by slowest client in a set */
    int y;			/* Loop index */
    char *dflt_proj[] = { "+proj=aeqd", "+ellps=sphere" };
				/* Defalut projection */

    printf("%s --\nVersion %s. Copyright (c) 2010 Gordon D. Carrie. "
	    "All rights reserved.\n", cmd, SIGMET_VSN);

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "Could not set up signal management.");
	exit(EXIT_FAILURE);
    }

    /* Usage: sigmet_rawd [-n integer] */
    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", cmd);
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
    w_phys = 100000.0;		/* 100,000 meters */
    w_dpy = h_dpy = 600;
    if ( !(pj = pj_init(2, dflt_proj)) ) {
	fprintf(stderr, "Could not set default projection.\n");
	exit(EXIT_FAILURE);
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	if ( !DataType_Add(Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y)) ) {
	    fprintf(stderr, "Could not register data type %s\n",
		    Sigmet_DataType_Abbrv(y));
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
	    fprintf(stderr, "Unknown angle unit %s.\n", ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    /* Create working directory */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "Could not identify daemon directory. Please specify "
		"daemon directory with SIGMET_RAWD_DIR environment variable.\n");
	exit(EXIT_FAILURE);
    }
    if ( chdir(ddir) == -1 ) {
	perror("Could not set working directory.");
	exit(EXIT_FAILURE);
    }

    /* Create named pipe for command input */
    if ( (mkfifo(SIGMET_RAWD_IN, S_IRUSR | S_IWUSR) == -1) ) {
	fprintf(stderr, "Could not create input pipe.\n%s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    tmout = 20;
    dchk = new_dchk();
    printf("Daemon starting. Process id = %d\nClients that block daemon "
	    "for more than %d seconds will be killed.\n", getpid(), tmout);
    if (tmoadj && verbose) {
	printf("Will assess timeout after %d iterations\n", dchk );
    }

    fflush(stdout);

    /* Open command input stream. */
    if ( (i_cmd0 = open(SIGMET_RAWD_IN, O_RDONLY)) == -1 ) {
	fprintf(stderr, "Could not open %s for input.\n%s\n",
		SIGMET_RAWD_IN, strerror(errno));
	exit(EXIT_FAILURE);
    }
    if ( (i_cmd1 = open(SIGMET_RAWD_IN, O_WRONLY)) == -1 ) {
	fprintf(stderr, "Could not open %s for output.\n%s\n",
		SIGMET_RAWD_IN, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Read commands from i_cmd0 into buf and execute them.  */
    while ( (r = read(i_cmd0, &l, sizeof(size_t))) ) {
	int argc1;		/* Number of arguments in received command line */
	char *argv1[ARGCX];	/* Arguments from received command line */
	int a;			/* Index into argv1 */
	char rslt1_nm[LEN];	/* Name of file for standard output */
	char rslt2_nm[LEN];	/* Name of file for error output */
	int success;		/* Result of callback */
	int i;			/* Loop index */
	int tmx = 0;		/* If true, time this session */

	/* Tolerate interrupt in read. Otherwise, bail out */
	if ( r == -1 && errno != EINTR ) {
	    break;
	}

	/* Fetch the command */
	if ( l > BUF_L ) {
	    fprintf(stderr, "%s: Command with %lu bytes to large for buffer.\n",
		    time_stamp(), l);
	    continue;
	}
	if ( (r = read(i_cmd0, buf, l)) != l ) {
	    fprintf(stderr, "%s: Failed to read command of %ld bytes. ",
		    time_stamp(), l);
	    if ( r == -1 ) {
		fprintf(stderr, "%s\n", strerror(errno));
	    }
	    continue;
	}

	/*
	   If imposing timeouts (tmgout is set), set alarm.
	   If adjusting timeouts store start time for this session.
	 */

	if ( tmgout ) {
	    alarm(tmout);
	    if ( tmoadj && --dchk < 8 ) {
		tmx = 1;
		start = time(NULL);
	    } else {
		tmx = 0;
	    }
	}

	/* Break command line into arguments */
	b = buf;
	client_pid = *(pid_t *)b;
	b += sizeof(client_pid);
	argc1 = *(int *)b;
	b += sizeof(argc1);
	if (argc1 > ARGCX) {
	    fprintf(stderr, "%s: Unable to parse command with %d arguments. "
		    "Limit is %d\n", time_stamp(), argc1, ARGCX);
	    continue;
	}
	for (a = 0, argv1[a] = b; b < buf_e && a < argc1; b++) {
	    if ( *b == '\0' ) {
		argv1[++a] = b + 1;
	    }
	}
	if ( b == buf_e ) {
	    fprintf(stderr, "%s: Command line gives no destination.\n",
		    time_stamp());
	    continue;
	}

	/* Open "standard output" file */
	rslt1 = NULL;
	if (snprintf(rslt1_nm, LEN, "%d.1", client_pid) > LEN) {
	    fprintf(stderr, "%s: Could not create file name for client %d.\n",
		    time_stamp(), client_pid);
	    continue;
	}
	if ( !(rslt1 = fopen(rslt1_nm, "w")) ) {
	    fprintf(stderr, "%s: Could not open %s for output.\n%s\n",
		    time_stamp(), rslt1_nm, strerror(errno));
	    continue;
	}

	/* Open error file */
	rslt2 = NULL;
	if (snprintf(rslt2_nm, LEN, "%d.2", client_pid) > LEN) {
	    fprintf(stderr, "%s: Could not create file name for client %d.\n",
		    time_stamp(), client_pid);
	    continue;
	}
	if ( !(rslt2 = fopen(rslt2_nm, "w")) ) {
	    fprintf(stderr, "%s: Could not open %s for output.\n%s\n",
		    time_stamp(), rslt2_nm, strerror(errno));
	    continue;
	}

	/* Identify command */
	cmd1 = argv1[0];
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    fputc(EXIT_FAILURE, rslt2);
	    fprintf(rslt2, "No option or subcommand named \"%s\"\n", cmd1);
	    fprintf(rslt2, "Subcommand must be one of: ");
	    for (i = 0; i < NCMD; i++) {
		fprintf(rslt2, "%s ", cmd1v[i]);
	    }
	    fprintf(rslt2, "\n");
	    if ( (fclose(rslt1) == EOF) ) {
		fprintf(stderr, "%s: Could not close %s.\n%s\n",
			time_stamp(), rslt1_nm, strerror(errno));
	    }
	    if ( (fclose(rslt2) == EOF) ) {
		fprintf(stderr, "%s: Could not close %s.\n%s\n",
			time_stamp(), rslt2_nm, strerror(errno));
	    }
	    continue;
	}

	/* Run command. Callback will send "standard output" to rslt1. */
	success = (cb1v[i])(argc1, argv1);
	if ( (fclose(rslt1) == EOF) ) {
	    fprintf(stderr, "%s: Could not close %s.\n%s\n",
		    time_stamp(), rslt1_nm, strerror(errno));
	}

	/* Send status and error messages, if any */
	if ( success ) {
	    if ( fputc(EXIT_SUCCESS, rslt2) == EOF ) {
		fprintf(stderr, "%s: Could not send return code for %s.\n"
			"%s\n", time_stamp(), cmd1, strerror(errno) );
	    }
	} else {
	    if ( fputc(EXIT_FAILURE, rslt2) == EOF
		    || fprintf(rslt2, "%s failed.\n%s\n",
			cmd1, Err_Get()) == -1 ) {
		fprintf(stderr, "%s: Could not send return code or error "
			"output for %s.\n%s\n",
			time_stamp(), cmd1, strerror(errno) );
	    }
	}
	if ( (fclose(rslt2) == EOF) ) {
	    fprintf(stderr, "%s: Could not close %s.\n%s\n",
		    time_stamp(), rslt2_nm, strerror(errno));
	}

	/*
	   If imposing timeouts, clear alarm.
	   Adjust timeout if desired and necessary.
	 */
	if ( tmgout ) {
	    alarm(0);
	    if ( tmx ) {
		end = time(NULL);
		dt = difftime(end, start);
		if (verbose) {
		    printf("%s: Session %d took ", time_stamp(), dchk);
		    if ( dt == 0.0 ) {
			printf("< 1.0 sec.\n");
		    } else {
			printf("%lf sec.\n", dt);
		    }
		}
		if (dt > dtx) {
		    dtx = dt;
		    if (verbose) {
			printf("%s: Slowest client in this set so "
				"far took ", time_stamp());
			if ( dtx == 0.0 ) {
			    printf("< 1.0 sec.\n");
			} else {
			    printf("%lf sec.\n", dtx);
			}
		    }
		}
		if ( dchk == 0 ) {
		    if ( dtx == 0.0 ) {
			dtx = 1.0;
		    }
		    if ( dtx < tmout / 2 ) {
			tmout = tmout / 2 + 1;
			    printf("%s: Setting timeout to %u sec.\n",
				    time_stamp(), tmout);
		    }
		    dchk = new_dchk();
		    if (verbose) {
			printf("%s: Will assess timeout after %d "
				"iterations\n", time_stamp(), dchk);
		    }
		    dtx = -1.0;
		}
	    }
	}
    }

    /* Should not end up here. Process should exit with "stop" command. */
    fprintf(stderr, "%s: unexpected exit.  %s\n", time_stamp(), strerror(errno));
    if ( close(i_cmd0) == -1 ) {
	fprintf(stderr, "%s: could not close command stream.\n%s\n",
		time_stamp(), strerror(errno));
    }
    if ( unlink(SIGMET_RAWD_IN) == -1 ) {
	fprintf(stderr, "%s: could not delete input pipe.\n", time_stamp());
    }
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
   Daemon times a few clients after serving dchk of them.  This function
   computes a new dchk so that the sampling intervals are random.
 */
static unsigned new_dchk(void)
{
    return 100 + 100 * random() / 0x7fffffff;
}

static int cmd_len_cb(int argc, char *argv[])
{
    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    fprintf(rslt1, "%d\n", BUF_L);
    return 1;
}

static int verbose_cb(int argc, char *argv[])
{
    char *val;

    if (argc != 2) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" true|false");
	return 0;
    }
    val = argv[1];
    if ( strcmp(val, "true") == 0 ) {
	verbose = 1;
    } else if ( strcmp(val, "false") == 0 ) {
	verbose = 0;
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" true|false");
	return 0;
    }
    return 1;
}

static int pid_cb(int argc, char *argv[])
{
    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    fprintf(rslt1, "%d\n", getpid());
    return 1;
}

static int timeout_cb(int argc, char *argv[])
{
    char *a, *m;
    unsigned to;

    if (argc == 1) {
	if (tmgout) {
	    fprintf(rslt1, "timeout = %d sec %s\n",
		    tmout, tmoadj ? "adjustable" : "fixed");
	} else {
	    fprintf(rslt1, "none\n");
	}
	return 1;
    } else if (argc == 2) {
	alarm(0);
	a = argv[1];
	if (strcmp(a, "none") == 0) {
	    tmgout = 0;
	    return 1;
	} else if (sscanf(a, "%d", &tmout) == 1) {
	    tmgout = 1;
	    return 1;
	} else {
	    Err_Append(cmd1);
	    Err_Append(" expected integer or \"none\" for timeout, got ");
	    Err_Append(a);
	    Err_Append(".  ");
	    return 0;
	}
    } else if (argc == 3) {
	alarm(0);
	a = argv[1];
	m = argv[2];
	if (sscanf(a, "%d", &to) != 1) {
	    Err_Append(cmd1);
	    Err_Append(" expected integer or \"none\" for timeout, got ");
	    Err_Append(a);
	    Err_Append(".  ");
	    return 0;
	}
	if (strcmp(m, "adjustable") == 0) {
	    tmoadj = 1;
	} else if (strcmp(m, "fixed") == 0) {
	    tmoadj = 0;
	} else {
	    Err_Append(cmd1);
	    Err_Append(" expected \"adjustable\" or \"fixed\" for modifier, got ");
	    Err_Append(m);
	    Err_Append(".  ");
	    return 0;
	}
	tmgout = 1;
	tmout = to;
	return 1;
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" \"none\"|integer \"adjustable\"|\"fixed\"");
	return 0;
    }

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
	fprintf(rslt1, "%s | %s\n",
		Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return 1;
}

/*
   Open volume file vol_nm, possibly via a pipe.
   Return file handle, or NULL if failure.
   If file is actually a process, pidp gets the child's process id.
   Otherwise, pidp is left alone.
 */
static FILE *vol_open(const char *vol_nm, pid_t *pidp)
{
    FILE *in;		/* Return value */
    pid_t pid = 0;	/* Process identifier, for fork */
    char *sfx;		/* Filename suffix */
    int pfd[2];		/* Pipe for data */

    sfx = strrchr(vol_nm , '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/* If filename ends with ".gz", read from gunzip pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for gzip.  ");
	    return NULL;
	}
	pid = fork();
	switch (pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		return NULL;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( close(i_cmd0) == -1 || close(i_cmd1) == -1
			|| fclose(rslt1) == EOF) {
		    fprintf(stderr, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(stderr, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read gzip process.  ");
		    return NULL;
		} else {
		    *pidp = pid;
		    return in;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/* If filename ends with ".bz2", read from bunzip2 pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for gzip.  ");
	    return NULL;
	}
	pid = fork();
	switch (pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		return NULL;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( close(i_cmd0) == -1 || close(i_cmd1) == -1
			|| fclose(rslt1) == EOF) {
		    fprintf(stderr, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(stderr, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read gzip process.  ");
		    return NULL;
		} else {
		    *pidp = pid;
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
}

static int good_cb(int argc, char *argv[])
{
    char *vol_nm;
    FILE *in;
    int rslt;
    pid_t pid = -1;

    if ( argc != 2 ) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sigmet_volume");
	return 0;
    }
    vol_nm = argv[1];
    if ( !(in = vol_open(vol_nm, &pid)) ) {
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(". ");
	return 0;
    }
    rslt = Sigmet_GoodVol(in);
    if ( pid != -1 ) {
	kill(pid, SIGKILL);
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
    int i;			/* Index into vols */
    struct stat sbuf;   	/* Information about file to read */
    char *vol_nm;		/* Sigmet raw file */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t pid;			/* Decompressing child */
    int n, ntries;		/* Attempt to read volume ntries times */

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
	fprintf(rslt1, "%s loaded%s.\n",
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
    pid = -1;
    if ( !(in = vol_open(vol_nm, &pid)) ) {
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(" for input. ");
	return 0;
    }
    for (n = 0, ntries = 4; n < ntries; n++) {
	switch (Sigmet_ReadHdr(in, &vols[i].vol)) {
	    case READ_OK:
		/* Success. Break out. */
		n = ntries;
		break;
	    case MEM_FAIL:
		/* Try to free some memory and try again */
		if ( flush(1) ) {
		    continue;
		} else {
		    n = ntries;
		}
		break;
	    case INPUT_FAIL:
	    case BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fclose(in);
		if (pid != -1) {
		    kill(pid, SIGKILL);
		}
		unload(i);
		Err_Append("Could not read volume from ");
		Err_Append(vol_nm);
		Err_Append(".\n");
		return 0;
		break;
	}
    }
    fclose(in);
    if (pid != -1) {
	kill(pid, SIGKILL);
    }
    vols[i].oqpd = 1;
    stat(vol_nm, &sbuf);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].users++;
    fprintf(rslt1, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int read_cb(int argc, char *argv[])
{
    struct stat sbuf;   	/* Information about file to read */
    int i;			/* Index into vols */
    char *vol_nm;		/* Sigmet raw file */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t pid;			/* Decompressing child */
    int n, ntries;		/* Attempt to read volume ntries times */

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
	fprintf(rslt1, "%s loaded.\n", vol_nm);
	return 1;
    }

    /* Volume was not loaded, or was freed because truncated. Read volume */
    pid = -1;
    if ( !(in = vol_open(vol_nm, &pid)) ) {
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(" for input. ");
	return 0;
    }
    /* Read volume */
    for (n = 0, ntries = 4; n < ntries; n++) {
	switch (Sigmet_ReadVol(in, &vols[i].vol)) {
	    case READ_OK:
		/* Success. Break out. */
		n = ntries;
		break;
	    case MEM_FAIL:
		/* Try to free some memory and try again */
		if ( flush(1) ) {
		    continue;
		} else {
		    n = ntries;
		}
		break;
	    case INPUT_FAIL:
	    case BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fclose(in);
		if (pid != -1) {
		    kill(pid, SIGKILL);
		}
		unload(i);
		Err_Append("Could not read volume from ");
		Err_Append(vol_nm);
		Err_Append(".\n");
		return 0;
		break;
	}
    }
    fclose(in);
    if (pid != -1) {
	kill(pid, SIGKILL);
    }
    vols[i].oqpd = 1;
    strncpy(vols[i].vol_nm, vol_nm, LEN);
    vols[i].st_dev = sbuf.st_dev;
    vols[i].st_ino = sbuf.st_ino;
    vols[i].users++;
    fprintf(rslt1, "%s loaded%s.\n",
	    vol_nm, vols[i].vol.truncated ? " (truncated)" : "");
    return 1;
}

static int list_cb(int argc, char *argv[])
{
    int i;

    for (i = 0; i < N_VOLS; i++) {
	if ( vols[i].oqpd != 0 ) {
	    fprintf(rslt1, "%s users=%d %s\n",
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

    for (i = h; i + 1 != h; i = ++i % N_VOLS) {
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
    for (i = h; i + 1 != h; i = ++i % N_VOLS) {
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
    Sigmet_PrintHdr(rslt1, &vols[i].vol);
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
    fprintf(rslt1, "site_name=\"%s\"\n", vol.ih.ic.su_site_name);
    fprintf(rslt1, "radar_lon=%.4lf\n",
	   GeogLonR(Sigmet_Bin4Rad(vol.ih.ic.longitude), 0.0) * DEG_PER_RAD);
    fprintf(rslt1, "radar_lat=%.4lf\n",
	    Sigmet_Bin4Rad(vol.ih.ic.latitude) * DEG_PER_RAD);
    fprintf(rslt1, "task_name=\"%s\"\n", vol.ph.pc.task_name);
    fprintf(rslt1, "types=\"");
    fprintf(rslt1, "%s", Sigmet_DataType_Abbrv(vol.types[0]));
    for (y = 1; y < vol.num_types; y++) {
	fprintf(rslt1, " %s", Sigmet_DataType_Abbrv(vol.types[y]));
    }
    fprintf(rslt1, "\"\n");
    fprintf(rslt1, "num_sweeps=%d\n", vol.ih.ic.num_sweeps);
    fprintf(rslt1, "num_rays=%d\n", vol.ih.ic.num_rays);
    fprintf(rslt1, "num_bins=%d\n", vol.ih.tc.tri.num_bins_out);
    fprintf(rslt1, "range_bin0=%d\n", vol.ih.tc.tri.rng_1st_bin);
    fprintf(rslt1, "bin_step=%d\n", vol.ih.tc.tri.step_out);
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
    fprintf(rslt1, "prf=%.2lf\n", prf);
    fprintf(rslt1, "prf_mode=%s\n", mp_s);
    fprintf(rslt1, "vel_ua=%.3lf\n", vel_ua);
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
    fprintf(rslt1, "%d\n", nrst);
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
	    fprintf(rslt1, "sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		Err_Append("Bad ray time.  ");
		return 0;
	    }
	    fprintf(rslt1, "%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    fprintf(rslt1, "az %7.3f %7.3f | ",
		    vol.ray_az0[s][r] * DEG_PER_RAD,
		    vol.ray_az1[s][r] * DEG_PER_RAD);
	    fprintf(rslt1, "tilt %6.3f %6.3f\n",
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
		fprintf(rslt1, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(rslt1, "ray %d: ", r);
		    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
			if (Sigmet_IsData(d)) {
			    fprintf(rslt1, "%f ", d);
			} else {
			    fprintf(rslt1, "nodat ");
			}
		    }
		    fprintf(rslt1, "\n");
		}
	    }
	}
    } else if (s == all && r == all && b == all) {
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    fprintf(rslt1, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		fprintf(rslt1, "ray %d: ", r);
		for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		    if (Sigmet_IsData(d)) {
			fprintf(rslt1, "%f ", d);
		    } else {
			fprintf(rslt1, "nodat ");
		    }
		}
		fprintf(rslt1, "\n");
	    }
	}
    } else if (r == all && b == all) {
	fprintf(rslt1, "%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(rslt1, "ray %d: ", r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(rslt1, "%f ", d);
		} else {
		    fprintf(rslt1, "nodat ");
		}
	    }
	    fprintf(rslt1, "\n");
	}
    } else if (b == all) {
	if (vol.ray_ok[s][r]) {
	    fprintf(rslt1, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(rslt1, "%f ", d);
		} else {
		    fprintf(rslt1, "nodat ");
		}
	    }
	    fprintf(rslt1, "\n");
	}
    } else {
	if (vol.ray_ok[s][r]) {
	    fprintf(rslt1, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
	    if (Sigmet_IsData(d)) {
		fprintf(rslt1, "%f ", d);
	    } else {
		fprintf(rslt1, "nodat ");
	    }
	    fprintf(rslt1, "\n");
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
    fprintf(rslt1, "%f %f %f %f %f %f %f %f\n",
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
		    fprintf(rslt1, "%6d: %3d %5d\n", n, r, b);
		}
	    }
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

/* Specify image geometry */
static int geom_cb(int argc, char *argv[])
{
    char *w_phys_s, *w_dpy_s, *h_dpy_s;

    if ( argc != 4 ) {
	Err_Append("Usage: geom width_phys width_dpy height_dpy");
	return 0;
    }
    w_phys_s = argv[1];
    w_dpy_s = argv[2];
    h_dpy_s = argv[3];
    if ( sscanf(w_phys_s, "%lf", &w_phys) != 1 ) {
	Err_Append("Expected float value for physical width, got ");
	Err_Append(w_phys_s);
	Err_Append(". ");
	return 0;
    }
    if ( sscanf(w_dpy_s, "%d", &w_dpy) != 1 ) {
	Err_Append("Expected integer value for display width, got ");
	Err_Append(w_dpy_s);
	Err_Append(". ");
	return 0;
    }
    if ( sscanf(h_dpy_s, "%d", &h_dpy) != 1 ) {
	Err_Append("Expected integer value for display height, got ");
	Err_Append(h_dpy_s);
	Err_Append(". ");
	return 0;
    }
    return 1;
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
	Err_Append(strerror(errno));
	Err_Append(". ");
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
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */

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
    if ( !Tm_JulToCal(vol.sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	Err_Append("Sweep time garbled. ");
	return 0;
    }
    fprintf(rslt1, "%s_%02d%02d%02d%02d%02d%02.0f_%s_%.1f.png\n",
		vol.ih.ic.hw_site_name, yr, mo, da, h, mi, sec,
		abbrv, vol.sweep_angle[s] * DEG_PER_RAD);

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
    double cnrs_ll[8];		/* Corners of a gate, lon-lat */
    double *ll;			/* Element from cnrs_ll */
    double step;		/* Half width or height of display area. */
    double west, east;		/* Display area longitude limits */
    double south, north;	/* Display area latitude limits */
    projUV cnrs_uv[4];		/* Corners of a gate, lon-lat or x-y */
    projUV *uv;			/* Element from cnrs_uv */
    int n;			/* Loop index */
    int yr, mo, da, h, mi;	/* Sweep year, month, day, hour, minute */
    double sec;			/* Sweep second */
    char img_fl_nm[LEN];	/* Output file */
    size_t img_fl_nm_l;		/* strlen(img_fl_nm) */
    char kml_fl_nm[LEN];	/* Output file */
    FILE *kml_fl;		/* KML file */
    FILE *out = NULL;		/* Where to send outlines to draw */
    pid_t pid = 0;		/* Process identifier, for fork */
    int pfd[2];			/* Pipe for data */
    int errp[2];		/* Pipe for error messages */
    FILE *err = NULL;		/* Output from image process */
    char err_buf[LEN];		/* Store image process output */
    char *e, *e1 = err_buf + LEN; /* Point into err_buf */
    double px_per_m;		/* Display units per map unit */
    struct dvpoint points[4];	/* Corners of a gate in device coordinates */

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
       Longitude, latitude limits of display area.
       Assume w_phys in meters.
       Step half of display width. Convert to radians by multiplying by:
       1 nmi / 1852 meters * 1 deg / 60 nmi * 1 radian / 180 deg
     */
    radar.u = Sigmet_Bin4Rad(vol.ih.ic.longitude);
    radar.v = Sigmet_Bin4Rad(vol.ih.ic.latitude);
    step = w_phys / (2 * 1852.0 * 60.0 * 180.0);
    GeogStep(radar.u, radar.v, 270.0 * RAD_PER_DEG, step, &west, &d);
    GeogStep(radar.u, radar.v, 90.0 * RAD_PER_DEG, step, &east, &d);
    step *= h_dpy / w_dpy;
    GeogStep(radar.u, radar.v, 180.0 * RAD_PER_DEG, step, &d, &south);
    GeogStep(radar.u, radar.v, 0.0, step, &d, &north);
    west *= DEG_PER_RAD;
    east *= DEG_PER_RAD;
    south *= DEG_PER_RAD;
    north *= DEG_PER_RAD;

    /* Limits of display area in projection coordinates */
    radar = pj_fwd(radar, pj);
    if ( radar.u == HUGE_VAL || radar.v == HUGE_VAL ) {
	Err_Append("Could not get map coordinates of radar. ");
	return 0;
    }
    left = radar.u - 0.5 * w_phys;
    rght = radar.u + 0.5 * w_phys;
    top = radar.v + 0.5 * (h_dpy / w_dpy) * w_phys;
    btm = radar.v - 0.5 * (h_dpy / w_dpy) * w_phys;
    px_per_m = w_dpy / w_phys;

    /* Make name of output file */
    if ( !Tm_JulToCal(vol.sweep_time[s], &yr, &mo, &da, &h, &mi, &sec) ) {
	Err_Append("Sweep time garbled. ");
	return 0;
    }
    if ( snprintf(img_fl_nm, LEN, "%s_%02d%02d%02d%02d%02d%02.0f_%s_%.1f.png",
		vol.ih.ic.hw_site_name, yr, mo, da, h, mi, sec,
		abbrv, vol.sweep_angle[s] * DEG_PER_RAD) > LEN ) {
	Err_Append("Could not make image file name. ");
	return 0;
    }

    /* Launch the external drawing application and create a pipe to it. */
    if ( pipe(pfd) == -1 || pipe(errp) == -1 ) {
	Err_Append(strerror(errno));
	Err_Append("\nCould not create pipes for sweep drawing application.  ");
	return 0;
    }
    pid = fork();
    switch (pid) {
	case -1:
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not spawn sweep drawing application.  ");
	    return 0;
	case 0:
	    /*
	       Child.  Close stdout.
	       Write stderr to write side of error pipe.
	       Read polygons from stdin (read side of data pipe).
	     */
	    if ( close(i_cmd0) == -1 || close(i_cmd1) == -1
		    || fclose(rslt1) == EOF) {
		fprintf(stderr, "%s: %s child could not close"
			" server streams", img_app, time_stamp());
		_exit(EXIT_FAILURE);
	    }
	    if ( dup2(pfd[0], STDIN_FILENO) == -1 || close(pfd[1]) == -1 ) {
		fprintf(stderr, "%s: could not set up %s process",
			img_app, time_stamp());
		_exit(EXIT_FAILURE);
	    }
	    if ( dup2(errp[1], STDERR_FILENO) == -1 || close(errp[0]) == -1 ) {
		fprintf(stderr, "%s: could not set up %s process",
			img_app, time_stamp());
		_exit(EXIT_FAILURE);
	    }
	    execl(img_app, img_app, (char *)NULL);
	    _exit(EXIT_FAILURE);
	default:
	    /* This process.  Send polygon to out. Read errors from err */
	    if ( close(pfd[0]) == -1 || !(out = fdopen(pfd[1], "w"))) {
		Err_Append(strerror(errno));
		Err_Append("\nCould not write to drawing application.  ");
		return 0;
	    }
	    if ( close(errp[1]) == -1 || !(err = fdopen(errp[0], "r"))) {
		fclose(out);
		Err_Append(strerror(errno));
		Err_Append("\nCould not write to drawing application.  ");
		return 0;
	    }
    }

    /* Send global information about the image to drawing process */
    img_fl_nm_l = strlen(img_fl_nm);
    if ( fwrite(&img_fl_nm_l, sizeof(img_fl_nm_l), 1, out) != 1
	    || fwrite(img_fl_nm, 1, img_fl_nm_l, out) != img_fl_nm_l ) {
	Err_Append("Could not write image file name. ");
	goto error;
    }
    if ( fwrite(&w_dpy, sizeof(w_dpy), 1, out) != 1
	    ||fwrite(&h_dpy, sizeof(h_dpy), 1, out) != 1 ) {
	Err_Append("Could not write image dimensions. ");
	goto error;
    }
    if ( fwrite(&alpha, sizeof(alpha), 1, out) != 1 ) {
	Err_Append("Could not write alpha value. ");
	goto error;
    }
    if ( fwrite(&n_clrs, 1, sizeof(n_clrs), out) != 1 ) {
	Err_Append("Could not write number of colors. ");
	goto error;
    }
    for (n = 0; n < n_clrs; n++) {
	if ( fwrite(&clrs[n].red, sizeof(unsigned char), 1, out) != 1
		|| fwrite(&clrs[n].green, sizeof(unsigned char), 1, out) != 1
		|| fwrite(&clrs[n].blue, sizeof(unsigned char), 1, out) != 1 ) {
	    Err_Append("Could not write colors. ");
	    goto error;
	}
    }

    /* Determine which interval from bounds each bin value is in. */
    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( vol.ray_ok[s][r] ) {
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
		if ( Sigmet_IsData(d) && (n = BISearch(d, bnds, n_bnds)) != -1 ) {
		    int undef = 0;	/* If true, gate is outside map */
		    size_t npts = 4;	/* Number of vertices */
		    struct dvpoint *pt_p;

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

		    points[0].x = (cnrs_uv[0].u - left) * px_per_m;
		    points[0].y = (top - cnrs_uv[0].v) * px_per_m;
		    points[1].x = (cnrs_uv[1].u - left) * px_per_m;
		    points[1].y = (top - cnrs_uv[1].v) * px_per_m;
		    points[2].x = (cnrs_uv[2].u - left) * px_per_m;
		    points[2].y = (top - cnrs_uv[2].v) * px_per_m;
		    points[3].x = (cnrs_uv[3].u - left) * px_per_m;
		    points[3].y = (top - cnrs_uv[3].v) * px_per_m;
		    if ( fwrite(&n, sizeof(int), 1, out) != 1 ) {
			Err_Append("Could not write polygon color index. ");
			goto error;
		    }
		    if ( fwrite(&npts, sizeof(size_t), 1, out) != 1 ) {
			Err_Append("Could not write polygon point count. ");
			goto error;
		    }
		    for (pt_p = points; pt_p < points + npts; pt_p++) {
			if ( fwrite(&pt_p->x, sizeof(int), 1, out) != 1
				|| fwrite(&pt_p->y, sizeof(int), 1, out) != 1 ) {
			    Err_Append("failed to write bin corner coordinates. ");
			    goto error;
			}
		    }
		}
	    }
	}
    }
    fclose(out);

    /* Get output from image process stderr. 1st character is status */
    memset(err_buf, 0, LEN);
    *err_buf = fgetc(err);
    for (e = err_buf + 1; e < e1; e++) {
	    *e = fgetc(err);
	    if ( *e == EOF ) {
		break;
	    }
    }
    fclose(err);
    if ( *err_buf == EXIT_FAILURE ) {
	Err_Append("Image process failed. ");
	Err_Append(err_buf + 1);
	return 0;
    }

    /* Make kml file and return */
    strcpy(kml_fl_nm, img_fl_nm);
    strcpy(strstr(kml_fl_nm, ".png"), ".kml");
    if ( !(kml_fl = fopen(kml_fl_nm, "w")) ) {
	Err_Append("Could not open ");
	Err_Append(kml_fl_nm);
	Err_Append(" for output. ");
	return 0;
    }
    fprintf(kml_fl, kml_tmpl,
		vol.ih.ic.hw_site_name, vol.ih.ic.hw_site_name,
		yr, mo, da, h, mi, sec,
		abbrv, vol.sweep_angle[s] * DEG_PER_RAD,
		img_fl_nm,
		north, south, west, east);
    fclose(kml_fl);

    fprintf(rslt1, "%s\n", img_fl_nm);
    return 1;
error:
    if (err) {
	fclose(err);
    }
    if (out) {
	fclose(out);
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
    fprintf(stderr, "%s: received stop command, exiting.\n", time_stamp());
    exit(EXIT_SUCCESS);
    return 0;
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
    if ( sigaction(SIGCHLD, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGPIPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    /* Call special handler for alarm signals */
    act.sa_handler = alarm_handler;
    if ( sigaction(SIGALRM, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    /* Generic action for termination signals */
    act.sa_handler = handler;
    if ( sigaction(SIGTERM, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGBUS, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGFPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGILL, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGSEGV, &act, NULL) == -1 ) {
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
   Assume alarm signals are due to a blocking client. Attempt to kill client.
   POSSIBLE BUG. This assumes client_pid identifies a blocking client, and not
   an unrelated process that replaced the (possibly long dead) client.
 */
static void alarm_handler(int signum)
{
    write(STDERR_FILENO, "Client timed out\n", 17);
    kill(client_pid, SIGTERM);
    tmout *= 2;
}

/* For exit signals, print an error message if possible */
static void handler(int signum)
{
    switch (signum) {
	case SIGTERM:
	    write(STDERR_FILENO, "Exiting on termination signal    \n", 34);
	    break;
	case SIGBUS:
	    write(STDERR_FILENO, "Exiting on bus error             \n", 34);
	    break;
	case SIGFPE:
	    write(STDERR_FILENO, "Exiting arithmetic exception     \n", 34);
	    break;
	case SIGILL:
	    write(STDERR_FILENO, "Exiting illegal instruction      \n", 34);
	    break;
	case SIGSEGV:
	    write(STDERR_FILENO, "Exiting invalid memory reference \n", 34);
	    break;
	case SIGSYS:
	    write(STDERR_FILENO, "Exiting on bad system call       \n", 34);
	    break;
	case SIGXCPU:
	    write(STDERR_FILENO, "Exiting: CPU time limit exceeded \n", 34);
	    break;
	case SIGXFSZ:
	    write(STDERR_FILENO, "Exiting: file size limit exceeded\n", 34);
	    break;
    }
    _exit(EXIT_FAILURE);
}
