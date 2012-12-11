/*
   -	Sigmet_Proj.c --
   -		This file connects functions in sigmet_raw.c and
   -		sigmet_vol.c to geog_proj functions. It can be
   -		used as a template for connecting to other geographic
   -		conversion interfaces.
   - 
   .	Copyright (c) 2012 Gordon D. Carrie.  All rights reserved.
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
   .	$Revision: 1.1 $ $Date: 2012/12/06 23:46:08 $
 */

#include <math.h>
#include <stdio.h>
#include "geog_lib.h"
#include "geog_proj.h"
#include "sigmet.h"

static int init;
static struct GeogProj proj;

int Sigmet_Proj_Set(char *l)
{
    if ( GeogProjSetFmStr(l, &proj) ) {
	init = 1;
	return 1;
    } else {
	return 0;
    }
}

int Sigmet_Proj_XYTLonLat(double x, double y, double *lon_p, double *lat_p)
{
    if ( !init ) {
	fprintf(stderr, "Sigmet map projection not set.\n");
	return 0;
    }
    return GeogProjXYToLonLat(x, y, lon_p, lat_p, &proj);
}

int Sigmet_Proj_LonLatToXY(double lon, double lat, double *x_p, double *y_p)
{
    if ( !init ) {
	fprintf(stderr, "Sigmet map projection not set.\n");
	return 0;
    }
    return GeogProjLonLatToXY(lon, lat, x_p, y_p, &proj);
}
