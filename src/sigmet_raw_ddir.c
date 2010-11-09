/*
 -	sigmet_raw_ddir.c --
 -		Manage the daemon working directory.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.6 $ $Date: 2010/11/08 19:47:49 $
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

void cleanup(void)
{
    FREE(dsock);
    FREE(ddir);
}

/*
   Identify and create the daemon working directory.
   Identify but do NOT create daemon socket.
   If something goes wrong, exit the program.
 */

void SigmetRaw_MkDDir(void)
{
    static int init;
    size_t l;
    mode_t mode;
    char *ddir_e = getenv("SIGMET_RAWD_DIR");
    char *hdir = getenv("HOME");

    if ( init ) {
	return;
    }
    if ( !hdir ) {
	fprintf(stderr, "HOME not set.\n");
	exit(EXIT_FAILURE);
    }

    /*
       If SIGMET_RAWD_DIR environment variable is set, use it. Otherwise,
       set daemon working directory to a default.
     */

    if ( ddir_e ) {
	l = strlen(ddir_e) + 1;
	if ( !(ddir = MALLOC(l)) ) {
	    fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
		    "working directory.\n", l);
	    exit(EXIT_FAILURE);
	}
	if ( snprintf(ddir, l, "%s", ddir_e) > l ) {
	    perror("Could not create name for daemon working directory.");
	    exit(EXIT_FAILURE);
	}
    } else {
	l = strlen(hdir) + strlen("/.sigmet_raw") + 1;
	if ( !(ddir = MALLOC(l)) ) {
	    fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
		    "working directory.\n", l);
	    exit(EXIT_FAILURE);
	}
	if ( snprintf(ddir, l, "%s/.sigmet_raw", hdir) > l ) {
	    perror("Could not create name for daemon working directory.");
	    exit(EXIT_FAILURE);
	}
    }

    /*
       Create absolute path name for daemon socket.
     */

    l = strlen(ddir) + strlen("/") + strlen(SIGMET_RAWD_IN) + 1;
    if ( !(dsock = MALLOC(l)) ) {
	fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
		"working directory.\n", l);
	exit(EXIT_FAILURE);
    }
    if ( snprintf(dsock, l, "%s/%s", ddir, SIGMET_RAWD_IN ) > l ) {
	perror("Could not create name for daemon socket.");
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
    atexit(cleanup);
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
	if ( snprintf(dsock, l, "%s/%s", ddir_t, SIGMET_RAWD_IN ) > l ) {
	    return NULL;
	}
	return dsock;
    } else {
	return NULL;
    }
}
