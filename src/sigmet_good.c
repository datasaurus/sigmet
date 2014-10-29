/*
   -	sigmet_good.c --
   -		Return true to shell if volume named on command line is
   -		navigable.
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
   .	$Revision: 1.5 $ $Date: 2013/05/16 19:50:55 $
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sigmet.h"

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *vol_fl_nm;		/* Name of Sigmet raw product file */
    FILE *in;
    int status;

    if ( argc == 2 && strcmp(argv[1], "-v") == 0 ) {
	printf("%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
		"All rights reserved.\n", argv[0], SIGMET_RAW_VERSION);
	return EXIT_SUCCESS;
    }
    if ( argc == 1 ) {
	status = Sigmet_Vol_Read(stdin, NULL);
    } else if ( argc == 2 ) {
	vol_fl_nm = argv[1];
	if ( strcmp(vol_fl_nm, "-") == 0 ) {
	    in = stdin;
	} else {
	    in = fopen(vol_fl_nm, "r");
	}
	if ( !in ) {
	    fprintf(stderr, "%s: could not open %s for reading.\n",
		    argv0, vol_fl_nm);
	    status = SIGMET_IO_FAIL;
	}
	status = Sigmet_Vol_Read(in, NULL);
    } else {
	fprintf(stderr, "Usage: %s [raw_file]\n", argv0);
	exit(EXIT_FAILURE);
    }
    return ( status == SIGMET_OK ) ? EXIT_SUCCESS : EXIT_FAILURE;
}
