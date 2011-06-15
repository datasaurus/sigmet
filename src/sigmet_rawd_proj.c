/*
 -	sigmet_raw_proj.c --
 -		Manage the geographic projection used in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.9 $ $Date: 2011/06/09 16:51:15 $
 */

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "alloc.h"
#include "err_msg.h"
#include "sigmet_raw.h"

static char **proj;
static char **inv_proj;
static int init;

static void cleanup(void);
static void cleanup(void)
{
    if ( proj ) {
	FREE(*proj);
    }
    FREE(proj);
    proj = NULL;
    if ( inv_proj ) {
	FREE(*inv_proj);
    }
    FREE(inv_proj);
    inv_proj = NULL;
}

/*
   Set projection to command specified in argv.
   Generate error information with Err_Append and return failure code if
   something goes wrong.
 */

int SigmetRaw_SetProj(int argc, char *argv[])
{
    pid_t pid, p_t;
    int wr;
    int si;
    char **aa, *a, **pp, *p;
    size_t len;

    if ( !init ) {
	atexit(cleanup);
	init = 1;
    }

    /*
       Check viability of command.
     */
    
    if ( (pid = Sigmet_Execvp_Pipe(argv, &wr, NULL)) == -1 ) {
	Err_Append("Could spawn projection command for test. ");
	return SIGMET_BAD_ARG;
    }
    close(wr);
    p_t = waitpid(pid, &si, 0);
    if ( p_t == pid ) {
	if ( WIFEXITED(si) && WEXITSTATUS(si) == EXIT_FAILURE ) {
	    Err_Append("Projection command failed during test. ");
	    return SIGMET_HELPER_FAIL;
	} else if ( WIFSIGNALED(si) ) {
	    Err_Append("Projection command exited on signal during test. ");
	    return SIGMET_HELPER_FAIL;
	}
    } else {
	Err_Append("Could not get exit status for projection command. ");
	if (p_t == -1) {
	    Err_Append(strerror(errno));
	    Err_Append(". ");
	} else {
	    Err_Append("Unknown error. ");
	}
	return SIGMET_HELPER_FAIL;
    }

    /*
       Command can run, at least. Register it.
     */

    if ( proj ) {
	FREE(*proj);
    }
    FREE(proj);
    proj = NULL;
    if ( !(proj = CALLOC(argc + 1, sizeof(char *))) ) {
	Err_Append("Could not allocate space for projection array. ");
	return SIGMET_ALLOC_FAIL;
    }
    for (len = 0, aa = argv; *aa; aa++) {
	len += strlen(*aa) + 1;
    }
    if ( !(*proj = CALLOC(len, 1)) ) {
	FREE(proj);
	proj = NULL;
	Err_Append("Could not allocate space for projection content. ");
	return SIGMET_ALLOC_FAIL;
    }
    for (p = *proj, pp = proj, aa = argv; *aa; aa++) {
	*pp++ = p;
	for (a = *aa; *a; ) {
	    *p++ = *a++;
	}
	*p++ = '\0';
    }
    *pp = (char *)NULL;
    return SIGMET_OK;
}

char **SigmetRaw_GetProj(void)
{
    if ( !proj ) {
	Err_Append("Projection not set. ");
    }
    return proj;
}

/*
   Set inverse projection to command specified in argv.
   Generate error information with Err_Append and return failure code if
   something goes wrong.
 */

int SigmetRaw_SetInvProj(int argc, char *argv[])
{
    pid_t pid, p_t;
    int wr;
    int si;
    char **aa, *a, **pp, *p;
    size_t len;

    if ( !init ) {
	atexit(cleanup);
	init = 1;
    }

    /*
       Check viability of command.
     */
    
    if ( (pid = Sigmet_Execvp_Pipe(argv, &wr, NULL)) == -1 ) {
	Err_Append("Could spawn projection command for test. ");
	return SIGMET_BAD_ARG;
    }
    close(wr);
    p_t = waitpid(pid, &si, 0);
    if ( p_t == pid ) {
	if ( WIFEXITED(si) && WEXITSTATUS(si) == EXIT_FAILURE ) {
	    Err_Append("Projection command failed during test. ");
	    return SIGMET_HELPER_FAIL;
	} else if ( WIFSIGNALED(si) ) {
	    Err_Append("Projection command exited on signal during test. ");
	    return SIGMET_HELPER_FAIL;
	}
    } else {
	Err_Append("Could not get exit status for projection command. ");
	if (p_t == -1) {
	    Err_Append(strerror(errno));
	    Err_Append(". ");
	} else {
	    Err_Append("Unknown error. ");
	}
	return SIGMET_HELPER_FAIL;
    }

    /*
       Command can run, at least. Register it.
     */

    if ( inv_proj ) {
	FREE(*inv_proj);
    }
    FREE(inv_proj);
    inv_proj = NULL;
    if ( !(inv_proj = CALLOC(argc + 1, sizeof(char *))) ) {
	Err_Append("Could not allocate space for projection array. ");
	return SIGMET_ALLOC_FAIL;
    }
    for (len = 0, aa = argv; *aa; aa++) {
	len += strlen(*aa) + 1;
    }
    if ( !(*inv_proj = CALLOC(len, 1)) ) {
	FREE(inv_proj);
	inv_proj = NULL;
	Err_Append("Could not allocate space for projection content. ");
	return SIGMET_ALLOC_FAIL;
    }
    for (p = *inv_proj, pp = inv_proj, aa = argv; *aa; aa++) {
	*pp++ = p;
	for (a = *aa; *a; ) {
	    *p++ = *a++;
	}
	*p++ = '\0';
    }
    *pp = (char *)NULL;
    return SIGMET_OK;
}

char **SigmetRaw_GetInvProj(void)
{
    if ( !inv_proj ) {
	Err_Append("Inverse projection not set. ");
    }
    return inv_proj;
}
