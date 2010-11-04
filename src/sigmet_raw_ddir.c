/*
 -	sigmet_raw_ddir.c --
 -		Manage the daemon working directory.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.2 $ $Date: 2010/11/04 16:13:06 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "alloc.h"
#include "sigmet_raw.h"

/* Daemon socket */
static char *dsock;

/* Daemon working directory */
static char *ddir;

/* Default path length */
#define LEN 4096

/*
   Identify and create the daemon working directory.
   Identify but do NOT create daemon socket.
   If something goes wrong, exit the program.
 */

void SigmetRaw_MkDDir(void)
{
    static int init;
    long l;
    mode_t mode;

    if ( init ) {
	return;
    }

    /*
       If SIGMET_RAWD_DIR environment variable is set, use it. Otherwise,
       use a default.
     */

    if ( !(ddir = getenv("SIGMET_RAWD_DIR")) ) {
	errno = 0;
	if ( (l = pathconf("/", _PC_PATH_MAX)) == -1 ) {
	    if ( errno == 0 ) {
		l = LEN;
	    } else {
		fprintf(stderr, "Could not determine maximum path length for "
			"system while setting daemon working directory.\n%s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	    }
	    l++;
	}
	if ( !(ddir = MALLOC(l)) ) {
	    l = LEN;
	    if ( !(ddir = MALLOC(l)) ) {
		fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
			"working directory.\n", l);
		exit(EXIT_FAILURE);
	    }
	}
	if ( snprintf(ddir, l, "%s/.sigmet_raw", getenv("HOME")) > l ) {
	    fprintf(stderr, "Could not create name for daemon working "
		    "directory.\n");
	    exit(EXIT_FAILURE);
	}
    }

    /*
       Create absolute path name for daemon socket.
     */

    if ( !(dsock = MALLOC(l)) ) {
	fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
		"working directory.\n", l);
	exit(EXIT_FAILURE);
    }
    if ( snprintf(dsock, l, "%s/%s", ddir, SIGMET_RAWD_IN ) > l ) {
	fprintf(stderr, "Could not create name for daemon socket.\n");
	exit(EXIT_FAILURE);
    }

    /*
       Create the daemon working directory.
     */

    mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP;
    if ( mkdir(ddir, mode) == -1 && errno != EEXIST) {
	perror("Could not create daemon working directory.");
	exit(EXIT_FAILURE);
    }

    init = 1;
}

/*
   Return path to daemon working directory, or NULL if cannot be determined.
   Caller should not modify return value.
 */

char *SigmetRaw_GetDDir(void)
{
    return ddir ? ddir : getenv("SIGMET_RAWD_DIR");
}

/*
   Return path to daemon socket, or NULL if it cannot be determined.
   Caller should not modify return value.
 */

char *SigmetRaw_GetSock(void)
{
    size_t l;
    char *ddir_t;

    if ( dsock ) {
	return dsock;
    } else if ( (ddir_t = SigmetRaw_GetDDir()) ) {
	l = strlen(ddir_t) + 1 + strlen(SIGMET_RAWD_IN) + 1;
	if ( !(dsock = MALLOC(l)) ) {
	    return NULL;
	}
	if ( snprintf(dsock, l, "%s/%s", ddir, SIGMET_RAWD_IN ) > l ) {
	    return NULL;
	}
	return dsock;
    } else {
	return NULL;
    }
}
