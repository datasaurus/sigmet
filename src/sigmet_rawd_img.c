/*
 -	sigmet_raw_proj.c --
 -		Manage image configuration in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.5 $ $Date: 2011/01/06 19:50:03 $
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include "sigmet_raw.h"
#include "alloc.h"
#include "err_msg.h"

static unsigned w_pxl = 600;		/* Width of image in display units,
					   pixels, points, cm */
static unsigned h_pxl = 600;		/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char *img_app;			/* External application to draw sweeps */

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

int SigmetRaw_SetImgApp(char *nm)
{
    struct stat sbuf;
    mode_t m = S_IXUSR | S_IXGRP | S_IXOTH;	/* Executable mode */
    static int init;
    size_t sz;

    if ( !init ) {
	atexit(cleanup);
	init = 1;
    }
    if ( stat(nm, &sbuf) == -1 ) {
	Err_Append("Could not get information about ");
	Err_Append(nm);
	Err_Append(" ");
	Err_Append(strerror(errno));
	return SIGMET_BAD_ARG;
    }
    if ( ((sbuf.st_mode & S_IFREG) != S_IFREG) || ((sbuf.st_mode & m) != m) ) {
	Err_Append(nm);
	Err_Append(" is not executable. ");
	return SIGMET_BAD_ARG;
    }
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
