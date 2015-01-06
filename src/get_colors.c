/*
   -	GetColors --
   -		Get colors from color file at path clr_fl_nm.
   -
   .
   .	clr_fl_nm must have format --
   .	number_of_colors bound color bound color ... color bound
   .	
   .	Number of colors must be a positive integer
   .	Colors names are strings with up to COLOR_NM_LEN_S - 1 characters.
   .	Bounds must be float values.
   .	
   .	Number of colors, colors and bounds are placed at num_colors_p,
   .	colors_p, and bnds_p.
   .
   .	If function succeeds, colors_p will receive an array dimensioned
   .	[*num_colors_p][COLOR_NM_LEN_S] with the color names. bnds_p will
   .	receive [*num_colors_p + 1] bounds. Caller must eventually FREE
   .	storage for returned arrays with FREE, viz. FREE(*colors_p),
   .	FREE(bnds_p).

   .	Copyright (c) 2013, Gordon D. Carrie. All rights reserved.
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
   .	$Revision: $ $Date: $
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "alloc.h"
#include "get_colors.h"

/*
   Maximum number of characters allowed in a color name.
   COLOR_NM_LEN_A = storage size
   COLOR_NM_LEN_S = maximum number of non-nul characters.
 */

#define COLOR_NM_LEN_A 64
#define COLOR_NM_LEN_S "63"

int GetColors(char *clr_fl_nm, int *num_colors_p, char ***colors_p,
	float **bnds_p)
{
    FILE *clr_fl = NULL;		/* Input stream for color file */
    int num_colors;			/* Number of colors */
    int num_bnds;			/* Number of boundaries */
    char **colors = NULL;		/* Color names, e.g. "#rrggbb" */
    float *bnds = NULL;			/* Bounds for each color */
    size_t sz;				/* Allocation size */
    int c;				/* Loop index */

    if ( !(clr_fl = fopen(clr_fl_nm, "r")) ) {
	fprintf(stderr, "Could not open %s for reading.\n%s\n",
		clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( fscanf(clr_fl, " %d", &num_colors) != 1 ) {
	fprintf(stderr, "Could not get color count from %s.\n%s\n",
		clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( num_colors < 1) {
	fprintf(stderr, "Must have more than one color.\n%s\n",
		strerror(errno));
	goto error;
    }
    num_bnds = num_colors + 1;
    sz = num_colors * sizeof(char *) + num_colors * COLOR_NM_LEN_A;
    if ( !(colors = MALLOC(sz)) ) {
	fprintf(stderr, "Could not allocate %d colors.\n", num_colors);
	goto error;
    }
    memset(colors, 0, sz);
    colors[0] = (char *)(colors + num_colors);
    for (c = 1; c < num_colors; c++) {
	colors[c] = colors[c - 1] + COLOR_NM_LEN_A;
    }
    if ( !(bnds = CALLOC((size_t)(num_bnds), sizeof(double))) ) {
	fprintf(stderr, "Could not allocate %d color table bounds.\n",
		num_bnds);
	goto error;
    }
    for (c = 0; c < num_colors; c++) {
	if ( fscanf(clr_fl, " %f %" COLOR_NM_LEN_S "s",
		    bnds + c, colors[c]) != 2 ) {
	    fprintf(stderr, "Could not read color and bound at index %d "
		    "from %s.\n", c, clr_fl_nm);
	    goto error;
	}
    }
    if ( fscanf(clr_fl, " %f", bnds + c) != 1 ) {
	fprintf(stderr, "Could not read bound at index %d "
		"from %s.\n", c, clr_fl_nm);
	goto error;
    }
    fclose(clr_fl);
    *num_colors_p = num_colors;
    *colors_p = colors;
    *bnds_p = bnds;
    return 1;

error:
    fclose(clr_fl);
    FREE(colors);
    FREE(bnds);
    return 0;
}
