/*
   -	sigmet_raw.c --
   -		Client to sigmet_rawd. See sigmet_raw (1).
   -
   .	Copyright (c) 2009 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.36 $ $Date: 2010/08/31 15:23:13 $
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

/* Daemon program name */
#define SIGMET_RAWD "sigmet_rawd"

/* Size for various strings */
#define LEN 4096

/* Abbreviations */
#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))

/* Names of fifos that communicate with daemon. (Global for signal handlers) */
static char out_nm[LINE_MAX];	/* Receive standard output from daemon */
static char err_nm[LINE_MAX];	/* Receive error output from daemon */

/* Local functions */
static int handle_signals(void);
static void handler(int signum);

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *ddir;			/* Name of daemon working directory */
    char ddir_t[LEN];		/* Temporary writing space */
    int try, max_tries = 3;	/* Number of times check for existence of ddir */
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
    int status = EXIT_SUCCESS;	/* Return from this process */

    strcpy(out_nm, "");
    strcpy(err_nm, "");

    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", argv0);
	goto error;
    }
    if ( argc > SIGMET_RAWD_ARGCX ) {
	fprintf(stderr, "%s: cannot parse %d arguments. Maximum argument "
		"count is %d\n", argv0, argc, SIGMET_RAWD_ARGCX);
	goto error;
    }

    /* Set up signal handling */
    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.", argv0, pid);
	goto error;
    }

    if ( !getcwd(cwd, LINE_MAX - 1) ) {
	fprintf(stderr, "%s (%d): could not store current working directory\n%s.\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    cwd_l = strlen(cwd);

    /* Identify daemon working directory */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	if ( snprintf(ddir_t, LEN, "%s/.sigmet_raw", getenv("HOME")) > LEN ) {
	    fprintf(stderr, "%s (%d): could not create name for daemon working "
		    "directory.\n", argv0, getpid());
	    exit(EXIT_FAILURE);
	}
	ddir = ddir_t;
    }

    /*
       If subcommand is "start", spawn daemon and spawn the user command (the part
       of the command line after "start").
     */
    if ( strcmp(argv[1], "start") == 0 ) {
	char *argv2;		/* User command to run as a child process */
	pid_t dpid;		/* Id for daemon */
	pid_t upid;		/* Id of user command */
	pid_t cpid;		/* Id of a child process */
	char *c_s;		/* Identify child in printed messages */
	int si;			/* Exit information from a user command */
	int status;		/* Exit status of user command */

	if ( argc < 3 ) {
	    fprintf(stderr, "Usage: %s start command ...\n", argv0);
	    goto error;
	}
	argv2 = argv[2];

	if ( setpgid(0, pid) == -1 ) {
	    fprintf(stderr, "Could not create process group.\n%s\n",
		    strerror(errno));
	    _exit(EXIT_FAILURE);
	}

	/* Create daemon working directory and add to environment */
	m = S_IRUSR | S_IWUSR | S_IXUSR;
	if ( mkdir(ddir, m) == -1 ) {
	    perror("Could not create daemon working directory.");
	    goto error;
	}
	if ( setenv("SIGMET_RAWD_DIR", ddir, 1) == -1 ) {
	    fprintf(stderr, "%s (%d): could not export name for daemon working "
		    "directory.\n", argv0, pid);
	    goto error;
	}

	/* Start the daemon */
	switch (dpid = fork()) {
	    case -1:
		fprintf(stderr, "Could not fork\n%s\n.", strerror(errno));
		break;
	    case 0:
		/* Child process - sigmet_rawd daemon */
		if ( setpgid(0, pid) == -1 ) {
		    fprintf(stderr, "Could not create process group.\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp(SIGMET_RAWD, SIGMET_RAWD, (char *)NULL);
		_exit(EXIT_FAILURE);
	}

	/* Run the user command */
	switch (upid = fork()) {
	    case -1:
		fprintf(stderr, "Could not fork\n%s\n.", strerror(errno));
		break;
	    case 0:
		/* Child process - from command line */
		if ( setpgid(0, pid) == -1 ) {
		    fprintf(stderr, "Could not create process group.\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}

		/* Wait for daemon to make its working directory */
		for (try = 0; try < max_tries; try++) {
		    int d;

		    if ( (d = open(ddir, O_RDONLY)) == -1 ) {
			sleep(1);
		    } else {
			close(d);
			try = max_tries + 1;
		    }
		}
		if ( try == max_tries ) {
		    fprintf(stderr, "%s (%d): could not find daemon working "
			    "directory. Daemon probably failed.\n", argv0, pid);
		    goto error;
		}

		/* Execute the user command */
		execvp(argv2, argv + 2);
		_exit(EXIT_FAILURE);
	}

	/* Wait for a child to exit */
	if ( (cpid = waitpid(0, &si, 0)) == -1 ) {
	    fprintf(stderr, "%s (%d): unable to wait for children.\n%s\n",
		    argv0, pid, strerror(errno));
	    exit(EXIT_FAILURE);
	}
	c_s = ( cpid == dpid ) ? "sigmet daemon" : argv2;
	if ( WIFEXITED(si) ) {
	    status = WEXITSTATUS(si);
	    if ( status == EXIT_SUCCESS ) {
		printf("%s: exit ok\n", c_s);
	    } else {
		printf("%s: failed with exit status %d\n", c_s, WEXITSTATUS(si));
	    }
	} else if ( WIFSIGNALED(si) ) {
	    printf("%s: exited on signal %d\n", c_s, WTERMSIG(si));
	} else {
	    printf("%s: unknown exit.\n", c_s);
	}
	if ( cpid == dpid ) {
	    kill(upid, SIGTERM);
	} else {
	    kill(dpid, SIGTERM);
	}
	printf("sigmet_raw and associated processes are exiting.\n");
	exit(EXIT_SUCCESS);
    }

    /*
       Subcommand is not "start". Daemon should already be running and its
       directory indicated with the SIGMET_RAWD_DIR environment variable. 
       Connect to the daemon and run the subcommand.
     */

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

    /* Connect to daemon via socket in daemon directory */
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    if ( snprintf(sa.sun_path, SA_PLEN, "%s/%s", ddir, SIGMET_RAWD_IN) > SA_PLEN ) {
	fprintf(stderr, "%s (%d): could not fit %s into socket address path.\n",
		argv0, pid, SIGMET_RAWD_IN);
	goto error;
    }
    if ( (i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	fprintf(stderr, "%s (%d): could not create socket to connect with daemon\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( connect(i_dmn, (struct sockaddr *)&sa, SA_UN_SZ) == -1
	    || !(dmn = fdopen(i_dmn, "w")) ) {
	fprintf(stderr, "%s (%d): could not connect to daemon\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }

    /* Determine length of command line */
    for (cmd_ln_l = 0, a = argv; *a; a++) {
	cmd_ln_l += strlen(*a) + 1;	/* Add argument length + 1 (for nul) */
    }

    /* Send command to server */
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
       Get standard output and errors from fifos. Get exit status
       (singe byte 0 or 1) from i_dmn.
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
	    /* Daemon has sent standard output */

	    if ((ll = read(i_out, buf, LINE_MAX)) == -1) {
		fprintf(stderr, "%s (%d): could not get standard output from "
			"daemon\n%s\n", argv0, pid, strerror(errno));
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
			    "stream from dameon\n%s\n",
			    argv0, pid, strerror(errno));
		    goto error;
		}
		i_out = -1;
		fflush(stdout);
	    } else {
		/* Print the standard output */

		if ( (fwrite(buf, 1, ll, stdout)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write standard output.\n",
			    argv0, pid);
		    goto error;
		}
	    }
	} else if ( i_err != -1 && FD_ISSET(i_err, &read_set) ) {
	    /* Daemon has sent error output */

	    if ((ll = read(i_err, buf, LINE_MAX)) == -1) {
		fprintf(stderr, "%s (%d): could not get error output from "
			"daemon\n%s\n", argv0, pid, strerror(errno));
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
			    "from dameon\n%s\n", argv0, pid, strerror(errno));
		    goto error;
		}
		i_err = -1;
		fflush(stderr);
	    } else {
		/* Print the error output */

		if ( (fwrite(buf, 1, ll, stderr)) != ll ) {
		    fprintf(stderr, "%s (%d): failed to write error output.\n",
			    argv0, pid);
		    goto error;
		}
	    }
	} else if ( i_dmn != -1 && FD_ISSET(i_dmn, &read_set) ) {
	    /* Daemon has sent exit status. Clean up and exit */

	    if ( (ll = read(i_dmn, &status, sizeof(int))) == -1 || ll == 0 ) {
		fprintf(stderr, "%s (%d): could not get exit status from "
			"daemon\n%s\n", argv0, pid, strerror(errno));
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
    kill(0, SIGTERM);
    _exit(EXIT_FAILURE);
}
