/*
 -	sigmet_raw.c --
 -		Command line utility that accesses Sigmet raw volumes.
 -		See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.2 $ $Date: 2009/10/28 22:17:38 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "sigmet.h"

/* Application name and subcommand name */
char *cmd, *cmd1;

/* Subcommands */
#define NCMD 4
char *cmd1v[NCMD] = {"good", "volume_headers", "ray_headers", "data"};
typedef int (callback)(int , char **);
callback good_cb;
callback volume_headers_cb;
callback ray_headers_cb;
callback data_cb;
callback *cb1v[NCMD] = {good_cb, volume_headers_cb, ray_headers_cb, data_cb};

int main(int argc, char *argv[])
{
    int i;
    int rslt;

    /* Ensure minimum command line */
    cmd = argv[0];
    if (argc < 2) {
	fprintf(stderr, "Usage: %s subcommand [subcommand_options ...]\n", cmd);
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
		fprintf(stderr, "%s\n", Err_Get());
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

int good_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;

    /* Identify input */
    if (argc == 1) {
	inFlNm = "-";
    } else if (argc == 2) {
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append(inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* See if good volume */
    if ( !Sigmet_GoodVol(in) ) {
	if (in != stdin) {
	    fclose(in);
	}
	/* Skip output messages */
	exit(1);
    }
    if (in != stdin) {
	fclose(in);
    }
    return 1;
}

int volume_headers_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol sig_vol;

    /* Identify input */
    if (argc == 1) {
	inFlNm = "-";
    } else if (argc == 2) {
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append(inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&sig_vol);
    if ( !Sigmet_ReadHdr(in, &sig_vol) ) {
	Err_Append("Could not read ");
	Err_Append(inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    /* Write */
    Sigmet_PrintHdr(stdout, sig_vol);

    Sigmet_FreeVol(&sig_vol);
    return 1;
}

int ray_headers_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol sig_vol;
    int s, r;

    /* Identify input */
    if (argc == 1) {
	inFlNm = "-";
    } else if (argc == 2) {
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append(inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&sig_vol);
    if ( !Sigmet_ReadVol(in, &sig_vol) ) {
	Err_Append("Could not read ");
	Err_Append(inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    /* Write */
    for (s = 0; s < sig_vol.ih.tc.tni.num_sweeps; s++) {
	for (r = 0; r < sig_vol.ih.ic.num_rays; r++) {
	    int yr, mon, da, hr, min;
	    double sec;

	    printf("sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(sig_vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		Err_Append("Bad ray time.  ");
		return 0;
	    }
	    printf("%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    printf("az %7.3f %7.3f | ",
		    sig_vol.ray_az0[s][r] * DEG_PER_RAD,
		    sig_vol.ray_az1[s][r] * DEG_PER_RAD);
	    printf("tilt %6.3f %6.3f\n",
		    sig_vol.ray_tilt0[s][r] * DEG_PER_RAD,
		    sig_vol.ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }

    Sigmet_FreeVol(&sig_vol);
    return 1;
}

int data_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol sig_vol;

    /* Identify input */
    if (argc == 1) {
	inFlNm = "-";
    } else if (argc == 2) {
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append(inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&sig_vol);
    if ( !Sigmet_ReadVol(in, &sig_vol) ) {
	Err_Append("Could not read ");
	Err_Append(inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    /* Write */

    Sigmet_FreeVol(&sig_vol);
    return 1;
}
