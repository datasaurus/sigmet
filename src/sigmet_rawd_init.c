/*
 -	sigmet_raw.c --
 -		Command line utility that accesses Sigmet raw volumes.
 -		See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.34 $ $Date: 2010/01/13 23:19:28 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include "alloc.h"
#include "str.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/* subcommand name */
char *cmd1;

/* List of open files.  Child processes should close unneeded files. */
FILE **files;
long nfx;			/* Allocation at files */

/* Bounds limit indicating all possible index values */
#define ALL -1

int main(int argc, char *argv[])
{
    char *cmd;			/* Application name */
    int errno;
    char *in_nm;		/* File with commands */
    FILE *in;			/* Stream from the file named in_nm */
    FILE *out;			/* Unused outptut stream, to prevent process from
				 * exiting while waiting for input. */
    char *ang_u;		/* Angle unit */
    SigmetRaw_Callback *cb;	/* Subcommand to execute */
    char *ln = NULL;		/* Input line from the command file */
    int n;			/* Number of characters in ln */
    int argc1 = 0;		/* Number of arguments in an input line */
    char **argv1 = NULL;	/* Arguments from an input line */
    struct sigaction schld;	/* Signal action to ignore zombies */

    if ( (cmd = strrchr(argv[0], '/')) ) {
	cmd++;
    } else {
	cmd = argv[0];
    }
    if (argc == 1) {
	/* Call is of form: "sigmet_raw" */
	in_nm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw command_file" */
	in_nm = argv[1];
    } else {
	fprintf(stderr, "Usage: %s [command_file]\n", cmd);
	exit(EXIT_FAILURE);
    }
    if (strcmp(in_nm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(in_nm, "r")) || !(out = fopen(in_nm, "w")) ) {
	fprintf(stderr, "%s: Could not open %s for input.\n", cmd, in_nm);
	exit(EXIT_FAILURE);
    }
    if ( !SigmetRaw_AddFile(in) ) {
	fprintf(stderr, "%s: Could not log command stream.\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Catch signals from children to prevent zombies */
    schld.sa_handler = SIG_DFL;
    if ( sigfillset(&schld.sa_mask) == -1 
	    || sigprocmask(SIG_SETMASK, &schld.sa_mask, NULL) == -1 ) {
	perror("Could not initialize signal mask");
	exit(EXIT_FAILURE);
    }
    schld.sa_flags = SA_NOCLDWAIT;
    if ( sigaction(SIGCHLD, &schld, 0) == -1 ) {
	perror("Could not set up signal action for children");
	exit(EXIT_FAILURE);
    }
    if ( sigaction(SIGHUP, &schld, 0) == -1 
	    || sigaction(SIGINT, &schld, 0) == -1 
	    || sigaction(SIGQUIT, &schld, 0) == -1 ) {
	perror("Could not set up signal action for SIGQUIT");
	exit(EXIT_FAILURE);
    }
    if ( sigemptyset(&schld.sa_mask) == -1 
	    || sigprocmask(SIG_SETMASK, &schld.sa_mask, NULL) == -1 ) {
	perror("Could not finish initializing signal mask");
	exit(EXIT_FAILURE);
    }

    /* Initialize the files list */
    errno = 0;
    if ( (nfx = sysconf(_SC_OPEN_MAX)) == -1 && errno != 0 ) {
	perror("Could not get max number of files");
	exit(EXIT_FAILURE);
    }
    if ( !(files = CALLOC(nfx, sizeof(FILE *))) ) {
	fprintf(stderr, "Could not allocate files list.\n");
	exit(EXIT_FAILURE);
    }

    /* Check for angle unit */
    if ((ang_u = getenv("ANGLE_UNIT")) != NULL) {
	if (strcmp(ang_u, "DEGREE") == 0) {
	    SigmetRaw_UseDeg();
	} else if (strcmp(ang_u, "RADIAN") == 0) {
	    SigmetRaw_UseRad();
	} else {
	    fprintf(stderr, "%s: Unknown angle unit %s.\n", cmd, ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    /* Read and execute commands from in */
    while (Str_GetLn(in, '\n', &ln, &n) != 0) {
	if ( (argv1 = Str_Words(ln, argv1, &argc1))
		&& argv1[0] && (strcmp(argv1[0], "#") != 0) ) {
	    cmd1 = argv1[0];
	    if ( (cb = SigmetRaw_Cmd(cmd1)) == NULL) {
		fprintf(stderr, "%s: could not get callback for \"%s\"\n%s\n",
			cmd, cmd1, Err_Get());
	    } else if ( !(cb)(argc1, argv1) ) {
		fprintf(stderr, "%s: %s failed.\n%s\n", cmd, cmd1, Err_Get());
	    }
	}
    }
    if ( feof(in) ) {
	fprintf(stderr, "Exiting. No more input.");
    }
    if ( ferror(in) ) {
	perror("Failure on command stream");
    }
    FREE(ln);
    FREE(argv1);
    SigmetRaw_CleanUp();

    return 0;
}

/* Add a file pointer to the files list.  Return fail if list is full */
int SigmetRaw_AddFile(FILE *file)
{
    FILE **f, **f1;

    for (f = files, f1 = f + nfx; *f && f < f1; f++) {
    }
    if (f == f1) {
	return 0;
    } else {
	*f = file;
	return 1;
    }
}

/* Remove a file pointer from the files list. */
void SigmetRaw_RmFile(FILE *file)
{
    FILE **f, **f1;

    for (f = files, f1 = f + nfx; *f && f < f1; f++) {
	if ( *f == file ) {
	    *f = NULL;
	    return;
	}
    }
}

/* Retrieve the files list. */
FILE ** SigmetRaw_GetFiles(long *n)
{
    *n = nfx;
    return files;
}
