/*
 -	sigmet_raw.c --
 -		Client to sigmet_rawd. See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.17 $ $Date: 2010/02/15 21:45:05 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include "alloc.h"
#include "sigmet_raw.h"

int handle_signals(void);
void handler(int signum);
void pipe_handler(int signum);

/* Array size */
#define LEN 1024

/* Global file names.  Needed in signal handlers. */
char rslt1_nm[LEN];		/* Name of file for standard results */
char rslt2_nm[LEN];		/* Name of file for error results */

int main(int argc, char *argv[])
{
    char *cmd = argv[0];
    pid_t pid = getpid();
    char *ddir;			/* Name of daemon working directory */
    int i_cmd1;			/* Where to send commands */
    char *buf;			/* Output buffer */
    size_t buf_l;		/* Allocation at buf */
    char *b;			/* Point into buf */
    char *b1;			/* End of buf */
    size_t l;			/* Length of command buffer as used */
    char **aa, *a;		/* Loop parameters */
    ssize_t w;			/* Return from write */
    FILE *rslt1;		/* File for standard results */
    FILE *rslt2;		/* File for error results */
    int c;			/* Character from daemon */
    int status;			/* Return from this process */

    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s: could not set up signal management.", argv[0]);
	exit(EXIT_FAILURE);
    }

    /* Specify where to put the command and get the results */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s: SIGMET_RAWD_DIR not set.  Is the daemon running?\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    if ( chdir(ddir) == -1 ) {
	perror("Could not change to daemon working directory.");
	exit(EXIT_FAILURE);
    }
    if ( snprintf(rslt1_nm, LEN, "%d.1", pid) > LEN ) {
	fprintf(stderr, "%s: could not create name for result pipe.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( snprintf(rslt2_nm, LEN, "%d.2", pid) > LEN ) {
	fprintf(stderr, "%s: could not create name for result pipe.\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Open command pipe */
    if ( !(i_cmd1 = open(SIGMET_RAWD_IN, O_WRONLY)) ) {
	perror("could not open command pipe");
	exit(EXIT_FAILURE);
    }

    /* Allocate command buffer */
    if ( (buf_l = fpathconf(i_cmd1, _PC_PIPE_BUF)) == -1 ) {
	fprintf(stderr, "%s: Could not get pipe buffer size.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }
    if ( !(buf = CALLOC(buf_l, 1)) ) {
	fprintf(stderr, "%s: Could not allocate command line.\n", cmd);
	exit(EXIT_FAILURE);
    }
    b1 = buf + buf_l;

    /* Create fifo to receive results from daemon */
    if ( mkfifo(rslt1_nm, 0600) == -1 ) {
	perror("could not create file for standard results");
	exit(EXIT_FAILURE);
    }
    if ( mkfifo(rslt2_nm, 0600) == -1 ) {
	perror("could not create file for standard results");
	exit(EXIT_FAILURE);
    }

    /* Fill buf and send to daemon. */
    memset(buf, 0, buf_l);
    b = buf + sizeof(size_t);
    *(pid_t *)b = pid;
    b += sizeof(pid);
    *(int *)b = argc - 1;
    b += sizeof(argc);
    for (aa = argv + 1, a = *aa; b < b1 && *aa; b++, a++) {
	*b = *a;
	if (*a == '\0' && *++aa) {
	    a = *aa;
	    *++b = *a;
	}
    }
    if ( b == b1 ) {
	fprintf(stderr, "%s: command line too big (%ld characters max)\n",
		cmd, buf_l - sizeof(int) - 1);
	exit(EXIT_FAILURE);
    }
    l = b - buf;
    *(size_t *)buf = l - sizeof(size_t);
    if ( (w = write(i_cmd1, buf, l)) != l ) {
	if ( w == -1 ) {
	    perror("could not write command to sigmet daemon");
	} else {
	    fprintf(stderr, "%s: Incomplete write to daemon.\n", cmd);
	}
	if ( close(i_cmd1) == -1 ) {
	    perror("could not close command pipe");
	}
	FREE(buf);
	exit(EXIT_FAILURE);
    }
    if ( close(i_cmd1) == -1 ) {
	perror("could not close command pipe");
    }

    /* Open files from which to read standard output and error */
    if ( !(rslt1 = fopen(rslt1_nm, "r")) ) {
	perror("could not open result pipe");
	if ( unlink(rslt1_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
	exit(EXIT_FAILURE);
    }
    if ( !(rslt2 = fopen(rslt2_nm, "r")) ) {
	perror("could not open result pipe");
	if ( unlink(rslt2_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
	exit(EXIT_FAILURE);
    }

    /* Get standard output result from daemon and send to stdout */
    while ( (c = fgetc(rslt1)) != EOF ) {
	putchar(c);
    }
    if ( fclose(rslt1) == EOF ) {
	perror("could not close result pipe");
	if ( unlink(rslt1_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
    }
    if ( access(rslt1_nm, F_OK) == 0 && unlink(rslt1_nm) == -1 ) {
	perror("could not remove result pipe");
    }

    /* Get error output from daemon and send to stderr */
    status = fgetc(rslt2);
    while ( (c = fgetc(rslt2)) != EOF ) {
	fputc(c, stderr);
    }
    if ( fclose(rslt2) == EOF ) {
	perror("could not close result pipe");
	if ( unlink(rslt2_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
    }
    if ( access(rslt2_nm, F_OK) == 0 && unlink(rslt2_nm) == -1 ) {
	perror("could not remove result pipe");
    }

    return status;
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
    if ( access(rslt1_nm, F_OK) == 0 && unlink(rslt1_nm) == -1 ) {
	write(STDERR_FILENO, "Could not remove result pipe\n", 29);
    }
    if ( access(rslt2_nm, F_OK) == 0 && unlink(rslt2_nm) == -1 ) {
	write(STDERR_FILENO, "Could not remove result pipe\n", 29);
    }
    _exit(EXIT_FAILURE);
}
