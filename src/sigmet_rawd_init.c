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
 .	$Revision: 1.19 $ $Date: 2010/01/06 20:41:03 $
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
#define NCMD 8
char *cmd1v[NCMD] = {
    "types", "good", "read", "volume_headers", "ray_headers", "data",
    "bin_outline", "bintvls"
};
typedef int (callback)(int , char **);
callback types_cb;
callback good_cb;
callback read_cb;
callback volume_headers_cb;
callback ray_headers_cb;
callback data_cb;
callback bin_outline_cb;
callback bintvls_cb;
callback *cb1v[NCMD] = {
    types_cb, good_cb, read_cb, volume_headers_cb, ray_headers_cb, data_cb,
    bin_outline_cb, bintvls_cb
};

/* If true, use degrees instead of radians */
int use_deg = 0;

/* This structure stores data from a Sigmet volume */
int have_vol;
struct Sigmet_Vol vol;
void unload(void);

/* Bounds limit indicating all possible index values */
#define ALL -1

int main(int argc, char *argv[])
{
    char *cmd;			/* Application name */
    char *inFlNm;		/* File with commands */
    FILE *in;			/* Stream from the file named inFlNm */
    char *ang_u;		/* Angle unit */
    int i;			/* Index into cmd1v, cb1v */
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

    /* Initialize globals */
    have_vol = 0;
    Sigmet_InitVol(&vol);

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
    unload();
    
    return 0;
}

void unload(void)
{
    Sigmet_FreeVol(&vol);
    have_vol = 0;
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
	inFlNm = "-";
    } else if (argc == 2) {
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

/*
   Callback for the "read" command.
   Read a volume into memory.
   Usage:
       read
       read -h
       read raw_file
       read -h raw_file
 */
int read_cb(int argc, char *argv[])
{
    int hdr_only;
    char *inFlNm;
    FILE *in;

    hdr_only = 0;
    if (argc == 1) {
	inFlNm = "-";
    } else if (argc == 2) {
	if (strcmp(argv[1], "-h") == 0) {
	    hdr_only = 1;
	    inFlNm = "-";
	} else {
	    inFlNm = argv[1];
	}
    } else if ( argc == 3 && (strcmp(argv[1], "-h") == 0) ) {
	    hdr_only = 1;
	    inFlNm = argv[2];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [-h] [sigmet_volume]");
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
    unload();
    if ( !((hdr_only && Sigmet_ReadHdr(in, &vol))
		|| (!hdr_only && Sigmet_ReadVol(in, &vol))) ) {
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
    have_vol = 1;
    return 1;
}

int volume_headers_cb(int argc, char *argv[])
{
    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    Sigmet_PrintHdr(stdout, vol);
    return 1;
}

int ray_headers_cb(int argc, char *argv[])
{
    int s, r;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
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
    return 1;
}

int data_cb(int argc, char *argv[])
{
    int s, y, r, b;
    char *abbrv;
    float d;
    enum Sigmet_DataType type;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }

    /*
       Identify input and desired output
       Possible forms:
	   data			(argc = 1)
	   data y		(argc = 2)
	   data y s		(argc = 3)
	   data y s r		(argc = 4)
	   data y s r b		(argc = 5)
     */

    y = s = r = b = ALL;
    type = DB_ERROR;
    if (argc > 1 && (type = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc > 2 && sscanf(argv[2], "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc > 3 && sscanf(argv[3], "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc > 4 && sscanf(argv[4], "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc > 5) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin]");
	return 0;
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
	    return 0;
	}
    }
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r != ALL && r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
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

    return 1;
}

int bin_outline_cb(int argc, char *argv[])
{
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sweep ray bin");
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
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r != ALL && r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if ( !Sigmet_BinOutl(&vol, s, r, b, corners) ) {
	Err_Append("Could not compute bin outlines.  ");
	return 0;
    }
    Sigmet_FreeVol(&vol);
    c = (use_deg ? DEG_RAD : 1.0);
    printf("%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;
}

/* Usage: sigmet_raw bintvls type s bounds raw_vol */
int bintvls_cb(int argc, char *argv[])
{
    char *s_s;
    int s, y, r, b;
    char *abbrv;
    double d;
    enum Sigmet_DataType type_t;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep bounds");
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

    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	return 0;
    }

    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
	return 0;
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
}
