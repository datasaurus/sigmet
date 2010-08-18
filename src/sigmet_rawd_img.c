/*
 -	sigmet_raw_proj.c --
 -		Manage image configuration in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: $ $Date: $
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include "sigmet_raw.h"
#include "err_msg.h"

#define LEN 4096

static unsigned w_pxl = 600;		/* Width of image in display units,
					   pixels, points, cm */
static unsigned h_pxl = 600;		/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char img_app[LEN] = {'\0'};	/* External application to draw sweeps */

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

int SigmetRaw_SetImgApp(char *img_app_nm)
{
    struct stat sbuf;
    mode_t m = S_IXUSR | S_IXGRP | S_IXOTH;	/* Executable mode */

    if ( stat(img_app_nm, &sbuf) == -1 ) {
	Err_Append("Could not get information about ");
	Err_Append(img_app_nm);
	Err_Append(" ");
	Err_Append(strerror(errno));
	return 0;
    }
    if ( ((sbuf.st_mode & S_IFREG) != S_IFREG) || ((sbuf.st_mode & m) != m) ) {
	Err_Append(img_app_nm);
	Err_Append(" is not executable. ");
	return 0;
    }
    if ( snprintf(img_app, LEN, "%s", img_app_nm) > LEN ) {
	Err_Append("Name of image application too long. ");
	return 0;
    }
    return 1;
}

char * SigmetRaw_GetImgApp(void)
{
    return img_app;
}
