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
 .	$Revision: 1.26 $ $Date: 2010/01/11 17:43:33 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
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

/* The global Sigmet volume, used by most callbacks. */
struct Sigmet_Vol vol;	/* Sigmet volume */
int have_hdr;		/* If true, vol has headers from a Sigmet volume */
int have_vol;		/* If true, vol has headers and data from a Sigmet volume */
char *vol_nm;		/* Name of volume file */
size_t vol_nm_l;	/* Number of characters that can be stored at vol_nm */
void unload(void);	/* Delete and reinitialize contents of vol */

/* Bounds limit indicating all possible index values */
#define ALL -1

int main(int argc, char *argv[])
{
    char *cmd;			/* Application name */
    char *in_nm;		/* File with commands */
    FILE *in;			/* Stream from the file named in_nm */
    char *ang_u;		/* Angle unit */
    int i;			/* Index into cmd1v, cb1v */
    char *ln = NULL;		/* Input line from the command file */
    int n;			/* Number of characters in ln */
    int argc1 = 0;		/* Number of arguments in an input line */
    char **argv1 = NULL;	/* Arguments from an input line */
    int tty;			/* If true, session is interactive */

    if ( (cmd = strrchr(argv[0], '/')) ) {
	cmd++;
    } else {
	cmd = argv[0];
    }
    if (argc == 1) {
	/* Call is of form: "sigmet_raw" */
	in_nm = "-";
    } else if (argc == 2) {
	/* Call is of form: "sigmet_raw command_file" */
	in_nm = argv[1];
    } else {
	fprintf(stderr, "Usage: %s [command_file]\n", cmd);
	exit(EXIT_FAILURE);
    }
    if (strcmp(in_nm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(in_nm, "r")) ) {
	fprintf(stderr, "%s: Could not open %s for input.\n",
		cmd, (in == stdin) ? "standard in" : in_nm);
	return 0;
    }
    tty = isatty(STDIN_FILENO);

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
    have_hdr = have_vol = 0;
    Sigmet_InitVol(&vol);
    vol_nm_l = 1;
    if ( !(vol_nm = MALLOC(vol_nm_l)) ) {
	fprintf(stderr, "Could not allocate storage for volume file name.\n");
	exit(EXIT_FAILURE);
    }
    *vol_nm = '\0';

    /* Read and execute commands from in */
    if ( tty ) {
	printf("%s> ", cmd);
    }
    while (Str_GetLn(in, '\n', &ln, &n) == 1) {
	if ( (argv1 = Str_Words(ln, argv1, &argc1))
		&& argv1[0] && (strcmp(argv1[0], "#") != 0) ) {
	    cmd1 = argv1[0];
	    if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
		fprintf(stderr, "%s: No option or subcommand named \"%s\"\n",
			cmd, cmd1);
		fprintf(stderr, "Subcommand must be one of: ");
		for (i = 0; i < NCMD; i++) {
		    fprintf(stderr, "%s ", cmd1v[i]);
		}
		fprintf(stderr, "\n");
	    } else if ( !(cb1v[i])(argc1, argv1) ) {
		fprintf(stderr, "%s: %s failed.\n%s\n", cmd, cmd1, Err_Get());
	    }
	}
	if ( tty ) {
	    printf("%s> ", cmd);
	}
    }
    FREE(ln);
    FREE(argv1);
    unload();
    FREE(vol_nm);

    return 0;
}

void unload(void)
{
    Sigmet_FreeVol(&vol);
    have_hdr = have_vol = 0;
    vol_nm[0] = '\0';
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
    char *in_nm;
    FILE *in;

    if (argc == 1) {
	in_nm = "-";
    } else if (argc == 2) {
	in_nm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if (strcmp(in_nm, "-") == 0) {
	in = stdin;
    } else if ( !(in = fopen(in_nm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append((in == stdin) ? "standard in" : in_nm);
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
    char *in_nm;
    FILE *in;
    char *t;
    size_t l;
    pid_t pid = 0;

    hdr_only = 0;
    if (argc == 1) {
	in_nm = "-";
    } else if (argc == 2) {
	if (strcmp(argv[1], "-h") == 0) {
	    hdr_only = 1;
	    in_nm = "-";
	} else {
	    in_nm = argv[1];
	}
    } else if ( argc == 3 && (strcmp(argv[1], "-h") == 0) ) {
	    hdr_only = 1;
	    in_nm = argv[2];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [-h] [sigmet_volume]");
	return 0;
    }
    if (strcmp(in_nm, "-") == 0) {
	in = stdin;
    } else {
	char *sfx;	/* Filename suffix */
	int pfd[2];	/* Pipe for data */

	sfx = strrchr(in_nm , '.');
	if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	    if ( pipe(pfd) == -1 ) {
		Err_Append(strerror(errno));
		Err_Append("\nCould not create pipe for gzip.  ");
		return 0;
	    }
	    switch (pid = fork()) {
		case -1:
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not spawn gzip process.  ");
		    return 0;
		case 0:
		    /* Child process - gzip.  Send child stdout to pipe. */
		    if ( dup2(pfd[1], STDOUT_FILENO) == -1
			    || close(pfd[1]) == -1 || close(pfd[0]) == -1 ) {
			perror("Could not set up gzip process");
			_exit(EXIT_FAILURE);
		    }
		    execlp("gunzip", "gunzip", "-c", in_nm, (char *)NULL);
		    fprintf(stderr, "gunzip failed.\n");
		    _exit(EXIT_FAILURE);
		default:
		    /* This process.  Read output from gzip. */
		    if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
			Err_Append(strerror(errno));
			Err_Append("\nCould not read gzip process.  ");
			return 0;
		    }
	    }
	} else if ( !(in = fopen(in_nm, "r")) ) {
	    Err_Append("Could not open ");
	    Err_Append((in == stdin) ? "standard in" : in_nm);
	    Err_Append(" for input.\n");
	    return 0;
	}
    }
    if (hdr_only) {
	if( have_hdr && (in != stdin) && (strcmp(in_nm, vol_nm) == 0) ) {
	    /* No need to read headers */
	    fclose(in);
	    return 1;
	}
	/* Read headers */
	unload();
	if ( !Sigmet_ReadHdr(in, &vol) ) {
	    Err_Append("Could not read headers from ");
	    Err_Append((in == stdin) ? "standard input" : in_nm);
	    Err_Append(".\n");
	    if (in != stdin) {
		fclose(in);
	    }
	    return 0;
	}
	have_hdr = 1;
	have_vol = 0;
    } else {
	if ( have_vol && !vol.truncated && (in != stdin)
		&& (strcmp(in_nm, vol_nm) == 0) ) {
	    /* No need to read volume */
	    fclose(in);
	    return 1;
	}
	/* Read volume */
	unload();
	if ( !Sigmet_ReadVol(in, &vol) ) {
	    Err_Append("Could not read volume from ");
	    Err_Append((in == stdin) ? "standard input" : in_nm);
	    Err_Append(".\n");
	    if (in != stdin) {
		fclose(in);
	    }
	    return 0;
	}
	have_hdr = have_vol = 1;
    }
    if (in != stdin) {
	fclose(in);
    }
    if ( pid ) {
	waitpid(pid, NULL, WUNTRACED);
    }
    l = 0;
    if ( !(t = Str_Append(vol_nm, &l, &vol_nm_l, in_nm, strlen(in_nm))) ) {
	Err_Append("Could not store name of global volume.  ");
	unload();
	return 0;
    }
    vol_nm = t;
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
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b >= vol.ih.tc.tri.num_bins_out) {
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
