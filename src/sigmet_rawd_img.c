/*
 -	sigmet_raw_proj.c --
 -		Manage image configuration in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.11 $ $Date: 2011/06/09 16:51:57 $
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include "sigmet_raw.h"
#include "alloc.h"
#include "strlcpy.h"
#include "err_msg.h"

static unsigned w_pxl = 600;		/* Width of image in display units,
					   pixels, points, cm */
static unsigned h_pxl = 600;		/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char *img_app;			/* External application to
					   draw sweeps */

static void cleanup(void);
static void cleanup(void)
{
    FREE(img_app);
    img_app = NULL;
}

void SigmetRaw_SetImgSz(unsigned w, unsigned h)
{
    w_pxl = w;
    h_pxl = h;
}

void SigmetRaw_GetImgSz(unsigned *w, unsigned *h)
{
    *w = w_pxl;
    *h = h_pxl;
}

void SigmetRaw_SetImgAlpha(double a)
{
    alpha = a;
}

double SigmetRaw_GetImgAlpha(void)
{
    return alpha;
}

/*
   Set image application to nm.
 */

int SigmetRaw_SetImgApp(char *nm)
{
    char *argv[3];
    pid_t pid, p_t;
    int wr;
    int si;
    static int init;
    size_t sz;

    if ( !init ) {
	atexit(cleanup);
	init = 1;
    }

    /*
       Check viability of nm.
     */
    
    argv[0] = nm;
    argv[1] = ".gdpoly.test";
    argv[2] = NULL;
    if ( (pid = Sigmet_Execvp_Pipe(argv, &wr, NULL)) == -1 ) {
	Err_Append("Could spawn image app for test. ");
	return SIGMET_BAD_ARG;
    }
    close(wr);
    p_t = waitpid(pid, &si, 0);
    if ( p_t == pid ) {
	if ( WIFEXITED(si) && WEXITSTATUS(si) == EXIT_FAILURE ) {
	    Err_Append("Image app failed during test. ");
	    return SIGMET_HELPER_FAIL;
	} else if ( WIFSIGNALED(si) ) {
	    Err_Append("Image app exited on signal during test. ");
	    return SIGMET_HELPER_FAIL;
	}
    } else {
	Err_Append("Could not get exit status for image app. ");
	if (p_t == -1) {
	    Err_Append(strerror(errno));
	    Err_Append(". ");
	} else {
	    Err_Append("Unknown error. ");
	}
	return SIGMET_HELPER_FAIL;
    }
    system("rm .gdpoly.test*");

    /*
       nm works. Register it.
     */

    cleanup();
    sz = strlen(nm) + 1;
    if ( !(img_app = CALLOC(sz, 1)) ) {
	Err_Append("Could not allocate memory for image app name. ");
	return SIGMET_ALLOC_FAIL;
    }
    strlcpy(img_app, nm, sz);
    return SIGMET_OK;
}

char * SigmetRaw_GetImgApp(void)
{
    return img_app;
}
