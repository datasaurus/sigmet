/*
   -	sigmet_good.c --
   -		Return true to shell if volume named on command line is
   -		navigable.
   -
   .	Copyright (c) 2011 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: $ $Date: $
 */

#include <stdio.h>
#include "sigmet.h"

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *vol_fl_nm;		/* Name of Sigmet raw product file */
    FILE *in;
    int dum, status;

    if ( argc == 1 ) {
	status = Sigmet_Vol_Read(stdin, NULL);
    } else if ( argc == 2 ) {
	vol_fl_nm = argv[1];
	if ( (in = Sigmet_VolOpen(vol_fl_nm, &dum)) ) {
	    status = Sigmet_Vol_Read(in, NULL);
	} else {
	    status = SIGMET_IO_FAIL;
	}
    } else {
	fprintf(stderr, "Usage: %s [raw_file]\n", argv0);
	exit(EXIT_FAILURE);
    }

    return ( status == SIGMET_OK ) ? EXIT_SUCCESS : EXIT_FAILURE;
}
