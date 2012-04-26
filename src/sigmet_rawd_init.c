/*
 -	sigmet_rawd_init.c --
 -		This file defines data structures and functions for a
 -		daemon that reads, manipulates data from, and provides
 -		access to Sigmet raw volumes. See sigmet_raw (1).
 -
   .	Copyright (c) 2011, Gordon D. Carrie. All rights reserved.
   .	
   .	Redistribution and use in source and binary forms, with or without
   .	modification, are permitted provided that the following conditions
   .	are met:
   .	
   .	    * Redistributions of source code must retain the above copyright
   .	    notice, this list of conditions and the following disclaimer.
   .
   .	    * Redistributions in binary form must reproduce the above copyright
   .	    notice, this list of conditions and the following disclaimer in the
   .	    documentation and/or other materials provided with the distribution.
   .	
   .	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   .	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   .	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   .	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   .	HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   .	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   .	TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   .	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   .	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   .	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   .	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.404 $ $Date: 2012/04/26 22:23:21 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include "alloc.h"
#include "strlcpy.h"
#include "err_msg.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/*
   The volume provided by this daemon.
 */

static struct Sigmet_Vol vol;		/* Sigmet volume struct. See sigmet.h */
static int have_vol;			/* If true, vol contains a volume */

/*
   Size for various strings
 */

#define LEN 4096

/*
   Table of commands and functions to create them.
   N_BUCKETS should not be divisible by HASH_X.
 */

#define HASH_X 31
#define N_BUCKETS 200
static unsigned hash(const char *);
struct cmd_entry {
    char *cmd;				/* Command name */
    SigmetRaw_Callback *cb;		/* Callback */
    struct cmd_entry *next;		/* Pointer to next entry in bucket
					 * chain */
};
static struct {
    struct cmd_entry **buckets;		/* Bucket array.  Each element is a
					 * linked list of entries */
    unsigned n_entries;
} commands;
static int init_commands(void);
static SigmetRaw_Callback *get_cmd(char *);
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
   into memory in global struct vol, defined above.  It creates a socket
   for communication with clients. Then it puts this process into the background
   and waits for client requests on the socket.  The daemon executes callbacks
   defined below in response to the client requests. It communicates with
   clients via fifo files. If anything goes wrong, it prints an error message
   to stderr and exits this process. The names for the socket and log files
   are based on vol_nm.
 */

void SigmetRaw_Load(char *vol_fl_nm, char *vol_nm)
{
    FILE *in;			/* Stream that provides a volume */
    int status;			/* Result of a function */
    char *sock_nm;		/* Name of socket to communicate with daemon */
    char *log_nm;		/* Name of log file */
    char *err_nm;		/* Name of error log */
    int flags;			/* Flags for log files */
    struct sockaddr_un sa;	/* Socket to read command and return exit
				   status */
    struct sockaddr *sa_p;	/* &sa or &d_err_sa, for call to bind */
    int i_dmn;			/* File descriptors for daemon socket */
    pid_t pid;			/* Return from fork */
    mode_t m;			/* File mode */
    int i_log;			/* Daemon log */
    int i_err;			/* Daemon error log */
    struct stat buf;
    unsigned int check_intvl = 6;
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
       Identify socket and log files
     */

    sock_nm = vol_nm;
    if ( access(sock_nm, F_OK) == 0 ) {
	fprintf(stderr, "Daemon socket %s already exists. "
		"Is daemon already running?\n", sock_nm);
	exit(SIGMET_IO_FAIL);
    }

    /*
       Read the volume.
     */

    Sigmet_DataType_Init();
    Sigmet_Vol_Init(&vol);
    if ( strcmp(vol_fl_nm, "-") == 0 ) {
	in = stdin;
	vol_fl_nm = "standard input";
    } else if ( !(in = fopen(vol_fl_nm, "r")) ) {
	fprintf(stderr, "Could not open %s for input.\n%s\n",
		vol_fl_nm, Err_Get());
	xstatus = SIGMET_IO_FAIL;
	goto error;
    }
    switch (status = Sigmet_Vol_Read(in, &vol)) {
	case SIGMET_OK:
	case SIGMET_IO_FAIL:	/* Possibly truncated volume o.k. */
	    /*
	       If Sigmet_Vol_Read at least got headers, proceed.
	     */

	    if ( !vol.has_headers ) {
		fprintf(stderr, "Nothing useful in %s.\n%s\n",
			vol_fl_nm, Err_Get());
		xstatus = SIGMET_IO_FAIL;
		goto error;
	    }
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
    have_vol = 1;
    vol.mod = 0;
    sz = strlen(vol_fl_nm) + 1;
    if ( !(vol.raw_fl_nm = MALLOC(sz)) ) {
	fprintf(stderr, "Could not allocate space for volume file name.\n");
	xstatus = SIGMET_ALLOC_FAIL;
	goto error;
    }
    strlcpy(vol.raw_fl_nm, vol_fl_nm, sz);

    /*
       Initialize command table
     */

    if ( (xstatus = init_commands()) != SIGMET_OK ) {
	fprintf(stderr, "Could not initialize command table.\n%s\n", Err_Get());
	goto error;
    }

    /*
       Add commands. Additional "modules" should put calls here.
     */

    if ( (xstatus = SigmetRaw_AddBaseCmds()) != SIGMET_OK ) {
	fprintf(stderr, "Could not add base commands.\n%s\n", Err_Get());
	goto error;
    }

    /*
       Create a socket to recieve client requests.
     */

    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s", sock_nm) >= SA_PLEN ) {
	fprintf(stderr, "Could not fit %s into socket address path.\n",
		sock_nm);
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
       Identify log files.
     */

    if ( !(log_nm = MALLOC(strlen(vol_nm) + strlen(".log") + 1)) ) {
	fprintf(stderr, "Could not allocate memory for log file name.\n");
	exit(SIGMET_IO_FAIL);
    }
    sprintf(log_nm, "%s%s", vol_nm, ".log");
    if ( !(err_nm = MALLOC(strlen(vol_nm) + strlen(".err") + 1)) ) {
	fprintf(stderr, "Could not allocate memory for error log name.\n");
	exit(SIGMET_IO_FAIL);
    }
    sprintf(err_nm, "%s%s", vol_nm, ".err");

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
	       Child continues.
	     */

	    if ( !handle_signals() ) {
		fprintf(stderr, "Could not set up signal management "
			"in daemon.");
		xstatus = SIGMET_NOT_INIT;
		goto error;
	    }

	    break;
	default:
	    /*
	       Parent. Exit. Child will go to background (attach to init).
	     */

	    exit(SIGMET_OK);
    }

    /*
       Create log files. Send stdout and stderr to them. When a client is
       connected, stdout and stderr will go to the client fifos.
     */

    m = S_IRUSR | S_IWUSR | S_IRGRP;
    if ( (i_log = open(log_nm, O_CREAT | O_WRONLY, m)) == -1 ) {
	fprintf(stderr, "Daemon could not open log file: %s\n%s\n",
		log_nm, strerror(errno));
	goto error;
    }
    if ( dup2(i_log, STDOUT_FILENO) == -1 ) {
	fprintf(stderr, "Daemon could not redirect standard output "
		"to log file: %s\n%s\n", log_nm, strerror(errno));
	goto error;
    }
    if ( (i_err = open(err_nm, O_CREAT | O_WRONLY, m)) == -1 ) {
	fprintf(stderr, "Daemon could not open error file: %s\n%s\n",
		err_nm, strerror(errno));
	goto error;
    }
    if ( dup2(i_err, STDERR_FILENO) == -1 ) {
	fprintf(stderr, "Daemon could not redirect standard error "
		"to error file: %s\n%s\n", err_nm, strerror(errno));
	goto error;
    }
    fclose(stdin);

    /*
       Call fork again. Child will become daemon. Parent will watch socket.
       If socket disappears, parent will kill self and daemon.
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

	    break;
	default:
	    /*
	       Parent.
	     */

	    while (1) {
		sleep(check_intvl);
		if ( stat(sock_nm, &buf) == -1 || buf.st_nlink == 0 ) {
		    fprintf(stderr, "%s: daemon exiting. Socket gone.\n",
			    time_stamp());
		    kill(0, SIGTERM);
		    exit(EXIT_SUCCESS);
		}
	    }
    }

    /*
       Wait for clients until "unload" subcommand is received or this process
       is signaled.
     */

    while ( !stop && (cl_io_fd = accept(i_dmn, NULL, 0)) != -1 ) {
	int argc1;		/* Number of arguments in received command
				   line */
	char *argv1[SIGMET_RAWD_ARGCX]; /* Arguments from client command line */
	int a;			/* Index into argv1 */
	char *cmd0;		/* Name of client */
	char *cmd1;		/* Subcommand */
	SigmetRaw_Callback *cb;	/* Callback for subcommand */
	char out_nm[LEN];	/* Fifo to send standard output to client */
	char err_nm[LEN];	/* Fifo to send error output to client */
	FILE *out;		/* Stream for writing to output fifo */
	FILE *err;		/* Stream for writing to error fifo */
	int flags;		/* Return from fcntl, when configuring
				   cl_io_fd */
	int sstatus;		/* Result of callback */
	char *c, *e;		/* Loop parameters */
	void *t;		/* Hold return from realloc */

	/*
	   Close socket stream on fork to any helper application.
	 */

	if ( (flags = fcntl(cl_io_fd, F_GETFD)) == -1
		|| fcntl(cl_io_fd, F_SETFD, flags | FD_CLOEXEC) == -1 ) {
	    fprintf(stderr, "%s: could not set flags on connection to client.\n"
		    "%s\n", time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}

	/*
	   From socket stream, read client process id, client working directory,
	   argument count, command line length, and arguments
	 */

	if ( read(cl_io_fd, &client_pid, sizeof(pid_t)) == -1 ) {
	    fprintf(stderr, "%s: failed to read client process id.\n%s\n",
		    time_stamp(), strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( read(cl_io_fd, &argc1, sizeof(int)) == -1 ) {
	    fprintf(stderr, "%s: failed to read length of command line "
		    "for process %d.\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    close(cl_io_fd);
	    continue;
	}
	if ( argc1 + 1 > SIGMET_RAWD_ARGCX ) {
	    fprintf(stderr, "%s: cannot parse %d command line arguments for "
		    "process %d. Maximum is %d.\n",
		    time_stamp(), argc1, client_pid, SIGMET_RAWD_ARGCX);
	    close(cl_io_fd);
	    continue;
	}
	if ( read(cl_io_fd, &cmd_ln_l, sizeof(size_t)) == -1 ) {
	    fprintf(stderr, "%s: failed to read length of command line "
		    "for process %d.\n", time_stamp(), client_pid);
	    close(cl_io_fd);
	    continue;
	}
	if ( cmd_ln_l > cmd_ln_lx ) {
	    if ( !(t = REALLOC(cmd_ln, cmd_ln_l + 1)) ) {
		fprintf(stderr, "%s: allocation failed for command line of "
			"%lu bytes for process %ld.\n",
			time_stamp(), (unsigned long)cmd_ln_l,
			(long)client_pid);
		close(cl_io_fd);
		continue;
	    }
	    cmd_ln = t;
	    cmd_ln_lx = cmd_ln_l + 1;
	}
	memset(cmd_ln, 0, cmd_ln_lx);
	if ( read(cl_io_fd, cmd_ln, cmd_ln_l) == -1 ) {
	    fprintf(stderr, "%s: failed to read command line for "
		    "process %d.%s\n",
		    time_stamp(), client_pid, strerror(errno));
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
	    fprintf(stderr, "%s: command line garbled for process %d.\n",
		    time_stamp(), client_pid);
	    continue;
	}

	/*
	   Open fifos to client. Client should have already created them.
	   Output fifo is named as process id with ".1" suffix.
	   Error fifo is named as process id with ".2" suffix.
	 */

	if ( snprintf(out_nm, LEN, ".%d.1", client_pid) >= LEN ) {
	    fprintf(stderr, "%s: could not create name for result pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( !(out = fopen(out_nm, "w")) ) {
	    fprintf(stderr, "%s: could not open pipe for standard output for "
		    "process %d\n%s\n",
		    time_stamp(), client_pid, strerror(errno));
	    continue;
	}
	if ( snprintf(err_nm, LEN, ".%d.2", client_pid) >= LEN ) {
	    fprintf(stderr, "%s: could not create name for error pipe for "
		    "process %d.\n", time_stamp(), client_pid);
	    continue;
	}
	if ( !(err = fopen(err_nm, "w")) ) {
	    fprintf(stderr, "%s: could not open pipe for standard error for "
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
		if ( vol.mod ) {
		    fprintf(err, "Volume in memory has been modified.\n"
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
		fprintf(err, "Usage: %s %s [-f] socket\n", cmd0, cmd1);
		sstatus = SIGMET_BAD_ARG;
	    }
	} else if ( !(cb = get_cmd(cmd1)) ) {
	    struct cmd_entry **bp, **bp1, *ep;

	    fprintf(err, "No option or subcommand named %s. "
		    "Subcommand must be one of: ", cmd1);
	    for (bp = commands.buckets, bp1 = bp + N_BUCKETS; bp < bp1; bp++) {
		for (ep = *bp; ep; ep = ep->next) {
		    fprintf(err, "%s ", ep->cmd);
		}
	    }
	    fprintf(err, "\n");
	    sstatus = SIGMET_BAD_ARG;
	} else {
	    sstatus = (cb)(argc1, argv1, &vol, out, err);
	    if ( sstatus != SIGMET_OK ) {
		fprintf(err, "%s\n", Err_Get());
	    }
	}

	/*
	   Callback, if any, is done. Close fifo's.  Client will delete them.
	   Send callback exit status, which will be a SIGMET_* return value
	   defined in sigmet.h, through the daemon socket and close
	   the client side of the socket.
	 */

	fclose(out);
	fclose(err);
	if ( write(cl_io_fd, &sstatus, sizeof(int)) == -1 ) {
	    fprintf(stderr, "%s: could not send return code for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
	if ( close(cl_io_fd) == -1 ) {
	    fprintf(stderr, "%s: could not close socket for %s (%d).\n"
		    "%s\n", time_stamp(), cmd1, client_pid, strerror(errno) );
	}
    }

    /*
       Out of loop. No longer waiting for clients.
     */

    kill(getppid(), SIGTERM);
    unlink(sock_nm);
    FREE(cmd_ln);
    exit(xstatus);

error:
    unlink(sock_nm);
    fprintf(stderr, "%s: Could not spawn sigmet_raw daemon.\n",
	    time_stamp());
    exit(xstatus);
}

/*
   Initialize the command table
 */

static int init_commands(void)
{
    size_t sz;
    struct cmd_entry **bp;		/* Pointer into bucket array */

    commands.n_entries = 0;
    commands.buckets = NULL;
    sz = N_BUCKETS * sizeof(struct cmd_entry *);
    commands.buckets = (struct cmd_entry **)MALLOC(sz);
    if ( !commands.buckets ) {
	Err_Append("Could not allocate memory for hash table.\n");
	return SIGMET_ALLOC_FAIL;
    }
    for (bp = commands.buckets; bp < commands.buckets + N_BUCKETS; bp++) {
	*bp = NULL;
    }
    return SIGMET_OK;
}

/*
   Add command named cmd for callback function cb to command hash table tbl.
   Table tbl should have been initialized.
 */

int SigmetRaw_AddCmd(char *cmd, SigmetRaw_Callback cb)
{
    size_t len;
    struct cmd_entry *ep, *p;
    unsigned b;

    if ( !cmd ) {
	return SIGMET_BAD_ARG;
    }
    b = hash(cmd);
    for (p = commands.buckets[b]; p; p = p->next) {
	if (strcmp(p->cmd, cmd) == 0) {
	    Err_Append(cmd);
	    Err_Append(" command already exists.\n");
	    return SIGMET_BAD_ARG;
	}
    }
    ep = (struct cmd_entry *)MALLOC(sizeof(struct cmd_entry));
    if ( !ep ) {
	Err_Append("Could not allocate memory for new entry in command "
		"hash table.\n");
	return SIGMET_ALLOC_FAIL;
    }
    len = strlen(cmd) + 1;
    ep->cmd = (char *)MALLOC(len);
    if ( !ep->cmd ) {
	Err_Append("Could not allocate memory for new entry in command "
		"hash table.\n");
	FREE(ep);
	return SIGMET_ALLOC_FAIL;
    }
    strlcpy(ep->cmd, cmd, len);
    ep->cb = cb;
    ep->next = commands.buckets[b];
    commands.buckets[b] = ep;
    commands.n_entries++;
    return SIGMET_OK;
}

/*
   Get callback function for a command
 */

static SigmetRaw_Callback *get_cmd(char *cmd)
{
    unsigned b;			/* Index into buckets array */
    struct cmd_entry *ep;	/* Hash entry */

    if ( !cmd ) {
	return 0;
    }
    b = hash(cmd);
    for (ep = commands.buckets[b]; ep; ep = ep->next) {
	if (strcmp(ep->cmd, cmd) == 0) {
	    return ep->cb;
	}
    }
    return NULL;
}

/*
 * hash - compute an index in a hash table given the command name.
 * k = string key (in)
 * n = number of buckets in hash table (in)
 * Return value is a pseudo-random integer in range [0,n)
 */

static unsigned hash(const char *k)
{
    unsigned h;

    for (h = 0 ; *k != '\0'; k++) {
	h = HASH_X * h + (unsigned)*k;
    }
    return h % N_BUCKETS;
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
   For exit signals, print an error message.
 */

void handler(int signum)
{
    char *msg;
    int status = EXIT_FAILURE;

    msg = "sigmet_rawd daemon exiting                          \n";
    switch (signum) {
	case SIGQUIT:
	    msg = "sigmet_rawd daemon exiting on quit signal           \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGTERM:
	    msg = "sigmet_rawd daemon exiting on termination signal    \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGFPE:
	    msg = "sigmet_rawd daemon exiting arithmetic exception     \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGSYS:
	    msg = "sigmet_rawd daemon exiting on bad system call       \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXCPU:
	    msg = "sigmet_rawd daemon exiting: CPU time limit exceeded \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXFSZ:
	    msg = "sigmet_rawd daemon exiting: file size limit exceeded\n";
	    status = EXIT_FAILURE;
	    break;
    }
    _exit(write(STDERR_FILENO, msg, 53) == 53 ?  status : EXIT_FAILURE);
}
