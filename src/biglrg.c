/*
   -	bighi.c --
   -		Print a sequence of values with bigger
   -		steps at large magnitude values.
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
   .	$Revision: 1.8 $ $Date: 2012/11/08 21:10:11 $
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define VERSION "1.0"

int main(int argc, char *argv[])
{
    char *cmd = argv[0], *lo_s, *hi_s, *N_s;
    double lo, hi;
    int n, N;
    double n0;	/* Index where value crosses zero */

    if (argc != 4) {
	fprintf(stderr, "%s %s\nUsage: %s lo hi n\n", cmd, VERSION, cmd);
	exit(1);
    }
    lo_s = argv[1];
    hi_s = argv[2];
    N_s = argv[3];
    if (sscanf(lo_s, "%lf", &lo) != 1) {
	fprintf(stderr, "%s: expected float value for lo, got %s\n", cmd, lo_s);
	exit(1);
    }
    if (sscanf(hi_s, "%lf", &hi) != 1) {
	fprintf(stderr, "%s: expected float value for hi, got %s\n", cmd, hi_s);
	exit(1);
    }
    if ( !(lo < hi) ) {
	fprintf(stderr, "%s: low value must be less than high value\n", cmd);
	exit(1);
    }
    if (sscanf(N_s, "%d", &N) != 1) {
	fprintf(stderr, "%s: expected integer value for n, got %s\n", cmd, N_s);
	exit(1);
    }
    if (N <= 0) {
	fprintf(stderr, "%s: Number of values must be positive.\n", cmd);
	exit(1);
    }

    if (lo < 0.0 && hi >= 0.0) {
	/* Define two curves that grow exponentially away from n0. */
	n0 = (N - 1) / (log(1 + hi) / log(1 - lo) + 1);
	if (n0 < 0.0) {
	    fprintf(stderr, "Values cross zero at negative n.\n");
	    exit(1);
	}
	for (n = 0; n < n0; n++) {
	    printf("%d %f\n", n, 1 - pow(1 - lo, 1 - n / n0));
	}
	for ( ; n < N; n++) {
	    printf("%d %f\n", n, pow(1 - lo, n / n0 - 1) - 1);
	}
    } else if (lo >= 0.0 && hi > 0.0) {
	/* Single exponential curve */
	for (n = 0; n < N; n++) {
	    printf("%d %f\n",
		    n, lo - 1 + pow(hi + 1 - lo, (double)n / (N - 1)));
	}
    } else if (lo <= 0.0 && hi < 0.0) {
	/* Single logarithmic curve */
	for (n = 0; n < N; n++) {
	    printf("%d %f\n", n, lo + log(n + 1) / log(N) * (hi - lo));
	}
    }
    return 0;
}
