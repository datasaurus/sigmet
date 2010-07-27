/*
   -	sigmet_raw.c --
   -		Client to sigmet_rawd. See sigmet_raw (1).
   -
   .	Copyright (c) 2009 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.31 $ $Date: 2010/07/27 17:18:51 $
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
#include <sys/select.h>
#include "alloc.h"
#include "sigmet_raw.h"

/* Abbreviations */
#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

/* Length of various strings */
#define LEN 1024

/* Names of fifos that communicate with daemon. (Global for signal handlers) */
static char out_nm[LEN];	/* Receive standard output from daemon */
static char err_nm[LEN];	/* Receive error output from daemon */

/* Local functions */
static int handle_signals(void);
static void handler(int signum);

int main(int argc, char *argv[])
{
    char *cmd = argv[0];
    char *ddir;			/* Name of daemon working directory */
    struct sockaddr_un sa;	/* Address of socket that connects with daemon */
    int i_dmn = -1;		/* File descriptor associated with sa */
    pid_t pid = getpid();
    char buf[LINE_MAX];		/* Line sent to or received from daemon */
    char *b;			/* Point into buf */
    char *b1 = buf + LINE_MAX;	/* End of buf */
    size_t l;			/* Length of command buffer as used */
    char **aa, *a;		/* Loop parameters */
    ssize_t w;			/* Return from write */
    int i_out = -1;		/* File descriptor for standard output from daemon*/
    int i_err = -1;		/* File descriptors error output from daemon */
    mode_t m;			/* Permissions for output fifos */
    fd_set set, read_set;	/* Give i_dmn, i_out, and i_err to select */
    int fd_hwm = 0;		/* Highest file descriptor */
    ssize_t ll;			/* Number of bytes read from server */
    int status = EXIT_SUCCESS;	/* Return from this process */

    strcpy(out_nm, "");
    strcpy(err_nm, "");

    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", cmd);
	goto error;
    }

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.", cmd, pid);
	goto error;
    }

    /* Go to daemon working directory and set up files */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s (%d): SIGMET_RAWD_DIR not set. "
		"Is the daemon running?\n", cmd, pid);
	goto error;
    }
    if ( chdir(ddir) == -1 ) {
	fprintf(stderr, "%s (%d): could not go to daemon working directory.\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }

    /* Make fifos for results and errors from daemon */
    m = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    if ( snprintf(out_nm, LEN, "%d.1", pid) > LEN ) {
	fprintf(stderr, "%s (%d): could not create name for result pipe.\n",
		cmd, pid);
	goto error;
    }
    if ( mkfifo(out_nm, m) == -1 ) {
	fprintf(stderr, "%s (%d): could not create pipe for standard output\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }
    if ( snprintf(err_nm, LEN, "%d.2", pid) > LEN ) {
	fprintf(stderr, "%s (%d): could not create name for error pipe.\n",
		cmd, pid);
	goto error;
    }
    if ( mkfifo(err_nm, m) == -1 ) {
	fprintf(stderr, "%s (%d): could not create pipe for error messages\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }

    /* Connect to daemon via the daemon socket in SIGMET_RAWD_DIR */
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s", SIGMET_RAWD_IN) > SA_PLEN ) {
	fprintf(stderr, "%s (%d): could not fit %s into socket address path.\n",
		cmd, pid, SIGMET_RAWD_IN);
	goto error;
    }
    if ( (i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	fprintf(stderr, "%s (%d): could not create socket\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }
    if ( connect(i_dmn, (struct sockaddr *)&sa, SA_UN_SZ) == -1) {
	fprintf(stderr, "%s (%d): could not connect to daemon\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }

    /* Fill buf and send to daemon. */
    memset(buf, 0, LINE_MAX);
    b = buf + sizeof(size_t);
    *(pid_t *)b = pid;
    b += sizeof(pid);
    *(int *)b = argc;
    b += sizeof(argc);
    for (aa = argv, a = *aa; b < b1 && *aa; b++, a++) {
	*b = *a;
	if (*a == '\0' && *++aa) {
	    a = *aa;
	    *++b = *a;
	}
    }
    if ( b >= b1 ) {
	fprintf(stderr, "%s (%d): command line too big (%lu characters max)\n",
		cmd, pid, (unsigned long)(LINE_MAX - sizeof(int) - 1));
	goto error;
    }
    l = b - buf;
    *(size_t *)buf = l - sizeof(size_t);
    if ( (w = write(i_dmn, buf, l)) != l ) {
	if ( w == -1 ) {
	    fprintf(stderr, "%s (%d): could not send command to daemon\n%s\n",
		    cmd, pid, strerror(errno));
	} else {
	    fprintf(stderr, "%s (%d): incomplete write to daemon.\n", cmd, pid);
	}
	close(i_dmn);
	goto error;
    }

    /*
       Get standard output and errors from fifos. Get exit status
       (singe byte 0 or 1) from i_dmn.
     */

    if ( (i_out = open(out_nm, O_RDONLY)) == -1 ) {
	fprintf(stderr, "%s (%d): could not open pipe for standard output\n%s\n",
		cmd, pid, strerror(errno));
	goto error;
    }
    if ( (i_err = open(err_nm, O_RDONLY)) == -1 ) {
	fprintf(stderr, "%s (%d): could not open pipe for error messages\n%s\n",
		cmd, pid, strerror(errno));
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
		    cmd, pid, strerror(errno));
	    goto error;
	}
	if ( FD_ISSET(i_out, &read_set) ) {
	    /* Daemon has sent standard output */

	    if ((ll = read(i_out, buf, LINE_MAX)) == -1) {
		fprintf(stderr, "%s (%d): could not get standard output from "
			"daemon\n%s\n", cmd, pid, strerror(errno));
		goto error;
	    }
	    if ( ll == 0 ) {
		/* No more standard output */

		FD_CLR(i_out, &set);
		if ( i_out == fd_hwm ) {
		    fd_hwm--;
		}
		if ( close(i_out) == -1 ) {
		    fprintf(stderr, "%s (%d): could not close standard output "
			    "stream from dameon\n%s\n", cmd, pid, strerror(errno));
		    goto error;
		}
		i_out = -1;
		fflush(stdout);
	    } else {
		/* Print the standard output */

		if ( (fwrite(buf, 1, ll, stdout)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write standard output.\n",
			    cmd, pid);
		    goto error;
		}
	    }
	} else if ( FD_ISSET(i_err, &read_set) ) {
	    /* Daemon has sent error output */

	    if ((ll = read(i_err, buf, LINE_MAX)) == -1) {
		fprintf(stderr, "%s (%d): could not get error output from "
			"daemon\n%s\n", cmd, pid, strerror(errno));
		goto error;
	    }
	    if ( ll == 0 ) {
		/* No more error output */

		FD_CLR(i_err, &set);
		if ( i_err == fd_hwm ) {
		    fd_hwm--;
		}
		if ( close(i_err) == -1 ) {
		    fprintf(stderr, "%s (%d): could not close error output stream "
			    "from dameon\n%s\n", cmd, pid, strerror(errno));
		    goto error;
		}
		i_err = -1;
		fflush(stderr);
	    } else {
		/* Print the error output */

		if ( (fwrite(buf, 1, ll, stderr)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write error output.\n",
			    cmd, pid);
		    goto error;
		}
	    }
	} else if ( FD_ISSET(i_dmn, &read_set) ) {
	    /* Daemon has sent exit status. Clean up and exit */

	    if ( (ll = read(i_dmn, &status, sizeof(int))) == -1 || ll == 0 ) {
		fprintf(stderr, "%s (%d): could not get exit status from "
			"daemon\n%s\n", cmd, pid, strerror(errno));
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
	    exit(status);
	}
    }

    /* Unreachable */
    return status;

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
void handler(int signum)
{
    switch (signum) {
	case SIGKILL:
	    write(STDERR_FILENO, "Exiting on kill signal           \n", 34);
	    break;
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
    unlink(out_nm);
    unlink(err_nm);
    _exit(EXIT_FAILURE);
}
