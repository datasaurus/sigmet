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
 .	$Revision: 1.17 $ $Date: 2010/01/05 22:27:20 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "alloc.h"
#include "str.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/* subcommand name */
char *cmd1;

/* Subcommands */
#define NCMD 7
char *cmd1v[NCMD] = {
    "ray_headers", "bintvls", "bin_outline", "data", "good", "types",
    "volume_headers"
};
typedef int (callback)(int , char **);
callback types_cb;
callback good_cb;
callback volume_headers_cb;
callback ray_headers_cb;
callback data_cb;
callback bin_outline_cb;
callback bintvls_cb;
callback *cb1v[NCMD] = {
    ray_headers_cb, bintvls_cb, bin_outline_cb, data_cb, good_cb, types_cb,
    volume_headers_cb
};

/* If true, use degrees instead of radians */
int use_deg = 0;

/* Bounds limit indicating all possible index values */
#define ALL -1

int main(int argc, char *argv[])
{
    char *cmd;			/* Application name */
    char *inFlNm;		/* File with commands */
    FILE *in;			/* Stream from the file named inFlNm */
    char *ang_u;		/* Angle unit */
    int i;
    char *ln = NULL;		/* Input line from the command file */
    int n;			/* Number of characters in ln */
    int argc1 = 0;		/* Number of arguments in an input line */
    char **argv1 = NULL;	/* Arguments from an input line */

    /* Ensure minimum command line */
    cmd = argv[0];
    if (argc == 1) {
	/* Call is of form: "sigmet_raw" */
	inFlNm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw command_file" */
	inFlNm = argv[1];
    } else {
	fprintf(stderr, "Usage: %s [command_file]\n", cmd);
	exit(EXIT_FAILURE);
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	fprintf(stderr, "%s: Could not open %s for input.\n",
		cmd, (in == stdin) ? "standard in" : inFlNm);
	return 0;
    }

    /* Check for angle unit */
    if ((ang_u = getenv("ANGLE_UNIT")) != NULL) {
	if (strcmp(ang_u, "DEGREE") == 0) {
	    use_deg = 1;
	} else if (strcmp(ang_u, "RADIAN") == 0) {
	    use_deg = 0;
	} else {
	    fprintf(stderr, "%s: Unknown angle unit %s.\n", cmd, ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    /* Read and execute commands from in */
    while (Str_GetLn(in, '\n', &ln, &n) == 1) {

	/* Split the input line into words */
	if ( !(argv1 = Str_Words(ln, argv1, &argc1)) || !*argv1 ) {
	    fprintf(stderr, "%s: could not parse\n%s\nas a command.\n%s\n",
		    cmd, (ln && strlen(ln) > 0) ? ln : "(blank line)", Err_Get());
	    exit(EXIT_FAILURE);
	}
	cmd1 = argv1[0];

	/* Search cmd1v for cmd1.  When match is found, evaluate the associated
	 * callback from cb1v. */
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    fprintf(stderr, "%s: No option or subcommand named \"%s\"\n",
		    cmd, cmd1);
	    fprintf(stderr, "Subcommand must be one of: ");
	    for (i = 0; i < NCMD; i++) {
		fprintf(stderr, "%s ", cmd1v[i]);
	    }
	    fprintf(stderr, "\n");
	    exit(EXIT_FAILURE);
	}
	if ( !(cb1v[i])(argc1, argv1) ) {
	    fprintf(stderr, "%s %s failed.\n%s\n", cmd, cmd1, Err_Get());
	}
    }
    FREE(ln);
    FREE(argv1);
    
    return 0;
}

int types_cb(int argc, char *argv[])
{
    int y;

    if (argc != 1) {
	Err_Append("Usage: ");
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

    if (argc == 1) {
	/* Call is of form: "sigmet_raw good" */
	inFlNm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw good file_name" */
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append((in == stdin) ? "standard in" : inFlNm);
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

    if (argc == 1) {
	/* Call is of form: "sigmet_raw volume_headers" */
	inFlNm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw volume_headers file_name" */
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append((in == stdin) ? "standard in" : inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadHdr(in, &vol) ) {
	Err_Append("Could not read volume from ");
	Err_Append((in == stdin) ? "standard input" : inFlNm);
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

    if (argc == 1) {
	/* Call is of form: "sigmet_raw ray_headers" */
	inFlNm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw ray_headers file_name" */
	inFlNm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(inFlNm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append((in == stdin) ? "standard in" : inFlNm);
	Err_Append(" for input.\n");
	return 0;
    }

    /* Read */
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
	Err_Append("Could not read volume from ");
	Err_Append((in == stdin) ? "standard input" : inFlNm);
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

	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
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
    int s, y, r, b;
    char *abbrv;
    float d;
    enum Sigmet_DataType type, type_t;

    /*
       Identify input and desired output
       Possible forms:
	   sigmet_raw data		(argc = 1)
	   sigmet_raw data file		(argc = 2)
	   sigmet_raw data y		(argc = 2)
	   sigmet_raw data y file	(argc = 3)
	   sigmet_raw data y s		(argc = 3)
	   sigmet_raw data y s file	(argc = 4)
	   sigmet_raw data y s r	(argc = 4)
	   sigmet_raw data y s r file	(argc = 5)
	   sigmet_raw data y s r b	(argc = 5)
	   sigmet_raw data y s r b file	(argc = 6)
     */

    y = s = r = b = ALL;
    type = DB_ERROR;
    if (argc == 1) {
	/* Call is of form: "sigmet_raw data" */
	inFlNm = "-";
    }
    if (argc == 2) {
	if ((type_t = Sigmet_DataType(argv[1])) == DB_ERROR) {
	    /* Call is of form: "sigmet_raw data file_name" */
	    inFlNm = argv[1];
	} else {
	    /* Call is of form: "sigmet_raw data type" */
	    inFlNm = "-";
	    type = type_t;
	}
    }
    if (argc > 2 && (type = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc == 3) {
	if (sscanf(argv[2], "%d", &s) == 1) {
	    /* Call is of form: "sigmet_raw data type sweep" */
	    inFlNm = "-";
	} else {
	    /* Call is of form: "sigmet_raw data type file_name" */
	    inFlNm = argv[2];
	}
    }
    if (argc > 3 && sscanf(argv[2], "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc == 4) {
	if (sscanf(argv[3], "%d", &r) == 1) {
	    /* Call is of form: "sigmet_raw data type sweep ray" */
	    inFlNm = "-";
	} else {
	    /* Call is of form: "sigmet_raw data type sweep file_name" */
	    inFlNm = argv[3];
	}
    }
    if (argc > 4 && sscanf(argv[3], "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc == 5) {
	if (sscanf(argv[4], "%d", &b) == 1) {
	    /* Call is of form: "sigmet_raw data type sweep ray bin" */
	    inFlNm = "-";
	} else {
	    /* Call is of form: "sigmet_raw data type sweep ray file_name" */
	    inFlNm = argv[4];
	}
    }
    if (argc > 5 && sscanf(argv[4], "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc == 6) {
	/* Call is of form: "sigmet_raw data type sweep ray bin file_name" */
	inFlNm = argv[5];
    }
    if (argc > 6) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin] [sigmet_volume]");
	return 0;
    }

    /* Read volume */
    if (strcmp(inFlNm, "-") == 0 ) {
	in = stdin;
    } else {
	if ( !(in = fopen(inFlNm, "r")) ) {
	    Err_Append("Could not open ");
	    Err_Append((in == stdin) ? "standard input" : inFlNm);
	    Err_Append("for reading.  ");
	    return 0;
	}
    }
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
	Err_Append("Could not read volume from ");
	Err_Append((in == stdin) ? "standard input" : inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    if (type != DB_ERROR) {
	/*
	   User has specified a data type.  Search for it in the volume,
	   and set y to the specified type (instead of ALL).
	 */
	abbrv = Sigmet_DataType_Abbrv(type);
	for (y = 0; y < vol.num_types; y++) {
	    if (type == vol.types[y]) {
		break;
	    }
	}
	if (y == vol.num_types) {
	    Err_Append("Data type ");
	    Err_Append(abbrv);
	    Err_Append(" not in volume.\n");
	    goto error;
	}
    }
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	goto error;
    }
    if (r != ALL && r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }

    /* Write */
    if (y == ALL && s == ALL && r == ALL && b == ALL) {
	for (y = 0; y < vol.num_types; y++) {
	    type = vol.types[y];
	    abbrv = Sigmet_DataType_Abbrv(type);
	    for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
		printf("%s. sweep %d\n", abbrv, s);
		for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		    printf("ray %d: ", r);
		    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
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
    } else if (s == ALL && r == ALL && b == ALL) {
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    printf("%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		printf("ray %d: ", r);
		for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		    if (Sigmet_IsData(d)) {
			printf("%f ", d);
		    } else {
			printf("nodat ");
		    }
		}
		printf("\n");
	    }
	}
    } else if (r == ALL && b == ALL) {
	printf("%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    printf("ray %d: ", r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else if (b == ALL) {
	if (vol.ray_ok[s][r]) {
	    printf("%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else {
	if (vol.ray_ok[s][r]) {
	    printf("%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
	    if (Sigmet_IsData(d)) {
		printf("%f ", d);
	    } else {
		printf("nodat ");
	    }
	    printf("\n");
	}
    }

    Sigmet_FreeVol(&vol);
    return 1;

error:
    Sigmet_FreeVol(&vol);
    return 0;
}

int bin_outline_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol vol;
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if (argc == 4) {
	/* sigmet_raw bin_outline s r b */
	inFlNm = "-";
    } else if (argc == 5) {
	/* sigmet_raw bin_outline s r b raw_volume */
	inFlNm = argv[4];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sweep ray bin [sigmet_volume]");
	return 0;
    }
    s_s = argv[1];
    r_s = argv[2];
    b_s = argv[3];

    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (sscanf(r_s, "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (sscanf(b_s, "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }

    /* Read volume */
    if (strcmp(inFlNm, "-") == 0 ) {
	in = stdin;
    } else {
	if ( !(in = fopen(inFlNm, "r")) ) {
	    Err_Append("Could not open ");
	    Err_Append((in == stdin) ? "standard input" : inFlNm);
	    Err_Append("for reading.  ");
	    return 0;
	}
    }
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
	Err_Append("Could not read volume from ");
	Err_Append((in == stdin) ? "standard input" : inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    /* Compute */
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	goto error;
    }
    if (r != ALL && r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	goto error;
    }
    if ( !Sigmet_BinOutl(&vol, s, r, b, corners) ) {
	Err_Append("Could not compute bin outlines.  ");
	goto error;
    }
    Sigmet_FreeVol(&vol);
    c = (use_deg ? DEG_RAD : 1.0);
    printf("%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;

error:
    Sigmet_FreeVol(&vol);
    return 0;
}

/* Usage: sigmet_raw bintvls type s bounds raw_vol */
int bintvls_cb(int argc, char *argv[])
{
    char *inFlNm;
    FILE *in;
    struct Sigmet_Vol vol;
    char *s_s;
    int s, y, r, b;
    char *abbrv;
    double d;
    enum Sigmet_DataType type_t;

    if (argc == 4) {
	/* sigmet_raw bintvls type sweep bounds */
	inFlNm = "-";
    } else if (argc == 5) {
	/* sigmet_raw bintvls type sweep bounds raw_volume */
	inFlNm = argv[4];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep bounds [sigmet_volume]");
	return 0;
    }
    abbrv = argv[1];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
    }
    s_s = argv[2];
    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }

    /* Read volume */
    if (strcmp(inFlNm, "-") == 0 ) {
	in = stdin;
    } else if ( !(in = fopen(inFlNm, "r")) ) {
	    Err_Append("Could not open ");
	    Err_Append((in == stdin) ? "standard input" : inFlNm);
	    Err_Append("for reading.  ");
	    return 0;
    }
    Sigmet_InitVol(&vol);
    if ( !Sigmet_ReadVol(in, &vol) ) {
	Err_Append("Could not read volume from ");
	Err_Append((in == stdin) ? "standard input" : inFlNm);
	Err_Append(".\n");
	if (in != stdin) {
	    fclose(in);
	}
	return 0;
    }
    if (in != stdin) {
	fclose(in);
    }

    /* Locate user ordered type in volume */
    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	goto error;
    }

    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	goto error;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
	goto error;
    }

    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( !vol.ray_ok[s][r] ) {
	    continue;
	}
	for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
	    d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
	}
	printf("\n");
    }

    return 1;

error:
    Sigmet_FreeVol(&vol);
    return 0;
}
