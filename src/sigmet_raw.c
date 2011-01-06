/*
   -	sigmet_raw.c --
   -		Client to sigmet_rawd. See sigmet_raw (1).
   -
   .	Copyright (c) 2009 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.70 $ $Date: 2011/01/05 17:01:27 $
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include "alloc.h"
#include "sigmet_raw.h"

#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

/*
   Names of fifos that will receive standard output and error output from
   the sigmet_rawd daemon. (These variables are global because signal
   handlers need them)
 */

static char out_nm[LINE_MAX];
static char err_nm[LINE_MAX];

/*
   Local signal handlers.
 */

static int handle_signals(void);
static void handler(int signum);

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *ddir;			/* Name of daemon working directory */
    char *dsock;		/* Name of daemon socket */
    char cwd[LINE_MAX];		/* Current working directory */
    size_t cwd_l;		/* strlen(cwd) */
    struct sockaddr_un sa;	/* Address of socket that connects with daemon */
    int i_dmn = -1;		/* File descriptor associated with sa */
    FILE *dmn;			/* File associated with sa */
    pid_t pid = getpid();	/* Id for this process */
    char buf[LINE_MAX];		/* Line sent to or received from daemon */
    char **a;			/* Pointer into argv */
    size_t cmd_ln_l;		/* Command line length */
    int i_out = -1;		/* File descriptor for standard output from daemon*/
    int i_err = -1;		/* File descriptors error output from daemon */
    mode_t m;			/* File permissions */
    fd_set set, read_set;	/* Give i_dmn, i_out, and i_err to select */
    int fd_hwm = 0;		/* Highest file descriptor */
    ssize_t ll;			/* Number of bytes read from server */
    int sstatus;		/* Result of callback */

    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.", argv0, pid);
	goto error;
    }
    *out_nm = '\0';
    *err_nm = '\0';
    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", argv0);
	goto error;
    }
    if ( strcmp(argv[1], "start") == 0 ) {
	SigmetRaw_Start(argc - 2, argv + 2);
    }
    if ( argc > SIGMET_RAWD_ARGCX ) {
	fprintf(stderr, "%s: cannot parse %d arguments. Maximum argument "
		"count is %d\n", argv0, argc, SIGMET_RAWD_ARGCX);
	goto error;
    }

    /*
       Create input and output fifo's in daemon working directory.
     */

    if ( !(ddir = SigmetRaw_GetDDir()) ) {
	fprintf(stderr, "%s (%d): could not identify daemon working directory.\n",
		argv0, pid);
	exit(EXIT_FAILURE);
    }
    m = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    if ( snprintf(out_nm, LINE_MAX, "%s/%d.1", ddir, pid) > LINE_MAX ) {
	fprintf(stderr, "%s (%d): could not create name for result pipe.\n",
		argv0, pid);
	goto error;
    }
    if ( mkfifo(out_nm, m) == -1 ) {
	fprintf(stderr, "%s (%d): could not create pipe for standard output\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( snprintf(err_nm, LINE_MAX, "%s/%d.2", ddir, pid) > LINE_MAX ) {
	fprintf(stderr, "%s (%d): could not create name for error pipe.\n",
		argv0, pid);
	goto error;
    }
    if ( mkfifo(err_nm, m) == -1 ) {
	fprintf(stderr, "%s (%d): could not create pipe for error messages\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }

    /*
       Connect to daemon via socket in daemon directory
     */

    if ( !(dsock = SigmetRaw_GetSock()) ) {
	fprintf(stderr, "%s (%d): could not identify daemon socket.\n", argv0, pid);
	goto error;
    }
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, dsock, SA_PLEN);
    if ( (i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	fprintf(stderr, "%s (%d): could not create socket to connect with daemon\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( connect(i_dmn, (struct sockaddr *)&sa, SA_UN_SZ) == -1
	    || !(dmn = fdopen(i_dmn, "w")) ) {
	fprintf(stderr, "%s (%d): could not connect to daemon\nError %d: %s\n",
		argv0, pid, errno, strerror(errno));
	goto error;
    }

    /*
       Send server input to socket.  Server intput consists of:
	   Id of this process.
	   Path length of current working directory for this process.
	   Current working directory for this process.
	   Argument count.
	   Command line length.
	   Arguments.
     */

    if ( !getcwd(cwd, LINE_MAX - 1) ) {
	fprintf(stderr, "%s (%d): could not store current working directory\n%s.\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    cwd_l = strlen(cwd);
    for (cmd_ln_l = 0, a = argv; *a; a++) {
	cmd_ln_l += strlen(*a) + 1;
    }
    if ( fwrite(&pid, sizeof(pid_t), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send process id to daemon\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(&cwd_l, sizeof(size_t), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send working directory length to "
		"daemon\n%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(cwd, 1, cwd_l, dmn) != cwd_l ) {
	fprintf(stderr, "%s (%d): could not send working directory name to "
		"daemon\n%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(&argc, sizeof(int), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send argument count to daemon\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(&cmd_ln_l, sizeof(size_t), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send command line length to "
		"daemon\n%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    for (a = argv; *a; a++) {
	size_t a_l;

	a_l = strlen(*a);
	if ( fwrite(*a, 1, a_l, dmn) != a_l || fputc('\0', dmn) == EOF ) {
	    fprintf(stderr, "%s (%d): could not send command to daemon\n%s\n",
		    argv0, pid, strerror(errno));
	    goto error;
	}
    }
    fflush(dmn);

    /*
       Get standard output and errors from fifos.
       Get exit status (single byte 0 or 1) from i_dmn.
     */

    if ( (i_out = open(out_nm, O_RDONLY)) == -1 ) {
	fprintf(stderr, "%s (%d): could not open pipe for standard output\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    if ( (i_err = open(err_nm, O_RDONLY)) == -1 ) {
	fprintf(stderr, "%s (%d): could not open pipe for error messages\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    if ( i_dmn > fd_hwm ) {
	fd_hwm = i_dmn;
    }
    if ( i_out > fd_hwm ) {
	fd_hwm = i_out;
    }
    if ( i_err > fd_hwm ) {
	fd_hwm = i_err;
    }
    FD_ZERO(&set);
    FD_SET(i_dmn, &set);
    FD_SET(i_out, &set);
    FD_SET(i_err, &set);
    while ( 1 ) {
	read_set = set;
	if ( select(fd_hwm + 1, &read_set, NULL, NULL, NULL) == -1 ) {
	    fprintf(stderr, "%s (%d): could not get output from daemon\n%s\n",
		    argv0, pid, strerror(errno));
	    goto error;
	}
	if ( i_out != -1 && FD_ISSET(i_out, &read_set) ) {
	    /*
	       Daemon has sent standard output
	     */

	    if ( (ll = read(i_out, buf, LINE_MAX)) == -1 ) {
		fprintf(stderr, "%s (%d): could not get standard output from "
			"daemon\n%s\n", argv0, pid, strerror(errno));
		goto error;
	    }
	    if ( ll == 0 ) {
		/*
		   Zero bytes read => no more standard output from daemon.
		 */

		FD_CLR(i_out, &set);
		if ( i_out == fd_hwm ) {
		    fd_hwm--;
		}
		if ( close(i_out) == -1 ) {
		    fprintf(stderr, "%s (%d): could not close standard output "
			    "stream from daemon\n%s\n",
			    argv0, pid, strerror(errno));
		    goto error;
		}
		i_out = -1;
		fflush(stdout);
	    } else {
		/*
		   Non-empty standard output from daemon. Forward to stdout
		   for this process.
		 */

		if ( (fwrite(buf, 1, ll, stdout)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write standard output.\n",
			    argv0, pid);
		    goto error;
		}
	    }
	} else if ( i_err != -1 && FD_ISSET(i_err, &read_set) ) {
	    /*
	       Daemon has sent error output
	     */

	    if ( (ll = read(i_err, buf, LINE_MAX)) == -1 ) {
		fprintf(stderr, "%s (%d): could not get error output from "
			"daemon\n%s\n", argv0, pid, strerror(errno));
		goto error;
	    }
	    if ( ll == 0 ) {
		/*
		   Zero bytes read => no more error output from daemon.
		 */

		FD_CLR(i_err, &set);
		if ( i_err == fd_hwm ) {
		    fd_hwm--;
		}
		if ( close(i_err) == -1 ) {
		    fprintf(stderr, "%s (%d): could not close error output stream "
			    "from daemon\n%s\n", argv0, pid, strerror(errno));
		    goto error;
		}
		i_err = -1;
		fflush(stderr);
	    } else {
		/* 
		   Non-empty error output from daemon. Forward to stderr
		   for this process.
		 */

		if ( (fwrite(buf, 1, ll, stderr)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write error output.\n",
			    argv0, pid);
		    goto error;
		}
	    }
	} else if ( i_dmn != -1 && FD_ISSET(i_dmn, &read_set) ) {
	    /*
	       Daemon is done with this command and has send return status.
	       Clean up and return the status as exit status of this process.
	     */

	    if ( (ll = read(i_dmn, &sstatus, sizeof(int))) == -1 || ll == 0 ) {
		fprintf(stderr, "%s (%d): could not get exit status from "
			"daemon\n%s\n", argv0, pid,
			(ll == -1) ? strerror(errno) : "nothing to read");
		goto error;
	    }
	    unlink(out_nm);
	    unlink(err_nm);
	    if ( i_dmn != -1 ) {
		close(i_dmn);
	    }
	    if ( i_out != -1 ) {
		close(i_out);
	    }
	    if ( i_err != -1 ) {
		close(i_err);
	    }
	    exit(sstatus);
	}
    }

    return EXIT_FAILURE;

error:
    if ( strcmp(out_nm, "") != 0 ) {
	unlink(out_nm);
    }
    if ( strcmp(err_nm, "") != 0 ) {
	unlink(err_nm);
    }
    if ( i_dmn != -1 ) {
	close(i_dmn);
    }
    if ( i_out != -1 ) {
	close(i_out);
    }
    if ( i_err != -1 ) {
	close(i_err);
    }
    return EXIT_FAILURE;
}

/*
   Basic signal management.

   Reference --
   Rochkind, Marc J., "Advanced UNIX Programming, Second Edition",
   2004, Addison-Wesley, Boston.
 */

int handle_signals(void)
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
   For exit signals, close fifo's and print an error message if possible.
 */

void handler(int signum)
{
    char *msg;

    unlink(out_nm);
    unlink(err_nm);
    msg = "sigmet_raw command exiting                          \n";
    switch (signum) {
	case SIGTERM:
	    msg = "sigmet_raw command exiting on termination signal    \n";
	    break;
	case SIGBUS:
	    msg = "sigmet_raw command exiting on bus error             \n";
	    break;
	case SIGFPE:
	    msg = "sigmet_raw command exiting arithmetic exception     \n";
	    break;
	case SIGILL:
	    msg = "sigmet_raw command exiting illegal instruction      \n";
	    break;
	case SIGSEGV:
	    msg = "sigmet_raw command exiting invalid memory reference \n";
	    break;
	case SIGSYS:
	    msg = "sigmet_raw command exiting on bad system call       \n";
	    break;
	case SIGXCPU:
	    msg = "sigmet_raw command exiting: CPU time limit exceeded \n";
	    break;
	case SIGXFSZ:
	    msg = "sigmet_raw command exiting: file size limit exceeded\n";
	    break;
    }
    write(STDERR_FILENO, msg, 53);
    _exit(EXIT_FAILURE);
}
