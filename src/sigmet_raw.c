/*
   -	sigmet_raw.c --
   -		Command line access to sigmet raw product volumes.
   -		See sigmet_raw (1).
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
   .	$Revision: 1.127 $ $Date: 2013/01/18 20:22:56 $
 */

#include "unix_defs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "alloc.h"
#include "str.h"
#include "tm_calc_lib.h"
#include "bisearch_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/*
   This struct contains the Sigmet raw product volume used by this process.
 */

static struct Sigmet_Vol vol;

/*
   Where to send output from various commands.
 */

static FILE *out;

/*
   Maximum number of characters allowed in a color name.
   COLOR_NM_LEN_A = storage size
   COLOR_NM_LEN_S = maximum number of non-nul characters.
 */

#define COLOR_NM_LEN_A 64
#define COLOR_NM_LEN_S "63"

/*
   Name of transparent color
 */

#define TRANSPARENT "transparent"

/*
   Maximum number of characters in the string representation of a float value
   FLOAT_STR_LEN_A = storage size
   FLOAT_STR_LEN_S = maximum number of non-nul characters.
 */

#define FLOAT_STR_LEN_A 64
#define FLOAT_STR_LEN_S "63"

/*
   Function to convert between longitude-latitude coordinates and map
   coordinates.
 */

static int (*lonlat_to_xy)(double, double, double *, double *)
    = Sigmet_Proj_LonLatToXY;

/*
   Default static string length
 */

#define LEN 255

/*
   Local functions
 */

static FILE *vol_open(const char *, pid_t *);
static int handle_signals(void);
static void handler(int signum);
static char *sigmet_err(enum SigmetStatus);

/*
   Callbacks for the subcommands.
 */

typedef int (callback)(int , char **);
static callback commands_cb;
static callback open_cb;
static callback close_cb;
static callback exit_cb;
static callback data_types_cb;
static callback volume_headers_cb;
static callback vol_hdr_cb;
static callback near_sweep_cb;
static callback sweep_headers_cb;
static callback ray_headers_cb;
static callback new_field_cb;
static callback del_field_cb;
static callback size_cb;
static callback set_field_cb;
static callback add_cb;
static callback sub_cb;
static callback mul_cb;
static callback div_cb;
static callback log10_cb;
static callback incr_time_cb;
static callback data_cb;
static callback bdata_cb;
static callback bin_outline_cb;
static callback sweep_bnds_cb;
static callback radar_lon_cb;
static callback radar_lat_cb;
static callback shift_az_cb;
static callback outlines_cb;
static callback dorade_cb;

/*
   Convenience functions
 */

static int set_proj(void);

/*
   Subcommand names and associated callbacks. The hash function defined
   below returns the index from cmd1v or cb1v corresponding to string cmd.

   Array should be sized for perfect hashing. Parser does not search buckets.

   Hashing function from Kernighan, Brian W. and Rob Pike, The Practice of
   Programming, Reading, Massachusetts. 1999
 */

#define N_HASH_CMD 114
static char *cmd1v[N_HASH_CMD] = {
    "close", "", "shift_az", "", "radar_lon", "", "", "", 
    "", "outlines", "", "", "sub", "", "", "", 
    "", "", "", "", "", "", "del_field", "", 
    "", "", "", "", "", "", "", "ray_headers", 
    "radar_lat", "", "", "", "data_types", "", "", "", 
    "", "", "", "", "", "sweep_headers", "", "", 
    "", "", "", "", "commands", "", "incr_time", "set_field", 
    "", "", "sweep_bnds", "size", "", "", "data", "near_sweep", 
    "bdata", "div", "", "", "open", "", "mul", "new_field", 
    "bin_outline", "", "", "", "", "log10", "", "", 
    "vol_hdr", "", "", "", "", "", "", "add", 
    "", "", "", "", "", "", "", "", 
    "", "", "", "", "", "", "", "", 
    "", "", "exit", "dorade", "", "volume_headers", "", "", 
    "", "", 
};
static callback *cb1v[N_HASH_CMD] = {
    close_cb, NULL, shift_az_cb, NULL, radar_lon_cb, NULL, NULL, NULL, 
    NULL, outlines_cb, NULL, NULL, sub_cb, NULL, NULL, NULL, 
    NULL, NULL, NULL, NULL, NULL, NULL, del_field_cb, NULL, 
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, ray_headers_cb, 
    radar_lat_cb, NULL, NULL, NULL, data_types_cb, NULL, NULL, NULL, 
    NULL, NULL, NULL, NULL, NULL, sweep_headers_cb, NULL, NULL, 
    NULL, NULL, NULL, NULL, commands_cb, NULL, incr_time_cb, set_field_cb, 
    NULL, NULL, sweep_bnds_cb, size_cb, NULL, NULL, data_cb, near_sweep_cb, 
    bdata_cb, div_cb, NULL, NULL, open_cb, NULL, mul_cb, new_field_cb, 
    bin_outline_cb, NULL, NULL, NULL, NULL, log10_cb, NULL, NULL, 
    vol_hdr_cb, NULL, NULL, NULL, NULL, NULL, NULL, add_cb, 
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
    NULL, NULL, exit_cb, dorade_cb, NULL, volume_headers_cb, NULL, NULL, 
    NULL, NULL, 
};

#define HASH_X 31
static int hash(const char *);
static int hash(const char *cmd)
{
    unsigned h;

    for (h = 0 ; *cmd != '\0'; cmd++) {
	h = HASH_X * h + (unsigned)*cmd;
    }
    return h % N_HASH_CMD;
}

/*
   Environment variable
 */

#define SIGMET_GEOG_PROJ "SIGMET_GEOG_PROJ"

/*
   main function. See sigmet_raw (1)
 */

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *vol_fl_nm;			/* Name of raw product file */
    FILE *vol_fl;			/* Input stream associated with
					   vol_fl_nm */
    pid_t lpid;				/* Id if reading volume from another
					   process */
    char *script_nm;			/* Name of script file */
    FILE *script;			/* Script stream */
    int daemon = 0;			/* If true, do not exit on EOF */
    char *ln = NULL;			/* Input line */
    size_t n_max = 0;			/* Allocation at l */
    char **argv1 = NULL;		/* Command from input */
    int argc1;				/* Number of words in argv1 */
    char *cmd;				/* Comand name, from input */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    int n;

    if ( !handle_signals() ) {
	fprintf(stderr, "%s: could not set up signal management.", argv0);
	exit(EXIT_FAILURE);
    }
    out = stdout;
    if ( argc == 1 ) {
	fprintf(out, "%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
		"All rights reserved.\n"
		"Usage: %s raw_product_file [command_file]\n"
		"Refer to sigmet_raw (1) man page for more information.\n",
		argv0, SIGMET_VERSION, argv0);
	exit(EXIT_SUCCESS);
    } else if ( argc == 2 ) {
	vol_fl_nm = argv[1];
	script_nm = "-";
    } else if ( argc == 3 ) {
	vol_fl_nm = argv[1];
	script_nm = argv[2];
    } else {
	fprintf(stderr, "Usage: %s sigmet_raw_file [command_file]\n", argv0);
	exit(EXIT_FAILURE);
    }

    /*
       If commands will come from a script file, open it.
       If script file is a fifo, set daemon mode.
     */

    if ( strcmp(script_nm, "-") == 0 ) {
	script = stdin;
    } else {
	int i_script;			/* Script file descriptor */
	struct stat buf;		/* Script file information */

	if ( (i_script = open(script_nm, O_RDONLY)) == -1
		|| !(script = fdopen(i_script, "r")) ) {
	    fprintf(stderr, "%s: could not open %s for reading.\n%s\n",
		    argv0, script_nm, strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if ( fstat(i_script, &buf) == -1 ) {
	    fprintf(stderr, "%s: could not get information about %s.\n%s\n",
		    argv0, script_nm, strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if ( (buf.st_mode & S_IFMT) & S_IFIFO ) {
	    int o_script;		/* File descriptor for writing to
					   script fifo. Nothing will be
					   written to it. Having this
					   descriptor keeps process from
					   exiting when a single input
					   command sends EOF. */

	    daemon = 1;
	    if ( (o_script = open(script_nm, O_WRONLY)) == -1 ) {
		fprintf(stderr, "%s: could not open fifo %s for writng.\n%s\n",
			argv0, script_nm, strerror(errno));
		exit(EXIT_FAILURE);
	    }
	}
    }

    /*
       Load the volume
     */

    Sigmet_Vol_Init(&vol);
    lpid = -1;
    if ( !(vol_fl = vol_open(vol_fl_nm, &lpid)) ) {
	fprintf(stderr, "%s: could not open file %s for reading.\n%s\n",
		argv0, vol_fl_nm, strerror(errno));
	exit(EXIT_FAILURE);
    }
    sig_stat = Sigmet_Vol_Read(vol_fl, &vol);
    fclose(vol_fl);
    if ( lpid != -1 ) {
	waitpid(lpid, NULL, 0);
    }
    if ( sig_stat != SIGMET_OK ) {
	fprintf(stderr, "%s: could not read volume.\n%s\n",
		argv0, sigmet_err(sig_stat));
	exit(EXIT_FAILURE);
    }

    /*
       Read commands from standard input. Go to callback specified in first
       word.
     */

    while ( 1 ) {
	switch (Str_GetLn(script, '\n', &ln, &n_max)) {
	    case 1:
		if ( !(argv1 = Str_Words(ln, argv1, &argc1)) ) {
		    fprintf(stderr, "%s: could not parse %s\n", argv0, ln);
		    exit(EXIT_FAILURE);
		}
		cmd = argv1[0];
		if ( !cmd || strcmp(cmd, "#") == 0 ) {
		    continue;
		}
		n = hash(cmd);
		if ( strcmp(cmd, cmd1v[n]) != 0 ) {
		    fprintf(stderr, "%s: unknown command %s. "
			    "Subcommand must be one of ", argv0, cmd);
		    for (n = 0; n < N_HASH_CMD; n++) {
			if ( strlen(cmd1v[n]) > 0 ) {
			    fprintf(stderr, " %s", cmd1v[n]);
			}
		    }
		    fprintf(stderr, "\n");
		    exit(EXIT_FAILURE);
		}
		if ( !cb1v[n](argc1, argv1) ) {
		    fprintf(stderr, "%s: %s failed.\n", argv0, cmd);
		}
		break;
	    case 0:
		fprintf(stderr, "%s: failed to read input line.\n", argv0);
		exit(EXIT_FAILURE);
		break;
	    case EOF:
		if ( !daemon ) {
		    exit(EXIT_SUCCESS);
		}
		break;
	}
    }
    return EXIT_FAILURE;
}

static int commands_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int n;

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    for (n = 0; n < N_HASH_CMD; n++) {
	if ( strlen(cmd1v[n]) > 0 ) {
	    fprintf(out, " %s", cmd1v[n]);
	}
    }
    fprintf(out, "\n");
    return 1;
}

static int open_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *fl_nm;
    FILE *t;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s file\n", argv0);
	return 0;
    }
    fl_nm = argv[1];
    if ( !(t = fopen(fl_nm, "w")) ) {
	fprintf(stderr, "Could not open %s for writing.\n", fl_nm);
	return 0;
    }
    if ( out != stdout ) {
	fclose(out);
    }
    out = t;
    return 1;
}

static int close_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    if ( out != stdout ) {
	fclose(out);
	out = stdout;
    }
    return 1;
}

static int exit_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
    return 1;
}

static int data_types_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int y;
    char *data_type_s, *descr, *unit;

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    for (y = 0; y < Sigmet_Vol_NumTypes(&vol); y++) {
	Sigmet_Vol_DataTypeHdrs(&vol, y, &data_type_s, &descr, &unit);
	fprintf(out, "%s | %s | %s\n", data_type_s, descr, unit);
    }
    return 1;
}

static int volume_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    Sigmet_Vol_PrintHdr(out, &vol);
    return 1;
}

static int vol_hdr_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    Sigmet_Vol_PrintMinHdr(out, &vol);
    return 1;
}

static int near_sweep_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *ang_s;
    double ang;
    int s;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s angle\n", argv0);
	return 0;
    }
    ang_s = argv[1];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(stderr, "%s: expected floating point for sweep angle,"
		" got %s\n", argv0, ang_s);
	return 0;
    }
    ang *= RAD_PER_DEG;
    s = Sigmet_Vol_NearSweep(&vol, ang);
    if ( s == -1 ) {
	fprintf(stderr, "%s: could not determine sweep with "
		"sweep angle nearest %s\n", argv0, ang_s);
	return 0;
    }
    fprintf(out, "%d\n", s);
    return 1;
}

static int sweep_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int s;
    enum SigmetStatus sig_stat;
    int ok;
    double tm, ang;
    int yr, mon, da, hr, min, sec;

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    for (s = 0; s < Sigmet_Vol_NumSweeps(&vol); s++) {
	fprintf(out, "sweep %2d ", s);
	sig_stat = Sigmet_Vol_SweepHdr(&vol, s, &ok, &tm, &ang);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: %s\n", argv0, sigmet_err(sig_stat));
	}
	if ( ok ) {
	    if ( Tm_JulToCal(tm, &yr, &mon, &da, &hr, &min, &sec) ) {
		fprintf(out, "%04d/%02d/%02d %02d:%02d:%02d ",
			yr, mon, da, hr, min, sec);
	    } else {
		fprintf(out, "0000/00/00 00:00:00 ");
	    }
	    fprintf(out, "%7.3f\n", ang * DEG_PER_RAD);
	} else {
	    fprintf(out, "bad\n");
	}
    }
    return 1;
}

static int ray_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int s, r;
    int ok;
    int num_bins;
    double tm, tilt0, tilt1, az0, az1;
    int yr, mon, da, hr, min, sec;
    enum SigmetStatus sig_stat;

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    for (s = 0; s < Sigmet_Vol_NumSweeps(&vol); s++) {
	sig_stat = Sigmet_Vol_SweepHdr(&vol, s, &ok, NULL, NULL);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: %s\n", argv0, sigmet_err(sig_stat));
	}
	if ( ok ) {
	    for (r = 0; r < Sigmet_Vol_NumRays(&vol); r++) {
		sig_stat = Sigmet_Vol_RayHdr(&vol, s, r, &ok, &tm, &num_bins,
			&tilt0, &tilt1, &az0, &az1);
		if ( sig_stat != SIGMET_OK ) {
		    fprintf(stderr, "%s: %s\n", argv0, sigmet_err(sig_stat));
		}
		if ( !ok ) {
		    continue;
		}
		fprintf(out, "sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(tm, &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(stderr, "%s: bad ray time\n", argv0);
		    return 0;
		}
		fprintf(out, "%04d/%02d/%02d %02d:%02d:%02d | ",
			yr, mon, da, hr, min, sec);
		fprintf(out, "az %7.3f %7.3f | ",
			az0 * DEG_PER_RAD, az1 * DEG_PER_RAD);
		fprintf(out, "tilt %6.3f %6.3f\n",
			tilt0 * DEG_PER_RAD, tilt1 * DEG_PER_RAD);
	    }
	} else {
	    fprintf(out, "sweep %3d empty\n", s);
	}
    }
    return 1;
}

static int new_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int a;
    char *data_type_s;			/* Data type abbreviation */
    char *val_s = NULL;			/* Initial value */
    double val;
    char *descr = NULL;			/* Descriptor for new field */
    char *unit = NULL;			/* Unit for new field */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    /*
       Identify data type from command line. Fail if volume already has
       this data type.
     */

    if ( argc < 2 || argc > 8 ) {
	fprintf(stderr, "Usage: %s data_type [-d description] [-u unit] "
		"[-v value]\n", argv0);
	return 0;
    }
    data_type_s = argv[1];

    /*
       Obtain optional descriptor, units, and initial value, or use defaults.
     */

    for (a = 2; a < argc; a++) {
	if (strcmp(argv[a], "-d") == 0) {
	    descr = argv[++a];
	} else if (strcmp(argv[a], "-u") == 0) {
	    unit = argv[++a];
	} else if (strcmp(argv[a], "-v") == 0) {
	    val_s = argv[++a];
	} else {
	    fprintf(stderr, "%s: unknown option %s.\n", argv0, argv[a]);
	    return 0;
	}
    }
    if ( !descr || strlen(descr) == 0 ) {
	descr = "No description";
    }
    if ( !unit || strlen(unit) == 0 ) {
	unit = "Dimensionless";
    }
    sig_stat = Sigmet_Vol_NewField(&vol, data_type_s, descr, unit);
    if ( sig_stat != SIGMET_OK ) {
	fprintf(stderr, "%s: could not add data type %s to volume\n%s\n",
		argv0, data_type_s, sigmet_err(sig_stat));
	return 0;
    }

    /*
       If given optional value, initialize new field with it.
     */

    if ( val_s ) {
	if ( sscanf(val_s, "%lf", &val) == 1 ) {
	    sig_stat = Sigmet_Vol_Fld_SetVal(&vol, data_type_s, val);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s: could not set %s to %lf in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, data_type_s, val, sigmet_err(sig_stat));
		return 0;
	    }
	} else if ( strcmp(val_s, "r_beam") == 0 ) {
	    sig_stat = Sigmet_Vol_Fld_SetRBeam(&vol, data_type_s);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, data_type_s, val_s, sigmet_err(sig_stat));
		return 0;
	    }
	} else {
	    sig_stat = Sigmet_Vol_Fld_Copy(&vol, data_type_s, val_s);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, data_type_s, val_s, sigmet_err(sig_stat));
		return 0;
	    }
	}
    }
    return 1;
}

static int del_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *data_type_s;			/* Data type abbreviation */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s data_type\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    if ( (sig_stat = Sigmet_Vol_DelField(&vol, data_type_s)) != SIGMET_OK ) {
	fprintf(stderr, "%s: could not remove data type %s from volume\n%s\n",
		argv0, data_type_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

/*
   Print volume memory usage.
 */

static int size_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];

    if ( argc != 1 ) {
	fprintf(stderr, "Usage: %s\n", argv0);
	return 0;
    }
    fprintf(out, "%lu\n", (unsigned long)Sigmet_Vol_MemSz(&vol));
    return 1;
}

/*
   Set value for a field.
 */

static int set_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type value\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    d_s = argv[2];

    /*
       Parse value and set in data array.
       "r_beam" => set bin value to distance along bin, in meters.
       Otherwise, value must be a floating point number.
     */

    if ( strcmp("r_beam", d_s) == 0 ) {
	sig_stat = Sigmet_Vol_Fld_SetRBeam(&vol, data_type_s);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not set %s to beam range "
		    "in volume\n%s\n", argv0, data_type_s,
		    sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_SetVal(&vol, data_type_s, d);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not set %s to %lf in volume\n%s\n",
		    argv0, data_type_s, d, sigmet_err(sig_stat));
	    return 0;
	}
    } else {
	fprintf(stderr, "%s: field value must be a number or \"r_beam\"\n",
		argv0);
	return 0;
    }
    return 1;
}

/*
   Add a scalar or another field to a field.
 */

static int add_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type value|field\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    a_s = argv[2];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_AddVal(&vol, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not add %s to %lf in volume\n%s\n",
		    argv0, data_type_s, a, sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_AddFld(&vol, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s: could not add %s to %s in volume\n%s\n",
		argv0, data_type_s, a_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

/*
   Subtract a scalar or another field from a field.
 */

static int sub_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type value|field\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    a_s = argv[2];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_SubVal(&vol, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not subtract %lf from %s in "
		    "volume\n%s\n", argv0, a, data_type_s,
		    sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_SubFld(&vol, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s: could not subtract %s from %s in volume\n%s\n",
		argv0, a_s, data_type_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

/*
   Multiply a field by a scalar or another field
 */

static int mul_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type value|field\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    a_s = argv[2];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_MulVal(&vol, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not multiply %s by %lf in "
		    "volume\n%s\n", argv0, data_type_s, a,
		    sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_MulFld(&vol, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s: could not multiply %s by %s in volume\n%s\n",
		argv0, data_type_s, a_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

/*
   Divide a field by a scalar or another field
 */

static int div_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type value|field\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    a_s = argv[2];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_DivVal(&vol, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not divide %s by %lf in volume\n%s\n",
		    argv0, data_type_s, a, sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_DivFld(&vol, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s: could not divide %s by %s in volume\n%s\n",
		argv0, data_type_s, a_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

/*
   Replace a field with it's log10.
 */

static int log10_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s data_type\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    if ( (sig_stat = Sigmet_Vol_Fld_Log10(&vol, data_type_s)) != SIGMET_OK ) {
	fprintf(stderr, "%s: could not compute log10 of %s in volume\n%s\n",
		argv0, data_type_s, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

static int incr_time_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *dt_s;
    double dt;				/* Time increment, seconds */

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s dt\n", argv0);
	return 0;
    }
    dt_s = argv[1];
    if ( sscanf(dt_s, "%lf", &dt) != 1) {
	fprintf(stderr, "%s: expected float value for time increment, got %s\n",
		argv0, dt_s);
	return 0;
    }
    if ( (sig_stat = Sigmet_Vol_IncrTm(&vol, dt / 86400.0)) != SIGMET_OK ) {
	fprintf(stderr, "%s: could not increment time in volume\n%s\n",
		argv0, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

static int data_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int num_types, num_sweeps, num_rays, num_bins;
    int s, y, r, b;
    char *data_type_s;
    int all = -1;

    /*
       Identify input and desired output
       Possible forms:
       sigmet_ray data			(argc = 2)
       sigmet_ray data data_type	(argc = 3)
       sigmet_ray data data_type s	(argc = 4)
       sigmet_ray data data_type s r	(argc = 5)
       sigmet_ray data data_type s r b	(argc = 6)
     */

    data_type_s = NULL;
    y = s = r = b = all;
    if ( argc >= 2 ) {
	data_type_s = argv[1];
    }
    if ( argc >= 3 && sscanf(argv[2], "%d", &s) != 1 ) {
	fprintf(stderr, "%s: expected integer for sweep index, got %s\n",
		argv0, argv[2]);
	return 0;
    }
    if ( argc >= 4 && sscanf(argv[3], "%d", &r) != 1 ) {
	fprintf(stderr, "%s: expected integer for ray index, got %s\n",
		argv0, argv[3]);
	return 0;
    }
    if ( argc >= 5 && sscanf(argv[4], "%d", &b) != 1 ) {
	fprintf(stderr, "%s: expected integer for bin index, got %s\n",
		argv0, argv[4]);
	return 0;
    }
    if ( argc >= 6 ) {
	fprintf(stderr, "Usage: %s [[[[data_type] sweep] ray] bin]\n",
		argv0);
	return 0;
    }

    /*
       Validate.
     */

    num_types = Sigmet_Vol_NumTypes(&vol);
    if ( data_type_s
	    && (y = Sigmet_Vol_GetFld(&vol, data_type_s, NULL)) == -1 ) {
	fprintf(stderr, "%s: no data type named %s\n",
		argv0, data_type_s);
	return 0;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(&vol);
    if ( s != all && s >= num_sweeps ) {
	fprintf(stderr, "%s: sweep index %d out of range for volume\n",
		argv0, s);
	return 0;
    }
    num_rays = Sigmet_Vol_NumRays(&vol);
    if ( r != all && r >= num_rays ) {
	fprintf(stderr, "%s: ray index %d out of range for volume\n",
		argv0, r);
	return 0;
    }
    if ( b != all && b >= Sigmet_Vol_NumBins(&vol, s, -1) ) {
	fprintf(stderr, "%s: bin index %d out of range for volume\n",
		argv0, b);
	return 0;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < num_types; y++) {
	    for (s = 0; s < num_sweeps; s++) {
		Sigmet_Vol_DataTypeHdrs(&vol, y, &data_type_s, NULL, NULL);
		fprintf(out, "%s. sweep %d\n", data_type_s, s);
		for (r = 0; r < num_rays; r++) {
		    if ( Sigmet_Vol_BadRay(&vol, s, r) ) {
			continue;
		    }
		    fprintf(out, "ray %d: ", r);
		    num_bins = Sigmet_Vol_NumBins(&vol, s, r);
		    for (b = 0; b < num_bins; b++) {
			fprintf(out, "%f ",
				Sigmet_Vol_GetDatum(&vol, y, s, r, b));
		    }
		    fprintf(out, "\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < num_sweeps; s++) {
	    fprintf(out, "%s. sweep %d\n", data_type_s, s);
	    for (r = 0; r < num_rays; r++) {
		if ( Sigmet_Vol_BadRay(&vol, s, r) ) {
		    continue;
		}
		fprintf(out, "ray %d: ", r);
		num_bins = Sigmet_Vol_NumBins(&vol, s, r);
		for (b = 0; b < num_bins; b++) {
		    fprintf(out, "%f ", Sigmet_Vol_GetDatum(&vol, y, s, r, b));
		}
		fprintf(out, "\n");
	    }
	}
    } else if ( r == all && b == all ) {
	fprintf(out, "%s. sweep %d\n", data_type_s, s);
	for (r = 0; r < num_rays; r++) {
	    if ( Sigmet_Vol_BadRay(&vol, s, r) ) {
		continue;
	    }
	    fprintf(out, "ray %d: ", r);
	    num_bins = Sigmet_Vol_NumBins(&vol, s, r);
	    for (b = 0; b < num_bins; b++) {
		fprintf(out, "%f ", Sigmet_Vol_GetDatum(&vol, y, s, r, b));
	    }
	    fprintf(out, "\n");
	}
    } else if ( b == all ) {
	fprintf(out, "%s. sweep %d, ray %d: ", data_type_s, s, r);
	if ( !Sigmet_Vol_BadRay(&vol, s, r) ) {
	    num_bins = Sigmet_Vol_NumBins(&vol, s, r);
	    for (b = 0; b < num_bins; b++) {
		fprintf(out, "%f ", Sigmet_Vol_GetDatum(&vol, y, s, r, b));
	    }
	    fprintf(out, "\n");
	}
    } else {
	if ( !Sigmet_Vol_BadRay(&vol, s, r) ) {
	    fprintf(out, "%s. sweep %d, ray %d, bin %d: ",
		    data_type_s, s, r, b);
	    fprintf(out, "%f ", Sigmet_Vol_GetDatum(&vol, y, s, r, b));
	    fprintf(out, "\n");
	}
    }
    return 1;
}

/*
   Print sweep data as a binary stream.
   sigmet_ray bdata data_type s
   Each output ray will have num_output_bins floats.
   Missing values will be NAN.
 */

static int bdata_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int num_sweeps, num_bins;
    int s, y, r, b;
    char *data_type_s;
    static float *ray_p;	/* Buffer to receive ray data */
    enum SigmetStatus sig_stat;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s data_type sweep_index\n", argv0);
	return 0;
    }
    data_type_s = argv[1];
    if ( sscanf(argv[2], "%d", &s) != 1 ) {
	fprintf(stderr, "%s: expected integer for sweep index, got %s\n",
		argv0, argv[2]);
	return 0;
    }
    if ( (y = Sigmet_Vol_GetFld(&vol, data_type_s, NULL)) == -1 ) {
	fprintf(stderr, "%s: no data type named %s\n", argv0, data_type_s);
	return 0;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(&vol);
    if ( s >= num_sweeps ) {
	fprintf(stderr, "%s: sweep index %d out of range for volume\n",
		argv0, s);
	return 0;
    }
    num_bins = Sigmet_Vol_NumBins(&vol, s, -1);
    if ( num_bins == -1 ) {
	fprintf(stderr, "%s: could not get number of bins for sweep %d\n",
		argv0, s);
	return 0;
    }
    if ( !ray_p && !(ray_p = CALLOC(num_bins, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate output buffer for ray.\n");
	return 0;
    }
    for (r = 0; r < Sigmet_Vol_NumRays(&vol); r++) {
	num_bins = Sigmet_Vol_NumBins(&vol, s, r);
	for (b = 0; b < num_bins; b++) {
	    ray_p[b] = NAN;
	}
	if ( !Sigmet_Vol_BadRay(&vol, s, r) ) {
	    sig_stat = Sigmet_Vol_GetRayDat(&vol, y, s, r, &ray_p);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s: could not get ray data "
			"for data type %s, sweep index %d, ray %d.\n%s\n",
			argv0, data_type_s, s, r, sigmet_err(sig_stat));
		return 0;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins, out) != num_bins ) {
	    fprintf(stderr, "%s: could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n", argv0,
		    data_type_s, s, r, strerror(errno));
	    return 0;
	}
    }
    return 1;
}

static int bin_outline_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double cnr[8];

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s sweep ray bin\n", argv0);
	return 0;
    }
    s_s = argv[1];
    r_s = argv[2];
    b_s = argv[3];
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s: expected integer for sweep index, got %s\n",
		argv0, s_s);
	return 0;
    }
    if ( sscanf(r_s, "%d", &r) != 1 ) {
	fprintf(stderr, "%s: expected integer for ray index, got %s\n",
		argv0, r_s);
	return 0;
    }
    if ( sscanf(b_s, "%d", &b) != 1 ) {
	fprintf(stderr, "%s: expected integer for bin index, got %s\n",
		argv0, b_s);
	return 0;
    }
    if ( s >= Sigmet_Vol_NumSweeps(&vol) ) {
	fprintf(stderr, "%s: sweep index %d out of range for volume\n",
		argv0, s);
	return 0;
    }
    if ( r >= Sigmet_Vol_NumRays(&vol) ) {
	fprintf(stderr, "%s: ray index %d out of range for volume\n",
		argv0, r);
	return 0;
    }
    if ( b >= Sigmet_Vol_NumBins(&vol, s, r) ) {
	fprintf(stderr, "%s: bin index %d out of range for volume\n",
		argv0, b);
	return 0;
    }
    if ( Sigmet_Vol_IsPPI(&vol) ) {
	if ( !set_proj() ) {
	    fprintf(stderr, "%s: could not set geographic projection.\n",
		    argv0);
	    return 0;
	}
	sig_stat = Sigmet_Vol_PPI_BinOutl(&vol, s, r, b, lonlat_to_xy, cnr);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not compute bin outlines for bin "
		    "%d %d %d in volume\n%s\n", argv0, s, r, b,
		    sigmet_err(sig_stat));
	    return 0;
	}
    } else if ( Sigmet_Vol_IsRHI(&vol) ) {
	sig_stat = Sigmet_Vol_RHI_BinOutl(&vol, s, r, b, cnr);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not compute bin outlines for bin "
		    "%d %d %d in volume\n%s\n", argv0, s, r, b,
		    sigmet_err(sig_stat));
	    return 0;
	}
    } else {
    }
    fprintf(out, "%f %f %f %f %f %f %f %f\n",
	    cnr[0] * DEG_RAD, cnr[1] * DEG_RAD,
	    cnr[2] * DEG_RAD, cnr[3] * DEG_RAD,
	    cnr[4] * DEG_RAD, cnr[5] * DEG_RAD,
	    cnr[6] * DEG_RAD, cnr[7] * DEG_RAD);
    return 1;
}

/*
   Set geographic projection.
 */

static int set_proj(void)
{
    double lon, lat;			/* Radar location */
    char *proj_s;			/* Environment projection description */
    char dflt_proj_s[LEN];		/* Default projection description */

    if ( (proj_s = getenv(SIGMET_GEOG_PROJ)) ) {
	if ( !Sigmet_Proj_Set(proj_s) ) {
	    fprintf(stderr, "Could not set projection from "
		    SIGMET_GEOG_PROJ " environment variable.\n");
	    return 0;
	}
    } else {
	lon = Sigmet_Vol_RadarLon(&vol, NULL);
	lat = Sigmet_Vol_RadarLat(&vol, NULL);
	if ( snprintf(dflt_proj_s, LEN,
		    "CylEqDist %.9g %.9g", lon, lat) > LEN
		|| !Sigmet_Proj_Set(dflt_proj_s) ) {
	    fprintf(stderr, "Could not set default projection.\n");
	    return 0;
	}
    }
    return 1;
}

/*
   Print sweep limits. If ppi, print map coordinates. Map projection can be
   specified with SIGMET_GEOG_PROJ environment variable, otherwise projection
   defaults to cylindrical equidistant with no distortion at radar. If rhi,
   print distance down range and height above ground level.
 */

static int sweep_bnds_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    double x_min, x_max, y_min, y_max;
    char *sweep_s;
    int s;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s sweep\n", argv0);
	return 0;
    }
    sweep_s = argv[1];
    if ( sscanf(sweep_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s: expected integer for sweep index, got %s.\n",
		argv0, sweep_s);
    }
    if ( Sigmet_Vol_IsPPI(&vol) ) {
	if ( !set_proj() ) {
	    fprintf(stderr, "%s: could not set geographic projection.\n",
		    argv0);
	    return 0;
	}
	if ( Sigmet_Vol_PPI_Bnds(&vol, s, lonlat_to_xy,
		    &x_min, &x_max, &y_min, &y_max) != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not compute PPI boundaries.\n",
		    argv0);
	    return 0;
	}
    } else if ( Sigmet_Vol_IsRHI(&vol) ) {
	x_min = y_min = 0.0;
	if ( Sigmet_Vol_RHI_Bnds(&vol, s, &x_max, &y_max) != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not compute PPI boundaries.\n",
		    argv0);
	    return 0;
	}
    }
    fprintf(out, "x_min %lf x_max %lf y_min %lf y_max %lf\n",
	    x_min, x_max, y_min, y_max);
    return 1;
}

static int radar_lon_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *lon_s;			/* New longitude, degrees, in argv */
    double lon;				/* New longitude, degrees */

    if ( argc == 1 ) {
	printf("%lf\n", Sigmet_Vol_RadarLon(&vol, NULL) * DEG_PER_RAD);
	return 1;
    } else if ( argc == 2 ) {
	lon_s = argv[1];
	if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	    fprintf(stderr, "%s: expected floating point value for "
		    "new longitude, got %s\n", argv0, lon_s);
	    return 0;
	}
	lon = GeogLonR(lon * RAD_PER_DEG, M_PI);
	printf("%lf\n", Sigmet_Vol_RadarLon(&vol, &lon) * DEG_PER_RAD);
	return 1;
    } else {
	fprintf(stderr, "Usage: %s new_lon\n", argv0);
	return 0;
    }
}

static int radar_lat_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *lat_s;			/* New latitude, degrees, in argv */
    double lat;				/* New latitude, degrees */

    if ( argc == 1 ) {
	printf("%lf\n", Sigmet_Vol_RadarLat(&vol, NULL) * DEG_PER_RAD);
	return 1;
    } else if ( argc == 2 ) {
	lat_s = argv[1];
	if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	    fprintf(stderr, "%s: expected floating point value for "
		    "new latitude, got %s\n", argv0, lat_s);
	    return 0;
	}
	lat = GeogLatN(lat * RAD_PER_DEG);
	printf("%lf\n", Sigmet_Vol_RadarLat(&vol, &lat) * DEG_PER_RAD);
	return 1;
    } else {
	fprintf(stderr, "Usage: %s new_lat\n", argv0);
	return 0;
    }
}

static int shift_az_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *daz_s;			/* Degrees to add to each azimuth */
    double daz;				/* Radians to add to each azimuth */
    enum SigmetStatus sig_stat;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s dz\n", argv0);
	return 0;
    }
    daz_s = argv[1];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(stderr, "%s: expected float value for azimuth shift, "
		"got %s\n", argv0, daz_s);
	return 0;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, M_PI);
    if ( (sig_stat = Sigmet_Vol_ShiftAz(&vol, daz)) != SIGMET_OK ) {
	fprintf(stderr, "%s: failed to shift azimuths.\n%s\n",
		argv0, sigmet_err(sig_stat));
	return 0;
    }
    return 1;
}

static int outlines_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int num_rays, num_bins;
    int ppi;				/* If true, volume is ppi */
    int fill;				/* If true, fill space between rays */
    char *data_type_s;			/* Data type abbreviation */
    char *clr_fl_nm;			/* Name of file with color specifiers
					   and bounds */
    FILE *clr_fl;			/* Input stream for color file */
    int num_colors;			/* Number of colors */
    int num_bnds;			/* Number of boundaries */
    char *s_s;				/* Sweep index, as a string */
    int s;				/* Sweep index */
    char (*colors)[COLOR_NM_LEN_A] = NULL; /* Color names, e.g. "#rrggbb" */
    char bnd[FLOAT_STR_LEN_A];		/* String representation of a boundary
					   value, .e.g "1.23", or "-INF" */
    float *bnds = NULL;			/* Bounds for each color */
    char *format;			/* Conversion specifier */
    double r00, dr;			/* Range to first bin, bin step, m. */
    double *az0 = NULL, *az1;		/* Ray start, stop azimuths, radians */
    double *tilt0, *tilt1;		/* Ray start, stop tilts, radians */
    double a0, a1, tl0, tl1;		/* Members of az0, az1, tilt0, tilt1 */
    double r0, r1;			/* Distance along beam to start, end of 
					   a bin */
    double r0_g, r1_g;			/* Distance along ground to point under
					   start, end of a bin */
    double lon_r, lat_r;		/* Radar longitude, latitude */
    double lon, lat;			/* PPI bin corner location */
    double ord;				/* Ordinate */
    double tilt;			/* Median tilt */
    double re;				/* Earth radius, m */
    float *dat = NULL, *d_p;		/* Sweep data, point into dat */
    int d;				/* Index in dat */
    int c;				/* Color index */
    int y, r, b;			/* Data type, ray, bin index */
    int *lists = NULL;			/* Linked lists of gate indeces */
    double cnr[8];			/* Corner coordinates for a bin */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    if ( argc == 4 ) {
	fill = 0;
	data_type_s = argv[1];
	clr_fl_nm = argv[2];
	s_s = argv[3];
    } else if ( argc == 5 && strcmp(argv[1], "-f") == 0 ) {
	data_type_s = argv[2];
	clr_fl_nm = argv[3];
	s_s = argv[4];
	fill = 1;
    } else {
	fprintf(stderr, "Usage: %s [-f] data_type color_file sweep\n",
		argv0);
	return 0;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s: expected integer for sweep index, got %s\n",
		argv0, s_s);
	return 0;
    }

    /*
       Get colors from color file.
       Format --
       number_of_colors bound color bound color ... color bound

       Number of colors must be a positive integer
       First bound must be "-INF" or a float value
       Last bound must be a float value or "INF"
       All other bounds must be float values.
       Colors are strings with up to COLOR_NM_LEN - 1 characters.
     */

    if ( !(clr_fl = fopen(clr_fl_nm, "r")) ) {
	fprintf(stderr, "%s: could not open %s for reading.\n%s\n",
		argv0, clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( fscanf(clr_fl, " %d", &num_colors) != 1 ) {
	fprintf(stderr, "%s: could not get color count from %s.\n%s\n",
		argv0, clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( num_colors < 1) {
	fprintf(stderr, "%s: must have more than one color.\n%s\n",
		argv0, strerror(errno));
	goto error;
    }
    num_bnds = num_colors + 1;
    if ( !(colors = CALLOC((size_t)num_colors, COLOR_NM_LEN_A)) ) {
	fprintf(stderr, "%s: could not allocate %d colors.\n",
		argv0, num_colors);
	goto error;
    }
    if ( !(bnds = CALLOC((size_t)(num_bnds), sizeof(double))) ) {
	fprintf(stderr, "%s: could not allocate %d color table bounds.\n",
		argv0, num_bnds);
	goto error;
    }

    /*
       First bound and color.
     */

    format = " %" FLOAT_STR_LEN_S "s %" COLOR_NM_LEN_S "s";
    if ( fscanf(clr_fl, format, bnd, colors) == 2 ) {
	if ( strcmp(bnd, "-INF") == 0 ) {
	    bnds[0] = -FLT_MAX;
	} else if ( sscanf(bnd, "%f", bnds) == 1 ) {
	    ;
	} else {
	    fprintf(stderr, "%s: reading first color, expected number or "
		    "\"-INF\" for minimum value, got %s.\n", argv0, bnd);
	    goto error;
	}
    } else {
	fprintf(stderr, "%s: could not read first color and bound from %s.\n",
		argv0, clr_fl_nm);
	goto error;
    }

    /*
       Second through next to last bounds and colors.
     */

    format = " %f %" COLOR_NM_LEN_S "s";
    for (c = 1; c < num_colors; c++) {
	if ( fscanf(clr_fl, format, bnds + c, colors + c) != 2 ) {
	    fprintf(stderr, "%s: could not read color and bound at index %d "
		    "from %s.\n", argv0, c, clr_fl_nm);
	    goto error;
	}
    }

    /*
       Last bound.
     */

    format = " %" FLOAT_STR_LEN_S "s";
    if ( fscanf(clr_fl, format, bnd) == 1 ) {
	if ( sscanf(bnd, "%f", bnds + c) == 1 ) {
	    ;
	} else if ( strcmp(bnd, "INF") == 0 ) {
	    bnds[c] = FLT_MAX;
	} else {
	    fprintf(stderr, "%s: reading final color, expected number or "
		    "\"INF\" for boundary, got %s\n", argv0, bnd);
	    goto error;
	}
    } else {
	fprintf(stderr, "%s: could not read final bound from %s\n",
		argv0, clr_fl_nm);
	goto error;
    }

    /*
       Get sweep data and ray geometry.
     */

    num_rays = Sigmet_Vol_NumRays(&vol);
    num_bins = Sigmet_Vol_NumBins(&vol, s, -1);
    if ( num_rays == -1 || num_bins == -1 ) {
	fprintf(stderr, "%s: could not get sweep geometry %d\n", argv0, s);
	goto error;
    }
    ppi = Sigmet_Vol_IsPPI(&vol);
    if ( ppi && !set_proj() ) {
	fprintf(stderr, "%s: could not set geographic projection.\n", argv0);
	goto error;
    }
    lon_r = Sigmet_Vol_RadarLon(&vol, NULL);
    lat_r = Sigmet_Vol_RadarLat(&vol, NULL);
    if ( (y = Sigmet_Vol_GetFld(&vol, data_type_s, NULL)) == -1 ) {
	fprintf(stderr, "%s: volume has no data type named %s\n",
		argv0, data_type_s);
	goto error;
    }
    if ( !(az0 = CALLOC(4 * num_rays, sizeof(double))) ) {
	fprintf(stderr, "%s: failed to allocate memory for ray limits "
		"for %d rays.\n", argv0, num_rays);
	goto error;
    }
    az1 = az0 + num_rays;
    tilt0 = az1 + num_rays;
    tilt1 = tilt0 + num_rays;
    sig_stat = Sigmet_Vol_RayGeom(&vol, s, &r00, &dr, az0, az1, tilt0, tilt1,
	    fill);
    if ( sig_stat != SIGMET_OK ) {
	fprintf(stderr, "%s: could not get ray geometry.\n%s\n",
		argv0, sigmet_err(sig_stat));
    }
    if ( !(dat = CALLOC(num_rays * num_bins, sizeof(float))) ) {
	fprintf(stderr, "%s: could not allocate memory for data for "
		"sweep with %d rays, %d bins.\n",
		argv0, num_rays, num_bins);
	goto error;
    }
    for (r = 0, d_p = dat; r < num_rays; r++, d_p += num_bins) {
	for (b = 0; b < num_bins; b++) {
	    d_p[b] = NAN;
	}
	if ( Sigmet_Vol_GetRayDat(&vol, y, s, r, &d_p) ) {
	    fprintf(stderr, "%s: could not get data for ray %d.\n",
		    argv0, r);
	    goto error;
	}
    }
    lists = CALLOC((size_t)(num_bnds + num_rays * num_bins), sizeof(int));
    if ( !lists ) {
	fprintf(stderr, "%s: could not allocate color lists.\n", argv0);
	goto error;
    }

    /*
       Print outlines of gates for each color.
       Skip TRANSPARENT color.
     */

    BiSearch_FDataToList(dat, num_rays * num_bins, bnds, num_bnds, lists);
    for (c = 0; c < num_colors; c++) {
	if ( strcmp(colors[c], "none") != 0 ) {
	    fprintf(out, "color %s\n", colors[c]);
	    for (d = BiSearch_1stIndex(lists, c);
		    d != -1;
		    d = BiSearch_NextIndex(lists, d)) {
		r = d / num_bins;
		b = d % num_bins;
		r0 = r00 + b * dr;
		r1 = r0 + dr;
		if ( ppi ) {
		    a0 = az0[r];
		    a1 = az1[r];
		    tl0 = tilt0[r];
		    tl1 = tilt1[r];
		    if ( GeogLonR(a1, a0) > a0 ) {
			double t = a1;
			a1 = a0;
			a0 = t;
		    }
		    tilt = 0.5 * (tl0 + tl1);
		    re = GeogREarth(NULL);
		    r0_g = atan(r0 * cos(tilt) / (re + r0 * sin(tilt)));
		    r1_g = atan(r1 * cos(tilt) / (re + r1 * sin(tilt)));
		    GeogStep(lon_r, lat_r, a0, r0_g, &lon, &lat);
		    lonlat_to_xy(lon, lat, cnr + 0, cnr + 1);
		    GeogStep(lon_r, lat_r, a0, r1_g, &lon, &lat);
		    lonlat_to_xy(lon, lat, cnr + 2, cnr + 3);
		    GeogStep(lon_r, lat_r, a1, r1_g, &lon, &lat);
		    lonlat_to_xy(lon, lat, cnr + 4, cnr + 5);
		    GeogStep(lon_r, lat_r, a1, r0_g, &lon, &lat);
		    lonlat_to_xy(lon, lat, cnr + 6, cnr + 7);
		} else {
		    tl0 = tilt0[r];
		    tl1 = tilt1[r];
		    if ( tl1 < tl0 ) {
			double t = tl1;
			tl1 = tl0;
			tl0 = t;
		    }
		    re = GeogREarth(NULL) * 4.0 / 3.0;
		    cnr[1] = ord = GeogBeamHt(r0, tl0, re);
		    cnr[0] = re * asin(r0 * cos(tl0) / (re + ord));
		    cnr[3] = ord = GeogBeamHt(r1, tl0, re);
		    cnr[2] = re * asin(r1 * cos(tl0) / (re + ord));
		    cnr[5] = ord = GeogBeamHt(r1, tl1, re);
		    cnr[4] = re * asin(r1 * cos(tl1) / (re + ord));
		    cnr[7] = ord = GeogBeamHt(r0, tl1, re);
		    cnr[6] = re * asin(r0 * cos(tl1) / (re + ord));
		}
		fprintf(out, "gate %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n",
			cnr[0], cnr[1], cnr[2], cnr[3], cnr[4],
			cnr[5], cnr[6], cnr[7]);
	    }
	}
    }

    FREE(colors);
    FREE(bnds);
    FREE(lists);
    FREE(az0);
    FREE(dat);
    return 1;

error:
    FREE(colors);
    FREE(bnds);
    FREE(lists);
    FREE(az0);
    FREE(dat);
    return 0;
}

static int dorade_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    int num_sweeps;
    int s;				/* Index of desired sweep,
					   or -1 for all */
    char *s_s;				/* String representation of s */
    int all = -1;
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    struct Dorade_Sweep swp;

    if ( argc == 1 ) {
	s = all;
    } else if ( argc == 2 ) {
	s_s = argv[1];
	if ( strcmp(s_s, "all") == 0 ) {
	    s = all;
	} else if ( sscanf(s_s, "%d", &s) != 1 ) {
	    fprintf(stderr, "%s: expected integer for sweep index, got \"%s"
		    "\"\n", argv0, s_s);
	    return 0;
	}
    } else {
	fprintf(stderr, "Usage: %s [s]\n", argv0);
	return 0;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(&vol);
    if ( s >= num_sweeps ) {
	fprintf(stderr, "%s: sweep index %d out of range for volume\n",
		argv0, s);
	return 0;
    }
    if ( s == all ) {
	for (s = 0; s < num_sweeps; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (sig_stat = Sigmet_Vol_ToDorade(&vol, s, &swp)) != SIGMET_OK ) {
		fprintf(stderr, "%s: could not translate sweep %d of volume "
			"to DORADE format\n%s\n", argv0, s,
			sigmet_err(sig_stat));
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(stderr, "%s: could not write DORADE file for sweep "
			"%d of volume\n", argv0, s);
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (sig_stat = Sigmet_Vol_ToDorade(&vol, s, &swp)) != SIGMET_OK ) {
	    fprintf(stderr, "%s: could not translate sweep %d of volume to "
		    "DORADE format\n%s\n", argv0, s,
		    sigmet_err(sig_stat));
	    goto error;
	}
	if ( !Dorade_Sweep_Write(&swp) ) {
	    fprintf(stderr, "%s: could not write DORADE file for sweep "
		    "%d of volume\n", argv0, s);
	    goto error;
	}
	Dorade_Sweep_Free(&swp);
    }

    return 1;

error:
    Dorade_Sweep_Free(&swp);
    return 0;
}

static char *sigmet_err(enum SigmetStatus s)
{
    switch (s) {
	case SIGMET_OK:
	    return "Success.";
	    break;
	case SIGMET_IO_FAIL:
	    return "Input/output failure.";
	    break;
	case SIGMET_BAD_FILE:
	    return "Bad file.";
	    break;
	case SIGMET_BAD_VOL:
	    return "Bad volume.";
	    break;
	case SIGMET_MEM_FAIL:
	    return "Memory failure.";
	    break;
	case SIGMET_BAD_ARG:
	    return "Bad argument.";
	    break;
	case SIGMET_RNG_ERR:
	    return "Value out of range.";
	    break;
	case SIGMET_BAD_TIME:
	    return "Bad time.";
	    break;
	case SIGMET_HELPER_FAIL:
	    return "Helper application failed.";
	    break;
    }
    return "Unknown error";
}

/*
   Open volume file vol_nm.  If vol_nm suffix indicates a compressed file, open
   a pipe to a decompression process.  Return a file handle to the file or
   decompression process, or NULL if failure. If return value is output from a
   decompression process, put the process id at pid_p. Set *pid_p to -1 if
   there is no decompression process (i.e. vol_nm is a plain file).
 */

static FILE *vol_open(const char *vol_nm, pid_t *pid_p)
{
    FILE *in = NULL;		/* Return value */
    char *sfx;			/* Filename suffix */
    int pfd[2] = {-1};		/* Pipe for data */
    pid_t ch_pid = -1;		/* Child process id */

    *pid_p = -1;
    if ( strcmp(vol_nm, "-") == 0 ) {
	return stdin;
    }
    sfx = strrchr(vol_nm, '.');
    if ( sfx && sfx == vol_nm + strlen(vol_nm) - strlen(".gz")
	    && strcmp(sfx, ".gz") == 0 ) {
	if ( pipe(pfd) == -1 ) {
	    fprintf(stderr, "Could not create pipe for gzip\n%s\n",
		    strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(stderr, "Could not spawn gzip\n");
		goto error;
	    case 0: /* Child process - gzip */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    fprintf(stderr, "gzip process failed\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default: /* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(stderr, "Could not read gzip process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( sfx && sfx == vol_nm + strlen(vol_nm) - strlen(".bz2")
	    && strcmp(sfx, ".bz2") == 0 ) {
	if ( pipe(pfd) == -1 ) {
	    fprintf(stderr, "Could not create pipe for bzip2\n%s\n",
		    strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(stderr, "Could not spawn bzip2\n");
		goto error;
	    case 0: /* Child process - bzip2 */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    fprintf(stderr, "could not set up bzip2 process");
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default: /* This process.  Read output from bzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(stderr, "Could not read bzip2 process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	fprintf(stderr, "Could not open %s\n%s\n", vol_nm, strerror(errno));
	return NULL;
    }
    return in;

error:
    if ( ch_pid != -1 ) {
	kill(ch_pid, SIGTERM);
	ch_pid = -1;
    }
    if ( in ) {
	fclose(in);
	pfd[0] = -1;
    }
    if ( pfd[0] != -1 ) {
	close(pfd[0]);
    }
    if ( pfd[1] != -1 ) {
	close(pfd[1]);
    }
    return NULL;
}

/*
   Basic signal management.

   Reference --
   Rochkind, Marc J., "Advanced UNIX Programming, Second Edition",
   2004, Addison-Wesley, Boston.
 */

int handle_signals(void)
{
    sigset_t set;
    struct sigaction act;

    if ( sigfillset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    memset(&act, 0, sizeof(struct sigaction));
    if ( sigfillset(&act.sa_mask) == -1 ) {
	perror(NULL);
	return 0;
    }

    /*
       Signals to ignore
     */

    act.sa_handler = SIG_IGN;
    if ( sigaction(SIGHUP, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGINT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGQUIT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGPIPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    /*
       Generic action for termination signals
     */

    act.sa_handler = handler;
    if ( sigaction(SIGTERM, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGFPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGSYS, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXCPU, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXFSZ, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigemptyset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    return 1;
}

/*
   For exit signals, print an error message if possible.
 */

void handler(int signum)
{
    char *msg;
    int status = EXIT_FAILURE;

    msg = "sigmet_raw command exiting                          \n";
    switch (signum) {
	case SIGQUIT:
	    msg = "sigmet_raw command exiting on quit signal           \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGTERM:
	    msg = "sigmet_raw command exiting on termination signal    \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGFPE:
	    msg = "sigmet_raw command exiting arithmetic exception     \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGSYS:
	    msg = "sigmet_raw command exiting on bad system call       \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXCPU:
	    msg = "sigmet_raw command exiting: CPU time limit exceeded \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXFSZ:
	    msg = "sigmet_raw command exiting: file size limit exceeded\n";
	    status = EXIT_FAILURE;
	    break;
    }
    _exit(write(STDERR_FILENO, msg, 53) == 53 ?  status : EXIT_FAILURE);
}
