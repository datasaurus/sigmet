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
 .	$Revision: 1.5 $ $Date: 2009/11/05 22:56:08 $
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
#define NCMD 5
char *cmd1v[NCMD] = {
    "types", "good", "volume_headers", "ray_headers", "data"
};
typedef int (callback)(int , char **);
callback types_cb;
callback good_cb;
callback volume_headers_cb;
callback ray_headers_cb;
callback data_cb;
callback *cb1v[NCMD] = {
    types_cb, good_cb, volume_headers_cb, ray_headers_cb, data_cb
};

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

int types_cb(int argc, char *argv[])
{
    int y;

    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	return 0;
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	printf("%s | %s\n", Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return 1;
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
    struct Sigmet_Vol vol;

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
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadHdr(in, &vol) ) {
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
    Sigmet_PrintHdr(stdout, vol);

    Sigmet_FreeVol(&vol);
    return 1;
}

int ray_headers_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol vol;
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
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
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
    for (s = 0; s < vol.ih.tc.tni.num_sweeps; s++) {
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    int yr, mon, da, hr, min;
	    double sec;

	    printf("sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		Err_Append("Bad ray time.  ");
		return 0;
	    }
	    printf("%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    printf("az %7.3f %7.3f | ",
		    vol.ray_az0[s][r] * DEG_PER_RAD,
		    vol.ray_az1[s][r] * DEG_PER_RAD);
	    printf("tilt %6.3f %6.3f\n",
		    vol.ray_tilt0[s][r] * DEG_PER_RAD,
		    vol.ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }

    Sigmet_FreeVol(&vol);
    return 1;
}

int data_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol vol;
    int s1, y1, r1, b1;
    int s, y, r, b;
    char *abbrv;
    float d;

    /*
       Identify input and desired output
       Possible forms:
	   sigmet_raw data					(argc = 1)
	   sigmet_raw data    file				(argc = 2)
	   sigmet_raw data    y1				(argc = 2)
	   sigmet_raw data    y1    file			(argc = 3)
	   sigmet_raw data    y1    s1				(argc = 3)
	   sigmet_raw data    y1    s1    file			(argc = 4)
	   sigmet_raw data    y1    s1    r1			(argc = 4)
	   sigmet_raw data    y1    s1    r1    file		(argc = 5)
	   sigmet_raw data    y1    s1    r1    b1		(argc = 5)
	   sigmet_raw data    y1    s1    r1    b1    file	(argc = 6)
     */

    y1 = s1 = r1 = b1 = -1;
    if (argc == 1) {
	inFlNm = "-";
    } else {
	inFlNm = argv[argc - 1];
	if ((strcmp(inFlNm, "-") == 0 && (in = stdin))
		|| (in = fopen(inFlNm, "r"))) {
	    argc--;
	}
    }
    if (argc > 1 && (y1 = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc > 2 && sscanf(argv[2], "%d", &s1) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc > 3 && sscanf(argv[3], "%d", &r1) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc > 4 && sscanf(argv[4], "%d", &b1) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc > 5) {
	Err_Append("Usage: ");
	Err_Append(cmd);
	Err_Append(" ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin] [sigmet_volume]");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
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

    /* Verify */
    if (s1 >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	goto error;
    }
    if (r1 >= 0 && r1 >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }
    if (b1 >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }

    /* Write */
    if (argc == 1) {
	for (y = 0; y < vol.num_types; y++) {
	    enum Sigmet_DataType type = vol.types[y];

	    abbrv = Sigmet_DataType_Abbrv(type);
	    if (type == DB_XHDR || type == DB_ERROR) {
		fprintf(stderr, "Error: volume in memory contains %s data type.\n",
			abbrv);
		exit(1);
	    }
	    for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
		printf("%s. sweep %d\n", abbrv, s);
		for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    printf("ray %d: ", r);
		    for (b = 0; b < vol.ih.tc.tri.num_bins_out; b++) {
			d = Sigmet_DataType_ItoF(type, vol.dat[y][s][r][b]);
			if (Sigmet_IsData(d)) {
			    printf("%f ", d);
			} else {
			    printf("nodat ");
			}
		    }
		    printf("\n");
		}
	    }
	}
    }
    if (argc == 2) {
	y = y1;
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		for (b = 0; b < vol.ih.tc.tri.num_bins_out; b++) {
		}
	    }
	}
    }

    Sigmet_FreeVol(&vol);
    return 1;

error:
    Sigmet_FreeVol(&vol);
    return 0;
}
