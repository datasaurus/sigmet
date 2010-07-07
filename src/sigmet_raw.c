/*
 -	sigmet_raw.c --
 -		Client to sigmet_rawd. See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.22 $ $Date: 2010/07/02 22:15:47 $
 */

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
#include "alloc.h"
#include "sigmet_raw.h"

static int handle_signals(void);
static void handler(int signum);
static void alarm_handler(int signum);

/* Array size */
#define LEN 1024

/* Global file names.  Needed in signal handlers. */
char rslt1_nm[LEN];		/* Name of file for standard results */
char rslt2_nm[LEN];		/* Name of file for error results */

int main(int argc, char *argv[])
{
    char *cmd = argv[0];
    char *tmout_s;		/* Process time limit, string */
    unsigned tmout;		/* Process time limit */
    char *ddir;			/* Name of daemon working directory */
    struct sockaddr_un io_sa;	/* Socket for "stdio" */
    struct sockaddr *sa_p;	/* &io_sa, needed for call to bind */
    int io_fd;			/* File descriptor for "stdio" */
    FILE *io;			/* "stdio" */
    struct sockaddr_un err_sa;	/* Socket for "stderr" */
    int err_fd;			/* File descriptor for "stderr" */
    FILE *err;			/* "stderr" */
    pid_t pid = getpid();
    char buf[SIGMET_RAWD_ARGVX];/* Output buffer */
    char *b;			/* Point into buf */
    char *b1;			/* End of buf */
    size_t l;			/* Length of command buffer as used */
    char **aa, *a;		/* Loop parameters */
    ssize_t w;			/* Return from write */
    int c;			/* Character from daemon */
    int status;			/* Return from this process */

    b1 = buf + SIGMET_RAWD_ARGVX;
    status = EXIT_SUCCESS;

    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* To avoid haning server, exit if take too long */
    if ( (tmout_s = getenv("SIGMET_RAWD_TIMEOUT")) ) {
	if (strcmp(tmout_s, "none") != 0 && sscanf(tmout_s, "%u", &tmout) == 1) {
	    alarm(tmout);
	} else {
	    fprintf(stderr, "%s: timeout must be integer or \"none\".\n", cmd);
	    exit(EXIT_FAILURE);
	}
    } else {
	alarm(30);
    }

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s: could not set up signal management.", cmd);
	exit(EXIT_FAILURE);
    }

    /* Connect to daemon */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s: SIGMET_RAWD_DIR not set.  Is the daemon running?\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    if ( chdir(ddir) == -1 ) {
	perror("Could not change to daemon working directory.");
	exit(EXIT_FAILURE);
    }
    if ( (io_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	perror("Could not connect to sigmet_raw daemon.");
	exit(EXIT_FAILURE);
    }
    memset(&io_sa, '\0', sizeof(struct sockaddr_un));
    io_sa.sun_family = AF_UNIX;
    strncpy(io_sa.sun_path, SIGMET_RAWD_IN, sizeof(io_sa.sun_path) - 1);
    sa_p = (struct sockaddr *)&io_sa;
    connect(io_fd, sa_p, sizeof(struct sockaddr_un));
    if ( !(io = fdopen(io_fd, "r+")) ) {
	perror("Could not connect to sigmet_raw daemon.");
	exit(EXIT_FAILURE);
    }

    /* Fill buf and send to daemon. */
    memset(buf, 0, SIGMET_RAWD_ARGVX);
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
    if ( b >= b1 ) {
	fprintf(stderr, "%s: command line too big (%lu characters max)\n",
		cmd, (unsigned long)(SIGMET_RAWD_ARGVX - sizeof(int) - 1));
	exit(EXIT_FAILURE);
    }
    l = b - buf;
    *(size_t *)buf = l - sizeof(size_t);
    if ( (w = write(io_fd, buf, l)) != l ) {
	if ( w == -1 ) {
	    perror("could not write command to sigmet daemon");
	} else {
	    fprintf(stderr, "%s: Incomplete write to daemon.\n", cmd);
	}
	fclose(io);
	exit(EXIT_FAILURE);
    }

    /* Get standard output result from daemon and send to stdout */
    while ( (c = fgetc(io)) != EOF ) {
	putchar(c);
    }
    fclose(io);

    /* Get exit status and error messages from daemon */
    if ( (err_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	perror("Could not connect to sigmet_raw daemon.");
	exit(EXIT_FAILURE);
    }
    memset(&err_sa, '\0', sizeof(struct sockaddr_un));
    err_sa.sun_family = AF_UNIX;
    if (snprintf(err_sa.sun_path, sizeof(err_sa.sun_path), "%d.err", pid)
	    > sizeof(err_sa.sun_path)) {
	perror("Could not create error stream");
	exit(EXIT_FAILURE);
    }
    sa_p = (struct sockaddr *)&err_sa;
    while (connect(err_fd, sa_p, sizeof(struct sockaddr_un)) == -1) {
	if ( errno == ENOENT ) {
	    sleep(1);
	    continue;
	} else {
	    perror("Could not connect to error stream");
	    exit(EXIT_FAILURE);
	}
    }
    if ( !(err = fdopen(err_fd, "r")) ) {
	perror("Could not connect to error stream");
	exit(EXIT_FAILURE);
    }
    while ( (c = fgetc(err)) != EOF ) {
	fputc(c, stderr);
    }
    fclose(err);

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

/* Exit on alarm */
static void alarm_handler(int signum)
{
    write(STDERR_FILENO, "sigmet_raw timed out\n", 21);
    exit(EXIT_FAILURE);
}
