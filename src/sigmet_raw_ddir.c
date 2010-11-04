/*
 -	sigmet_raw_ddir.c --
 -		Manage the daemon working directory.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.1 $ $Date: 2010/11/04 15:59:20 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "alloc.h"
#include "sigmet_raw.h"

#define LEN 4096
static char *ddir;

/*
   Identify and create the daemon working directory.
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
	}
	if ( !(ddir = MALLOC(++l)) ) {
	    fprintf(stderr, "Could not allocate %ld bytes for path to daemon "
		    "working directory.\n", l);
	    exit(EXIT_FAILURE);
	}
	if ( snprintf(ddir, l, "%s/.sigmet_raw", getenv("HOME")) > l ) {
	    fprintf(stderr, "sigmet_raw start: could not create name "
		    "for daemon working directory.\n");
	    exit(EXIT_FAILURE);
	}
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
