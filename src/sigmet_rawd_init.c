/*
 -	sigmet_rawd.c --
 -		Server that accesses Sigmet raw volumes.
 -		See sigmet_rawd (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.87 $ $Date: 2010/01/27 22:16:57 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include "alloc.h"
#include "str.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

static int handle_signals(void);
static void handler(int signum);
static void pipe_handler(int signum);

/* Application and subcommand name */
static char *cmd;
static char *cmd1;

/* If true, send full error messages. */
static int verbose = 1;

/* Size for various strings */
#define LEN 1024

/* Subcommands */
#define NCMD 12
static char *cmd1v[NCMD] = {
    "cmd_len", "verbose", "log", "types", "good", "read", "volume_headers",
    "ray_headers", "data", "bin_outline", "bintvls", "stop"
};
typedef int (callback)(int , char **);
static callback cmd_len_cb;
static callback verbose_cb;
static callback log_cb;
static callback types_cb;
static callback good_cb;
static callback read_cb;
static callback volume_headers_cb;
static callback ray_headers_cb;
static callback data_cb;
static callback bin_outline_cb;
static callback bintvls_cb;
static callback stop_cb;
static callback *cb1v[NCMD] = {
    cmd_len_cb, verbose_cb, log_cb, types_cb, good_cb, read_cb, volume_headers_cb,
    ray_headers_cb, data_cb, bin_outline_cb, bintvls_cb, stop_cb
};

static char *time_stamp(void);
static FILE *vol_open(const char *in_nm, pid_t *pidp);

/* If true, use degrees instead of radians */
static int use_deg = 0;

/* The global Sigmet volume, used by most callbacks. */
static struct Sigmet_Vol vol;	/* Sigmet volume */
static int have_hdr;		/* If true, vol has headers from a Sigmet volume */
static int have_vol;		/* If true, vol has headers and data from a Sigmet
				 * volume */
static char *vol_nm;		/* Name of volume file */
static size_t vol_nm_l;		/* Number of characters that can be stored at
				 * vol_nm */
static void unload(void);	/* Delete and reinitialize contents of vol */

/* Bounds limit indicating all possible index values */
#define ALL -1

/* Files and process streams */
static char ddir[LEN];		/* Working directory for server */
static int i_cmd0;		/* Where to get commands */
static int i_cmd1;		/* Unused outptut (see explanation below). */
static int idlog = -1;		/* Error log */
static char log_nm[LEN];	/* Name of log file */
static FILE *dlog;		/* Error log */
static FILE *rslt1;		/* Where to send standard output */
static FILE *rslt2;		/* Where to send standard error */

/* Input line - has commands for the daemon */
static char *buf;
static long buf_l;		/* Allocation at buf */

/* Shell type determines type of printout */
enum SHELL_TYPE {C, SH};

/* Maximum number of arguments in input command */
#define ARGCX 512

int main(int argc, char *argv[])
{
    int bg = 1;			/* If true, run in foreground (do not fork) */
    enum SHELL_TYPE shtyp = SH;	/* Control how environment variables are printed */
    char cmd_in_nm[LEN];	/* Name of fifo from which to read commands */
    int a;			/* Index into argv */
    pid_t pid;			/* Process id for this process */
    char *ang_u;		/* Angle unit */
    char *b, *b1;		/* Point into buf, end of buf */

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s: could not set up signal management.", argv[0]);
	exit(EXIT_FAILURE);
    }

    if ( (cmd = strrchr(argv[0], '/')) ) {
	cmd++;
    } else {
	cmd = argv[0];
    }

    /* Usage: sigmet_rawd [-c] [-f] */
    for (a = 1; a < argc; a++) {
	if (strcmp(argv[a], "-c") == 0) {
	    shtyp = C;
	} else if (strcmp(argv[a], "-f") == 0) {
	    bg = 0;
	} else {
	    fprintf(stderr, "%s: unknown option \"%s\"\n", cmd, argv[a]);
	    exit(EXIT_FAILURE);
	}
    }

    /* Check for angle unit */
    if ((ang_u = getenv("ANGLE_UNIT")) != NULL) {
	if (strcmp(ang_u, "DEGREE") == 0) {
	    use_deg = 1;
	} else if (strcmp(ang_u, "RADIAN") == 0) {
	    use_deg = 0;
	} else {
	    fprintf(stderr, "%s: Unknown angle unit %s.\n", cmd, ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    /* Initialize globals */
    have_hdr = have_vol = 0;
    Sigmet_InitVol(&vol);
    vol_nm_l = 1;
    if ( !(vol_nm = MALLOC(vol_nm_l)) ) {
	fprintf(stderr, "%s: could not allocate storage for volume file name.\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    *vol_nm = '\0';

    /* Create working directory */
    if ( snprintf(ddir, LEN, "%s/.sigmet_raw", getenv("HOME")) >= LEN ) {
	fprintf(stderr, "%s: could not create name for working directory.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (mkdir(ddir, 0700) == -1) ) {
	fprintf(stderr, "%s: could not create\n%s\n%s\n",
		cmd, ddir, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Create named pipe for command input */
    if ( snprintf(cmd_in_nm, LEN, "%s/%s", ddir, "sigmet.in") >= LEN ) {
	fprintf(stderr, "%s: could not create name for server input pipe.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (mkfifo(cmd_in_nm, 0600) == -1) ) {
	fprintf(stderr, "%s: sigmet_rawd could not create input pipe.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Allocate input line */
    if ( (buf_l = pathconf(cmd_in_nm, _PC_PIPE_BUF)) == -1 ) {
	fprintf(stderr, "%s: Could not get pipe buffer size.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }
    if ( !(buf = CALLOC((size_t)buf_l, 1)) ) {
	fprintf(stderr, "%s: Could not allocate input buffer.\n", cmd);
	exit(EXIT_FAILURE);
    }
    b1 = buf + buf_l;

    /* Open log file */
    if ( snprintf(log_nm, LEN, "%s/%s", ddir, "sigmet.log") >= LEN ) {
	fprintf(stderr, "%s: could not create name for log file.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (idlog = open(log_nm, O_CREAT | O_TRUNC | O_WRONLY, 0600)) == -1
	    || !(dlog = fdopen(idlog, "w")) ) {
	fprintf(stderr, "%s: could not create log file.\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* If desired, put server in background */
    if ( bg ) {
	switch (pid = fork()) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		return 0;
	    case 0:
		/* Child = daemon process. Go to background. */
		break;
	    default:
		/* Parent. Print information and exit. */
		if (shtyp == SH) {
		    printf("SIGMET_RAWD_PID=%d; export SIGMET_RAWD_PID;\n", pid);
		    printf("SIGMET_RAWD_DIR=%s; export SIGMET_RAWD_DIR;\n", ddir);
		    printf("SIGMET_RAWD_IN=%s; export SIGMET_RAWD_IN;\n",
			    cmd_in_nm);
		} else {
		    printf("setenv SIGMET_RAWD_PID %d;\n", pid);
		    printf("setenv SIGMET_RAWD_DIR %s;\n", ddir);
		    printf("setenv SIGMET_RAWD_IN=%s;\n", cmd_in_nm);
		}
		printf("echo Starting sigmet_rawd. Process id = %d.;\n", pid);
		printf("echo Working directory = %s;\n", ddir);
		printf("echo Log file = %s;\n", log_nm);
		exit(EXIT_SUCCESS);
	}
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);
    }

    /* Log initialization */
    pid = getpid();
    fprintf(dlog, "%s: sigmet_rawd started. pid=%d\n", time_stamp(), pid);
    fprintf(dlog, "Set environment as follows:\n");
    if (shtyp == SH) {
	fprintf(dlog, "SIGMET_RAWD_PID=%d; export SIGMET_RAWD_PID;\n", pid);
	fprintf(dlog, "SIGMET_RAWD_DIR=%s; export SIGMET_RAWD_DIR;\n", ddir);
	fprintf(dlog, "SIGMET_RAWD_IN=%s; export SIGMET_RAWD_IN;\n", cmd_in_nm);
    } else {
	fprintf(dlog, "setenv SIGMET_RAWD_PID %d;\n", pid);
	fprintf(dlog, "setenv SIGMET_RAWD_DIR %s;\n", ddir);
	fprintf(dlog, "setenv SIGMET_RAWD_IN=%s;\n", cmd_in_nm);
    }
    fprintf(dlog, "\n");
    fflush(dlog);

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(dlog, "%s: could not set up signal management.", cmd);
	exit(EXIT_FAILURE);
    }

    /* Open command input stream. */
    if ( (i_cmd0 = open(cmd_in_nm, O_RDONLY)) == -1 ) {
	fprintf(dlog, "%s: Could not open %s for input.\n%s\n",
		cmd, cmd_in_nm, strerror(errno));
	exit(EXIT_FAILURE);
    }
    if ( (i_cmd1 = open(cmd_in_nm, O_WRONLY)) == -1 ) {
	fprintf(dlog, "%s: Could not open %s for output.\n%s\n",
		cmd, cmd_in_nm, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /*
       Read command from i_cmd0 into buf.
       Contents of buf: argc argv client_pid
       argc transferred as binary integer.
       argv members and client_pid nul separated.
     */
    while ( read(i_cmd0, buf, buf_l) == buf_l ) {
	int argc1;		/* Number of arguments in an input line */
	char *argv1[ARGCX];	/* Arguments from an input line */
	int a;			/* Index into argv1 */
	char *client_pid;	/* Process id of client, used to make file names */
	char rslt1_nm[LEN];	/* Name of file for standard output */
	char rslt2_nm[LEN];	/* Name of file for error output */
	int status;		/* Result of callback */
	int i;			/* Loop index */

	/* Break input line into arguments */
	argc1 = *(int *)buf;
	if (argc1 > ARGCX) {
	    fprintf(dlog, "%s: Unable to parse command with %d arguments. "
		    "Limit is %d\n", time_stamp(), argc1, ARGCX);
	    continue;
	}
	for (a = 0, argv1[a] = b = buf + sizeof(int); b < b1 && a < argc1; b++) {
	    if ( *b == '\0' ) {
		argv1[++a] = b + 1;
	    }
	}

	/* Use client process id to make argeed upon file names for output. */
	if ( b == b1 ) {
	    fprintf(dlog, "%s: Command line gives no destination.\n",
		    time_stamp());
	    continue;
	}
	client_pid = b;

	/* Standard output */
	rslt1 = NULL;
	if (snprintf(rslt1_nm, LEN, "%s/%s.1", ddir, client_pid) > LEN) {
	    fprintf(dlog, "%s: Could not create file name for client %s.\n",
		    time_stamp(), client_pid);
	    continue;
	}
	if ( !(rslt1 = fopen(rslt1_nm, "w")) ) {
	    fprintf(dlog, "%s: Could not open %s for output.\n%s\n",
		    time_stamp(), rslt1_nm, strerror(errno));
	    continue;
	}

	/* Error output */
	rslt2 = NULL;
	if (snprintf(rslt2_nm, LEN, "%s/%s.2", ddir, client_pid) > LEN) {
	    fprintf(dlog, "%s: Could not create file name for client %s.\n",
		    time_stamp(), client_pid);
	    continue;
	}
	if ( !(rslt2 = fopen(rslt2_nm, "w")) ) {
	    fprintf(dlog, "%s: Could not open %s for output.\n%s\n",
		    time_stamp(), rslt2_nm, strerror(errno));
	    continue;
	}

	/* Identify command */
	cmd1 = argv1[0];
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    fputc(EXIT_FAILURE, rslt2);
	    if ( verbose ) {
		fprintf(rslt2, "No option or subcommand named \"%s\"\n", cmd1);
		fprintf(rslt2, "Subcommand must be one of: ");
		for (i = 0; i < NCMD; i++) {
		    fprintf(rslt2, "%s ", cmd1v[i]);
		}
		fprintf(rslt2, "\n");
	    }
	    if ( (fclose(rslt1) == EOF) ) {
		fprintf(dlog, "%s: Could not close %s.\n%s\n",
			time_stamp(), rslt1_nm, strerror(errno));
	    }
	    if ( (fclose(rslt2) == EOF) ) {
		fprintf(dlog, "%s: Could not close %s.\n%s\n",
			time_stamp(), rslt2_nm, strerror(errno));
	    }
	    continue;
	}

	/* Run command. Send "standard output" to rslt1 */
	status = (cb1v[i])(argc1, argv1);
	if ( (fclose(rslt1) == EOF) ) {
	    fprintf(dlog, "%s: Could not close %s.\n%s\n",
		    time_stamp(), rslt1_nm, strerror(errno));
	}

	/* Send status and error messages, if any, to rslt2 and log */
	if ( status ) {
	    fputc(EXIT_SUCCESS, rslt2);
	} else {
	    fputc(EXIT_FAILURE, rslt2);
	    if ( verbose ) {
		fprintf(rslt2, "%s: %s failed.\n%s\n", cmd, cmd1, Err_Get());
	    }
	    fprintf(dlog, "%s: %s failed.\n%s\n", time_stamp(), cmd1, Err_Get());
	}
	if ( (fclose(rslt2) == EOF) ) {
	    fprintf(dlog, "%s: Could not close %s.\n%s\n",
		    time_stamp(), rslt2_nm, strerror(errno));
	}
    }

    /* Should not end up here */
    fprintf(dlog, "%s: unexpected exit. No more input.\n", time_stamp());
    if ( close(i_cmd0) == -1 ) {
	fprintf(dlog, "%s: could not close command stream.\n%s\n",
		time_stamp(), strerror(errno));
    }
    fclose(dlog);
    unload();
    FREE(vol_nm);
    FREE(buf);

    return 0;
}

static void unload(void)
{
    Sigmet_FreeVol(&vol);
    have_hdr = have_vol = 0;
    vol_nm[0] = '\0';
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
    fprintf(rslt1, "%ld\n", buf_l);
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

static int log_cb(int argc, char *argv[])
{
    FILE *l;
    int c;

    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    if ( !(l = fopen(log_nm, "r")) ) {
	Err_Append("Could not open log file.\n");
	return 0;
    }
    while ( (c = fgetc(l)) != EOF && fputc(c, rslt1) != EOF ) {
	continue;
    }
    if ( ferror(l) || ferror(rslt1) ) {
	Err_Append("Error writing log file.  ");
	Err_Append(strerror(errno));
    }
    if ( fclose(l) == EOF ) {
	Err_Append("Could not close log file.\n");
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
   Open volume file in_nm, possibly via a pipe.
   Return file handle, or NULL if failure.
   If file is from a process, pidp gets the child's process id. Otherwise,
   pidp is not touched.
 */
static FILE *vol_open(const char *in_nm, pid_t *pidp)
{
    FILE *in;		/* Return value */
    pid_t pid = 0;	/* Process identifier, for fork */
    char *sfx;		/* Filename suffix */
    int pfd[2];		/* Pipe for data */

    sfx = strrchr(in_nm , '.');
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
		    fprintf(dlog, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(dlog, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(idlog, STDERR_FILENO) == -1 || close(idlog) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", in_nm, (char *)NULL);
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
		    fprintf(dlog, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(dlog, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(idlog, STDERR_FILENO) == -1 || close(idlog) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", in_nm, (char *)NULL);
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
    } else if ( !(in = fopen(in_nm, "r")) ) {
	/* Uncompressed file */
	Err_Append("Could not open ");
	Err_Append(in_nm);
	Err_Append(" for input.\n");
	return NULL;
    }
    return in;
}

static int good_cb(int argc, char *argv[])
{
    FILE *in;
    int rslt;
    pid_t pid = -1;

    if ( argc != 2 ) {
	return 0;
    }
    if ( !(in = vol_open(argv[1], &pid)) ) {
	return 0;
    }
    rslt = Sigmet_GoodVol(in);
    if ( pid != -1 ) {
	kill(pid, SIGKILL);
    }
    fclose(in);
    return rslt;
}

/*
   Callback for the "read" command.
   Read a volume into memory.
   Usage --
   read raw_file
   read -h raw_file
 */
static int read_cb(int argc, char *argv[])
{
    int hdr_only;
    char *in_nm;
    FILE *in;
    char *t;
    size_t l;
    pid_t pid = 0;
    char *sfx;		/* Filename suffix */
    int pfd[2];		/* Pipe for data */

    hdr_only = 0;
    if ( argc == 2 ) {
	in_nm = argv[1];
    } else if ( argc == 3 && (strcmp(argv[1], "-h") == 0) ) {
	hdr_only = 1;
	in_nm = argv[2];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [-h] sigmet_volume");
	return 0;
    }
    if ( hdr_only && have_hdr && (strcmp(in_nm, vol_nm) == 0) ) {
	/* No need to read headers again */
	return 1;
    }
    if ( have_vol && !vol.truncated && (strcmp(in_nm, vol_nm) == 0) ) {
	/* No need to read volume again */
	return 1;
    }

    sfx = strrchr(in_nm , '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/* If filename ends with ".gz", read from gunzip pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for gzip.  ");
	    return 0;
	}
	pid = fork();
	switch (pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		return 0;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( close(i_cmd0) == -1 || close(i_cmd1) == -1
			|| fclose(rslt1) == EOF) {
		    fprintf(dlog, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(dlog, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(idlog, STDERR_FILENO) == -1 || close(idlog) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", in_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read gzip process.  ");
		    return 0;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/* If filename ends with ".bz2", read from bunzip2 pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for bunzip2.  ");
	    return 0;
	}
	pid = fork();
	switch (pid) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn bunzip2 process.  ");
		return 0;
	    case 0:
		/* Child process - bunzip2.  Send child stdout to pipe. */
		if ( close(i_cmd0) == -1 || close(i_cmd1) == -1
			|| fclose(rslt1) == EOF) {
		    fprintf(dlog, "%s: bunzip2 child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(dlog, "%s: could not set up bunzip2 process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(idlog, STDERR_FILENO) == -1 || close(idlog) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", in_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from bunzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read bunzip2 process.  ");
		    return 0;
		}
	}
    } else if ( !(in = fopen(in_nm, "r")) ) {
	/* Uncompressed file */
	Err_Append("Could not open ");
	Err_Append(in_nm);
	Err_Append(" for input.\n");
	return 0;
    }
    if (hdr_only) {
	/* Read headers */
	unload();
	if ( !Sigmet_ReadHdr(in, &vol) ) {
	    Err_Append("Could not read headers from ");
	    Err_Append(in_nm);
	    Err_Append(".\n");
	    fclose(in);
	    if (pid) {
		kill(pid, SIGKILL);
	    }
	    return 0;
	}
	have_hdr = 1;
	have_vol = 0;
    } else {
	/* Read volume */
	unload();
	if ( !Sigmet_ReadVol(in, &vol) ) {
	    Err_Append("Could not read volume from ");
	    Err_Append(in_nm);
	    Err_Append(".\n");
	    fclose(in);
	    if (pid) {
		kill(pid, SIGKILL);
	    }
	    return 0;
	}
	have_hdr = have_vol = 1;
    }
    fclose(in);
    if (pid) {
	kill(pid, SIGKILL);
    }
    l = 0;
    if ( !(t = Str_Append(vol_nm, &l, &vol_nm_l, in_nm, strlen(in_nm))) ) {
	Err_Append("Could not store name of global volume.  ");
	unload();
	return 0;
    }
    vol_nm = t;
    return 1;
}

static int volume_headers_cb(int argc, char *argv[])
{
    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    Sigmet_PrintHdr(rslt1, vol);
    return 1;
}

static int ray_headers_cb(int argc, char *argv[])
{
    int s, r;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
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
    int s, y, r, b;
    char *abbrv;
    float d;
    enum Sigmet_DataType type;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }

    /*
       Identify input and desired output
       Possible forms:
	   data			(argc = 1)
	   data y		(argc = 2)
	   data y s		(argc = 3)
	   data y s r		(argc = 4)
	   data y s r b		(argc = 5)
     */

    y = s = r = b = ALL;
    type = DB_ERROR;
    if (argc > 1 && (type = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc > 2 && sscanf(argv[2], "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc > 3 && sscanf(argv[3], "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc > 4 && sscanf(argv[4], "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc > 5) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin]");
	return 0;
    }

    if (type != DB_ERROR) {
	/*
	   User has specified a data type.  Search for it in the volume,
	   and set y to the specified type (instead of ALL).
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
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r != ALL && r >= (int)vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }

    /* Write */
    if (y == ALL && s == ALL && r == ALL && b == ALL) {
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
    } else if (s == ALL && r == ALL && b == ALL) {
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
    } else if (r == ALL && b == ALL) {
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
    } else if (b == ALL) {
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
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
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
    Sigmet_FreeVol(&vol);
    c = (use_deg ? DEG_RAD : 1.0);
    fprintf(rslt1, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;
}

/* Usage: sigmet_raw bintvls type s bounds raw_vol */
static int bintvls_cb(int argc, char *argv[])
{
    char *s_s;
    int s, y, r, b;
    char *abbrv;
    double d;
    enum Sigmet_DataType type_t;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep bounds");
	return 0;
    }
    abbrv = argv[1];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
    }
    s_s = argv[2];
    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }

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

    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( !vol.ray_ok[s][r] ) {
	    continue;
	}
	for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
	    d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
	}
	fprintf(rslt1, "\n");
    }

    return 1;
}

static int stop_cb(int argc, char *argv[])
{
    char rm[LEN];

    fclose(dlog);
    unload();
    FREE(vol_nm);
    FREE(buf);
    if (snprintf(rm, LEN, "rm -r %s", ddir) < LEN) {
	system(rm);
    } else {
	fprintf(dlog, "%s: could not delete working directory.\n", time_stamp());
    }
    fprintf(rslt1, "unset SIGMET_RAWD_PID SIGMET_RAWD_DIR SIGMET_RAWD_IN\n");
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

    /* Minimal action for pipes */
    act.sa_handler = pipe_handler;
    act.sa_flags = 0;
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

/* For exit signals, print an error message if possible */
static void handler(int signum)
{
    if ( idlog != -1 ) {
	switch (signum) {
	    case SIGTERM:
		write(idlog, "Exiting on termination signal    \n", 34);
		break;
	    case SIGBUS:
		write(idlog, "Exiting on bus error             \n", 34);
		break;
	    case SIGFPE:
		write(idlog, "Exiting arithmetic exception     \n", 34);
		break;
	    case SIGILL:
		write(idlog, "Exiting illegal instruction      \n", 34);
		break;
	    case SIGSEGV:
		write(idlog, "Exiting invalid memory reference \n", 34);
		break;
	    case SIGSYS:
		write(idlog, "Exiting on bad system call       \n", 34);
		break;
	    case SIGXCPU:
		write(idlog, "Exiting: CPU time limit exceeded \n", 34);
		break;
	    case SIGXFSZ:
		write(idlog, "Exiting: file size limit exceeded\n", 34);
		break;
	}
    }
    _exit(EXIT_FAILURE);
}

/* For pipe errors, report if possible, and keep going */
static void pipe_handler(int signum)
{
    if ( idlog != -1 ) {
	write(idlog, "Received pipe error signal\n", 27);
    }
}
