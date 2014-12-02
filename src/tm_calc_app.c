/*
   -	tm_calc.c --
   -		This file defines an application that does
   -		time calculations.
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
   .	$Revision: 1.18 $ $Date: 2013/03/18 17:24:01 $
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "alloc.h"
#include "str.h"
#include "tm_calc_lib.h"

/* Application name and subcommand name */
char *cmd, *cmd1;

/* Callback functions.  There should be one for each subcommand. */
typedef int (callback)(int , char **);
callback caltojul_cb;
callback jultocal_cb;

/* Number of subcommands */
#define NCMD 2

/* Array of subcommand names */
char *cmd1v[NCMD] = {"caltojul", "jultocal"};

/* Array of subcommand callbacks. cb1v[i] is the callback for cmd1v[i] */
callback *cb1v[NCMD] = {caltojul_cb, jultocal_cb};

int main(int argc, char *argv[])
{
    int i;
    int rslt = 0;

    /* Ensure minimum command line */
    cmd = argv[0];
    if (argc < 2) {
	fprintf(stderr, "%s %s\n"
		"Usage: %s subcommand [subcommand_options ...]\n",
		cmd, TMCALC_VERSION, cmd);
	exit(1);
    }
    cmd1 = argv[1];

    /* Search cmd1v for cmd1.  When match is found, evaluate the associated
     * callback from cb1v. */
    for (i = 0; i < NCMD; i++) {
	if (strcmp(cmd1v[i], cmd1) == 0) {
	    rslt = (cb1v[i])(argc - 1, argv + 1);
	    if ( !rslt ) {
		fprintf(stderr, "%s %s failed.\n", cmd, cmd1);
		break;
	    } else {
		break;
	    }
	}
    }
    if (i == NCMD) {
	fprintf(stderr, "%s: No option or subcommand named %s\n", cmd, cmd1);
	fprintf(stderr, "Subcommand must be one of: ");
	for (i = 0; i < NCMD; i++) {
	    fprintf(stderr, "%s ", cmd1v[i]);
	}
	fprintf(stderr, "\n");
    }
    return !rslt;
}

int caltojul_cb(int argc, char *argv[])
{
    char *yr_s, *mo_s, *dy_s, *hr_s, *mi_s, *sc_s;
    int yr, mo, dy, hr, mi;
    double sc;
    int da;
    char *fmt;

    /* Parse command line */
    if ( argc == 7 ) {
	fmt = "%lf\n";
	da = 0;
    } else if ( argc == 9 && strcmp(argv[1], "-f" ) == 0) {
	fmt = Str_Esc(argv[2]);
	da = 2;
    } else {
	fprintf(stderr, "Usage: %s %s [-f format] year month day "
		"hour minute second\n", cmd, cmd1);
	return 0;
    }
    yr_s = argv[1 + da];
    mo_s = argv[2 + da];
    dy_s = argv[3 + da];
    hr_s = argv[4 + da];
    mi_s = argv[5 + da];
    sc_s = argv[6 + da];

    /* Get values from command line arguments */
    if (sscanf(yr_s, "%d", &yr) != 1) {
	fprintf(stderr, "Expected integer value for year, got%s\n", yr_s);
	return 0;
    }
    if (sscanf(mo_s, "%d", &mo) != 1) {
	fprintf(stderr, "Expected integer value for month, got %s\n", mo_s);
	return 0;
    }
    if (sscanf(dy_s, "%d", &dy) != 1) {
	fprintf(stderr, "Expected integer value for day, got %s\n", dy_s);
	return 0;
    }
    if (sscanf(hr_s, "%d", &hr) != 1) {
	fprintf(stderr, "Expected integer value for hour, got %s\n", hr_s);
	return 0;
    }
    if (sscanf(mi_s, "%d", &mi) != 1) {
	fprintf(stderr, "Expected integer value for minute, got %s\n", mi_s);
	return 0;
    }
    if (sscanf(sc_s, "%lf", &sc) != 1) {
	fprintf(stderr, "Expected float value for second, got %s\n", sc_s);
	return 0;
    }

    /* Send result */
    printf(fmt, Tm_CalToJul(yr, mo, dy, hr, mi, sc));
    return 1;
}

int jultocal_cb(int argc, char *argv[])
{
    int yr, mo, dy, hr, mi;
    double sc;
    char *fmt;			/* Output format */
    char *j_s;			/* Julian day, string from command line */
    double j;			/* Julian day */

    /* Parse command line */
    if ( argc == 2 ) {
	fmt = "%d %d %d %d %d %lf\n";
	j_s = argv[1];
    } else if ( argc == 4 && strcmp(argv[1], "-f") == 0 ) {
	fmt = Str_Esc(argv[2]);
	j_s = argv[3];
    } else {
	fprintf(stderr, "Usage: %s %s [-f format] julian_day\n", cmd, cmd1);
	return 0;
    }

    /* Get Julian date from command line argument */
    if (sscanf(j_s, "%lf", &j) != 1) {
	fprintf(stderr, "Expected float value for Julian day, got %s\n", j_s);
	return 0;
    }

    /* Send result */
    if (Tm_JulToCal(j, &yr, &mo, &dy, &hr, &mi, &sc)) {
	printf(fmt, yr, mo, dy, hr, mi, sc);
	return 1;
    } else {
	return 0;
    }
}
