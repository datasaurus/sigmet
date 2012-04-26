/*
   -	sigmet_raw.c --
   -		Command line access to sigmet raw product volumes.
   -		See sigmet_raw (1).
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
   .	$Revision: 1.87 $ $Date: 2012/02/13 19:54:14 $
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/select.h>
#include "alloc.h"
#include "err_msg.h"
#include "strlcpy.h"
#include "geog_lib.h"
#include "sigmet_raw.h"

/*
   Size of various objects
 */

#define SA_UN_SZ (sizeof(struct sockaddr_un))
#define SA_PLEN (sizeof(sa.sun_path))
#define LEN 4096

/*
   Names of fifos that will receive standard output and error output from
   the sigmet_rawd daemon. (These variables are global for cleanup).
 */

static char out_nm[LEN];
static char err_nm[LEN];

/*
   Local functions
 */

static int handle_signals(void);
static void handler(int signum);
static void cleanup(void);
static int daemon_task(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1;
    char *vol_fl_nm = "-";	/* Name of Sigmet raw product file */
    char *sock_nm;		/* Name of socket to communicate with daemon */
    FILE *in;

    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.",
		argv0, getpid());
	return EXIT_FAILURE;
    }
    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", argv0);
	exit(EXIT_FAILURE);
    }
    argv1 = argv[1];

    /*
       Branch according to subcommand - second word of command line.
     */

    if ( strcmp(argv1, "-v") == 0 ) {
	printf("%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
		"All rights reserved.\n", argv[0], SIGMET_VERSION);
	return EXIT_SUCCESS;
    } else if ( strcmp(argv1, "data_types") == 0 ) {
	enum Sigmet_DataTypeN y;

	if ( argc == 2 ) {

	    /*
	       Just list data types from the Sigmet programmers manual.
	     */

	    for (y = 0; y < SIGMET_NTYPES; y++) {
		printf("%s | %s | %s\n", Sigmet_DataType_Abbrv(y),
			Sigmet_DataType_Descr(y), Sigmet_DataType_Unit(y));
	    }
	} else if ( argc == 3 ) {

	    /*
	       If current working directory contains a daemon socket, send this
	       command to it. The daemon might know of data types besides the
	       basic Sigmet ones.
	     */

	    sock_nm = argv[argc - 1];
	    if ( access(sock_nm, F_OK) == 0 ) {
		return daemon_task(argc, argv);
	    }
	} else {
	    fprintf(stderr, "Usage: %s data_types [socket_name]\n", argv0);
	    exit(EXIT_FAILURE);
	}
    } else if ( strcmp(argv1, "load") == 0 ) {
	/*
	   Start a raw volume daemon.
	 */

	if ( argc != 4 ) {
	    fprintf(stderr, "Usage: %s load raw_file socket_name\n", argv0);
	    exit(EXIT_FAILURE);
	}
	vol_fl_nm = argv[2];
	sock_nm = argv[3];
	SigmetRaw_Load(vol_fl_nm, sock_nm);
    } else if ( strcmp(argv1, "good") == 0 ) {
	/*
	   Determine if file given as stdin is navigable Sigmet volume.
	 */

	if ( argc == 2 ) {
	    in = stdin;
	    return Sigmet_Vol_Read(stdin, NULL);
	} else if ( argc == 3 ) {
	    vol_fl_nm = argv[2];
	    if ( strcmp(vol_fl_nm, "-") == 0 ) {
		in = stdin;
	    } else { 
		in = fopen(vol_fl_nm, "r");
	    }
	    if ( !in ) {
		fprintf(stderr, "%s: could not open %s for input\n%s\n",
			argv0, vol_fl_nm, Err_Get());
		exit(EXIT_FAILURE);
	    }
	    return Sigmet_Vol_Read(in, NULL);
	} else {
	    fprintf(stderr, "Usage: %s %s [raw_file]\n", argv0, argv1);
	    exit(EXIT_FAILURE);
	}
    } else {
	return daemon_task(argc, argv);
    }
    return EXIT_SUCCESS;
}

/*
   Send a command to raw product daemon.
 */

static int daemon_task(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *sock_nm;		/* Name of socket to communicate with daemon */
    mode_t m;			/* File permissions */
    struct sockaddr_un sa;	/* Address of socket that connects with
				   daemon */
    int i_dmn = -1;		/* File descriptor associated with sa */
    FILE *dmn;			/* File associated with sa */
    pid_t pid = getpid();	/* Id for this process */
    char buf[LEN];		/* Line sent to or received from daemon */
    char **a;			/* Pointer into argv */
    size_t cmd_ln_l;		/* Command line length */
    int i_out = -1;		/* File descriptor for standard output from
				   daemon*/
    int i_err = -1;		/* File descriptors error output from daemon */
    fd_set set, read_set;	/* Give i_dmn, i_out, and i_err to select */
    int fd_hwm = 0;		/* Highest file descriptor */
    ssize_t ll;			/* Number of bytes read from server */
    int sstatus;		/* Result of callback */

    if ( argc > SIGMET_RAWD_ARGCX ) {
	fprintf(stderr, "%s: cannot parse %d arguments. Maximum argument "
		"count is %d\n", argv0, argc, SIGMET_RAWD_ARGCX);
	goto error;
    }
    *out_nm = '\0';
    *err_nm = '\0';
    atexit(cleanup);

    /*
       Create input and output fifo's in daemon working directory.
     */

    m = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    if ( snprintf(out_nm, LEN, ".%d.1", pid) > LEN ) {
	fprintf(stderr, "%s (%d): could not create name for result pipe.\n",
		argv0, pid);
	goto error;
    }
    if ( mkfifo(out_nm, m) == -1 ) {
	fprintf(stderr, "%s (%d): could not create pipe for standard output\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( snprintf(err_nm, LEN, ".%d.2", pid) > LEN ) {
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
       Connect to daemon via daemon socket
     */

    sock_nm = argv[argc - 1];
    memset(&sa, '\0', SA_UN_SZ);
    sa.sun_family = AF_UNIX;
    strlcpy(sa.sun_path, sock_nm, SA_PLEN);
    if ( (i_dmn = socket(AF_UNIX, SOCK_STREAM, 0)) == -1 ) {
	fprintf(stderr, "%s (%d): could not create socket to connect "
		"with daemon\n%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( connect(i_dmn, (struct sockaddr *)&sa, SA_UN_SZ) == -1
	    || !(dmn = fdopen(i_dmn, "w")) ) {
	fprintf(stderr, "%s (%d): could not connect to daemon at %s\n"
		"Error %d: %s\n",
		argv0, pid, sock_nm, errno, strerror(errno));
	goto error;
    }

    /*
       Send to input socket of volume daemon:
       Id of this process.
       Argument count, excluding socket name.
       Command line length.
       Arguments, excluding socket name.
     */

    argc--;
    for (cmd_ln_l = 0, a = argv; *a; a++) {
	cmd_ln_l += strlen(*a) + 1;
    }
    if ( fwrite(&pid, sizeof(pid_t), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send process id to daemon\n%s\n",
		argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(&argc, sizeof(int), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send argument count to daemon\n"
		"%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    if ( fwrite(&cmd_ln_l, sizeof(size_t), 1, dmn) != 1 ) {
	fprintf(stderr, "%s (%d): could not send command line length to "
		"daemon\n%s\n", argv0, pid, strerror(errno));
	goto error;
    }
    for (a = argv; a < argv + argc; a++) {
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
	fprintf(stderr, "%s (%d): could not open pipe for standard output\n"
		"%s\n", argv0, pid, strerror(errno));
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

	    if ( (ll = read(i_out, buf, LEN)) == -1 ) {
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
		    fprintf(stderr, "%s (%d): failed to write "
			    "standard output.\n", argv0, pid);
		    goto error;
		}
	    }
	} else if ( i_err != -1 && FD_ISSET(i_err, &read_set) ) {
	    /*
	       Daemon has sent error output
	     */

	    if ( (ll = read(i_err, buf, LEN)) == -1 ) {
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
		    fprintf(stderr, "%s (%d): could not close error "
			    "output stream from daemon\n%s\n",
			    argv0, pid, strerror(errno));
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
	       Daemon is done with this command and has sent return status.
	       Clean up and return the status.
	     */

	    if ( (ll = read(i_dmn, &sstatus, sizeof(int))) == -1 || ll == 0 ) {
		fprintf(stderr, "%s (%d): could not get exit status from "
			"daemon\n%s\n", argv0, pid,
			(ll == -1) ? strerror(errno) : "nothing to read");
		goto error;
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
	    unlink(out_nm);
	    unlink(err_nm);
	    return sstatus;
	}
    }

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

static void cleanup(void)
{
    unlink(out_nm);
    unlink(err_nm);
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
   For exit signals, print an error message if possible.
 */

void handler(int signum)
{
    char *msg;
    int status = EXIT_FAILURE;

    msg = "sigmet_raw command exiting                          \n";
    switch (signum) {
	case SIGQUIT:
	    msg = "sigmet_raw command exiting on quit signal           \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGTERM:
	    msg = "sigmet_raw command exiting on termination signal    \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGFPE:
	    msg = "sigmet_raw command exiting arithmetic exception     \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGSYS:
	    msg = "sigmet_raw command exiting on bad system call       \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXCPU:
	    msg = "sigmet_raw command exiting: CPU time limit exceeded \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXFSZ:
	    msg = "sigmet_raw command exiting: file size limit exceeded\n";
	    status = EXIT_FAILURE;
	    break;
    }
    _exit(write(STDERR_FILENO, msg, 53) == 53 ?  status : EXIT_FAILURE);
}
