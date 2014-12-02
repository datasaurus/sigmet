/*
   -	hsv_app.c --
   -		This application prints rgb values for a sequence of hues.
   -	
   .	Copyright (c) 2011, Gordon D. Carrie. All rights reserved.
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
   .	$Revision: 1.8 $ $Date: 2012/11/08 21:06:29 $
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "hsv_lib.h"

int main(int argc, char *argv[])
{
    char *cmd = argv[0];
    char *h0_s, *h1_s, *n_s;		/* Command line arguments for start hue,
					   end hue, and number of colors */
    char *s_s = NULL, *v_s = NULL;	/* Command line arguments for optional
					   saturation and value */
    double h0, h1, dh;			/* Initial hue, final hue, hue
					   increment */
    double h, s = 1.0, v = 1.0;		/* Hue, saturation, value */
    double r, g, b;			/* Output: red, green, blue values */
    int n;				/* Number of colors */

    if (argc == 4) {
	h0_s = argv[1];
	h1_s = argv[2];
	n_s = argv[3];
    } else if (argc == 6 && strcmp(argv[1], "-s") == 0) {
	s_s = argv[2];
	h0_s = argv[3];
	h1_s = argv[4];
	n_s = argv[5];
    } else if (argc == 6 && strcmp(argv[1], "-v") == 0) {
	v_s = argv[2];
	h0_s = argv[3];
	h1_s = argv[4];
	n_s = argv[5];
    } else if (argc == 8
	    && (strcmp(argv[1], "-v") == 0) && (strcmp(argv[3], "-s") == 0)) {
	v_s = argv[2];
	s_s = argv[4];
	h0_s = argv[5];
	h1_s = argv[6];
	n_s = argv[7];
    } else if (argc == 8
	    && (strcmp(argv[1], "-s") == 0) && (strcmp(argv[3], "-v") == 0)) {
	s_s = argv[2];
	v_s = argv[4];
	h0_s = argv[5];
	h1_s = argv[6];
	n_s = argv[7];
    } else {
	fprintf(stderr,
		"%s %s\n"
		"Usage: %s [-s saturation] [-v value] hue0 hue1 n_colors\n",
		cmd, HSV_VERSION, cmd);
	exit(1);
    }
    if (s_s && (sscanf(s_s, "%lf", &s) != 1)) {
	fprintf(stderr, "%s expected float value for saturation, got %s\n",
		cmd, s_s);
	exit(1);
    }
    if (v_s && (sscanf(v_s, "%lf", &v) != 1)) {
	fprintf(stderr, "%s expected float value for value, got %s\n",
		cmd, v_s);
	exit(1);
    }
    if (sscanf(h0_s, "%lf", &h0) != 1) {
	fprintf(stderr, "%s expected float value for starting hue, got %s\n",
		cmd, h0_s);
	exit(1);
    }
    if (sscanf(h1_s, "%lf", &h1) != 1) {
	fprintf(stderr, "%s expected float value for ending hue, got %s\n",
		cmd, h1_s);
	exit(1);
    }
    if (sscanf(n_s, "%d", &n) != 1) {
	fprintf(stderr, "%s expected integer value for number of colors, "
		"got %s\n", cmd, n_s);
	exit(1);
    }
    dh = (h1 - h0) / (n - 1);
    for (h = h0; n > 0; n--, h += dh) {
	HSVtoRGB(&r, &g, &b, h, s, v);
	printf("%9.2f %9.2f %9.2f => #%02x%02x%02x\n",
		h, s, v,
		(unsigned)(r * 0xff),
		(unsigned)(g * 0xff),
		(unsigned)(b * 0xff));
    }
    return 0;
}
