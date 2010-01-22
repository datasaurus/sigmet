/*
 -	sigmet_raw.c --
 -		Client to sigmet_rawd. See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.3 $ $Date: 2010/01/21 21:45:21 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "alloc.h"

/* Array size */
#define LEN 4000

int main(int argc, char *argv[])
{
    char *cmd = argv[0];
    char *ddir;			/* Name of daemon working directory */
    char *cmd_pipe_nm;		/* Name of daemon command pipe */
    int cmd_pipe;		/* Where to send commands */
    char *buf;			/* Output buffer */
    size_t buf_l;		/* Allocation at buf */
    char *b;			/* Point into buf */
    char *b1;			/* End of buf */
    char **aa, *a;		/* Loop parameters */
    ssize_t w;			/* Return from write */
    char rslt_nm[LEN];		/* Where to get results */
    FILE *rslt;			/* Stream from rslt_nm */
    int c;			/* Character from daemon */

    /* Specify where to put the command and get the results */
    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	fprintf(stderr, "%s: SIGMET_RAWD_DIR not set.  Is the daemon running?\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    if ( !(cmd_pipe_nm = getenv("SIGMET_RAWD_IN")) ) {
	fprintf(stderr, "%s: SIGMET_RAWD_IN not set.  Is the daemon running?\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    if ( snprintf(rslt_nm, LEN, "%s/%d.1", ddir, getpid()) > LEN ) {
	fprintf(stderr, "%s: could not create name for result pipe.\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Open command pipe */
    if ( !(cmd_pipe = open(cmd_pipe_nm, O_WRONLY)) ) {
	perror("could not open command pipe");
	exit(EXIT_FAILURE);
    }

    /* Allocate command buffer */
    if ( (buf_l = fpathconf(cmd_pipe, _PC_PIPE_BUF)) == -1 ) {
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
    if ( mkfifo(rslt_nm, 0600) == -1 ) {
	perror("could not create result pipe");
	exit(EXIT_FAILURE);
    }

    /*
       Fill buf and send to daemon.
       Contents of buf: argc argv rslt_nm
       argc sent as binary integer. argv members and rslt_nm nul separated.
     */
    memset(buf, 0, buf_l);
    *(int *)buf = argc - 1;
    for (b = buf + sizeof(int), aa = argv + 1, a = *aa; b < b1 && *aa; b++, a++) {
	*b = *a;
	if (*a == '\0' && *++aa) {
	    a = *aa;
	    *++b = *a;
	}
    }
    if ( b == b1 ) {
	fprintf(stderr, "%s: command line to big (%ld characters max)\n",
		cmd, buf_l - sizeof(int) - strlen(rslt_nm) - 1);
	exit(EXIT_FAILURE);
    }
    for (a = rslt_nm; b < b1 && *a; b++, a++) {
	*b = *a;
    }
    if ( b == b1 ) {
	fprintf(stderr, "%s: command line to big (%ld characters max)\n",
		cmd, buf_l - sizeof(int) - strlen(rslt_nm) - 1);
	exit(EXIT_FAILURE);
    }
    *b = '\0';
    w = write(cmd_pipe, buf, buf_l);
    if ( w != buf_l ) {
	if ( w == -1 ) {
	    perror("could not write command size");
	} else {
	    fprintf(stderr, "%s: Incomplete write to daemon.\n", cmd);
	}
	if ( close(cmd_pipe) == -1 ) {
	    perror("could not close command pipe");
	}
	FREE(buf);
	exit(EXIT_FAILURE);
    }
    if ( close(cmd_pipe) == -1 ) {
	perror("could not close command pipe");
    }

    /* Get result from daemon and send to stdout */
    if ( !(rslt = fopen(rslt_nm, "r")) ) {
	perror("could not open result pipe");
	if ( unlink(rslt_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
	exit(EXIT_FAILURE);
    }
    while ( (c = fgetc(rslt)) != EOF ) {
	putchar(c);
    }
    if ( fclose(rslt) == EOF ) {
	perror("could not close result pipe");
	if ( unlink(rslt_nm) == -1 ) {
	    perror("could not remove result pipe");
	}
    }
    if ( access(rslt_nm, F_OK) == 0 && unlink(rslt_nm) == -1 ) {
	perror("could not remove result pipe");
    }

    return 0;
}
