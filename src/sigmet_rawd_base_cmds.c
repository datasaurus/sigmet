/*
   -	sigmet_rawd_base_cmds.c --
   -		This file defines callback functions for the sigmet_raw
   -		daemon commands.
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
   .	$Revision: 1.4 $ $Date: 2012/01/23 18:03:49 $
 */

#include <limits.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "alloc.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "data_types.h"
#include "sigmet.h"
#include "sigmet_raw.h"

/*
   Callbacks for the subcommands. Subcommand is the word on the command
   line (sent to the socket) after "sigmet_raw". The SigmetRaw_Callback is the
   subcommand name with a "_cb" suffix.
 */

#define NCMD 26
static SigmetRaw_Callback pid_cb;
static SigmetRaw_Callback data_types_cb;
static SigmetRaw_Callback new_data_type_cb;
static SigmetRaw_Callback volume_headers_cb;
static SigmetRaw_Callback vol_hdr_cb;
static SigmetRaw_Callback near_sweep_cb;
static SigmetRaw_Callback sweep_headers_cb;
static SigmetRaw_Callback ray_headers_cb;
static SigmetRaw_Callback new_field_cb;
static SigmetRaw_Callback del_field_cb;
static SigmetRaw_Callback size_cb;
static SigmetRaw_Callback set_field_cb;
static SigmetRaw_Callback add_cb;
static SigmetRaw_Callback sub_cb;
static SigmetRaw_Callback mul_cb;
static SigmetRaw_Callback div_cb;
static SigmetRaw_Callback log10_cb;
static SigmetRaw_Callback incr_time_cb;
static SigmetRaw_Callback data_cb;
static SigmetRaw_Callback bdata_cb;
static SigmetRaw_Callback bin_outline_cb;
static SigmetRaw_Callback radar_lon_cb;
static SigmetRaw_Callback radar_lat_cb;
static SigmetRaw_Callback shift_az_cb;
static SigmetRaw_Callback outlines_cb;
static SigmetRaw_Callback dorade_cb;
static char *cmd1v[NCMD] = {
    "pid", "data_types", "new_data_type",
    "volume_headers", "vol_hdr", "near_sweep", "sweep_headers",
    "ray_headers", "new_field", "del_field", "size", "set_field", "add",
    "sub", "mul", "div", "log10", "incr_time", "data", "bdata",
    "bin_outline", "radar_lon", "radar_lat", "shift_az",
    "outlines", "dorade"
};
static SigmetRaw_Callback *cb1v[NCMD] = {
    pid_cb, data_types_cb, new_data_type_cb, 
    volume_headers_cb, vol_hdr_cb, near_sweep_cb, sweep_headers_cb,
    ray_headers_cb, new_field_cb, del_field_cb, size_cb, set_field_cb, add_cb,
    sub_cb, mul_cb, div_cb, log10_cb, incr_time_cb, data_cb, bdata_cb,
    bin_outline_cb, radar_lon_cb, radar_lat_cb, shift_az_cb,
    outlines_cb, dorade_cb
};

int SigmetRaw_AddBaseCmds(void)
{
    int status = SIGMET_OK;	/* Exit status */
    int n;

    for (n = 0; n < NCMD; n++) {
	if ( (status = SigmetRaw_AddCmd(cmd1v[n], cb1v[n])) != SIGMET_OK ) {
	    Err_Append("Could not add ");
	    Err_Append(cmd1v[n]);
	    Err_Append(" command. ");
	}
    }
    return status;
}

static int pid_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    fprintf(out, "%d\n", getpid());
    return SIGMET_OK;
}

static int new_data_type_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *name, *desc, *unit;

    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s name descriptor unit socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    name = argv[2];
    desc = argv[3];
    unit = argv[4];
    switch (DataType_Add(name, desc, unit, DATA_TYPE_FLT, DataType_DblToDbl)) {
	case DATATYPE_ALLOC_FAIL:
	    return SIGMET_ALLOC_FAIL;
	case DATATYPE_INPUT_FAIL:
	    return SIGMET_IO_FAIL;
	case DATATYPE_BAD_ARG:
	    return SIGMET_BAD_ARG;
	case DATATYPE_SUCCESS:
	    return SIGMET_OK;
    }
    return SIGMET_OK;
}

static int data_types_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    struct DataType *data_type;		/* Information about a data type */
    size_t n;
    char **abbrvs, **a;

    abbrvs = DataType_Abbrvs(&n);
    if ( abbrvs ) {
	for (a = abbrvs; a < abbrvs + n; a++) {
	    data_type = DataType_Get(*a);
	    assert(data_type);
	    fprintf(out, "%s | %s | %s | ",
		    *a, data_type->descr, data_type->unit);
	    if ( Hash_Get(&vol_p->types_tbl, *a) ) {
		fprintf(out, "present\n");
	    } else {
		fprintf(out, "unused\n");
	    }
	}
    }

    return SIGMET_OK;
}

static int volume_headers_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    Sigmet_Vol_PrintHdr(out, vol_p);
    return SIGMET_OK;
}

static int vol_hdr_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    fprintf(out, "site_name=\"%s\"\n", vol_p->ih.ic.su_site_name);
    fprintf(out, "radar_lon=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD);
    fprintf(out, "radar_lat=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.latitude), 0.0) * DEG_PER_RAD);
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	    fprintf(out, "scan_mode=\"ppi sector\"\n");
	    break;
	case RHI:
	    fprintf(out, "scan_mode=rhi\n");
	    break;
	case MAN_SCAN:
	    fprintf(out, "scan_mode=manual\n");
	    break;
	case PPI_C:
	    fprintf(out, "scan_mode=\"ppi continuous\"\n");
	    break;
	case FILE_SCAN:
	    fprintf(out, "scan_mode=file\n");
	    break;
    }
    fprintf(out, "task_name=\"%s\"\n", vol_p->ph.pc.task_name);
    fprintf(out, "types=\"");
    if ( vol_p->dat && vol_p->dat[0].data_type->abbrv ) {
	fprintf(out, "%s", vol_p->dat[0].data_type->abbrv);
    }
    for (y = 1; y < vol_p->num_types; y++) {
	if ( vol_p->dat[y].data_type->abbrv ) {
	    fprintf(out, " %s", vol_p->dat[y].data_type->abbrv);
	}
    }
    fprintf(out, "\"\n");
    fprintf(out, "num_sweeps=%d\n", vol_p->ih.ic.num_sweeps);
    fprintf(out, "num_rays=%d\n", vol_p->ih.ic.num_rays);
    fprintf(out, "num_bins=%d\n", vol_p->ih.tc.tri.num_bins_out);
    fprintf(out, "range_bin0=%d\n", vol_p->ih.tc.tri.rng_1st_bin);
    fprintf(out, "bin_step=%d\n", vol_p->ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * vol_p->ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = vol_p->ih.tc.tdi.prf;
    mp = vol_p->ih.tc.tdi.m_prf_mode;
    vel_ua = -1.0;
    switch (mp) {
	case ONE_ONE:
	    mp_s = "1:1";
	    vel_ua = 0.25 * wavlen * prf;
	    break;
	case TWO_THREE:
	    mp_s = "2:3";
	    vel_ua = 2 * 0.25 * wavlen * prf;
	    break;
	case THREE_FOUR:
	    mp_s = "3:4";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
	case FOUR_FIVE:
	    mp_s = "4:5";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
    }
    fprintf(out, "prf=%.2lf\n", prf);
    fprintf(out, "prf_mode=%s\n", mp_s);
    fprintf(out, "vel_ua=%.3lf\n", vel_ua);
    return SIGMET_OK;
}

static int near_sweep_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;		/* Sweep angle, degrees */
    double ang;			/* Angle from command line */
    double da_min;		/* Angle difference, smallest difference */
    int s, nrst;

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s angle socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    ang_s = argv[2];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(err, "%s %s: expected floating point for sweep angle,"
		" got %s\n", argv0, argv1, ang_s);
	return SIGMET_BAD_ARG;
    }
    ang *= RAD_PER_DEG;
    if ( !vol_p->sweep_angle ) {
	fprintf(err, "%s %s: sweep angles not loaded. "
		"Is volume truncated?.\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    nrst = -1;
    for (da_min = DBL_MAX, s = 0; s < vol_p->num_sweeps_ax; s++) {
	double swang, da;	/* Sweep angle, angle difference */

	swang = GeogLonR(vol_p->sweep_angle[s], ang);
	da = fabs(swang - ang);
	if ( da < da_min ) {
	    da_min = da;
	    nrst = s;
	}
    }
    fprintf(out, "%d\n", nrst);
    return SIGMET_OK;
}

static int sweep_headers_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < vol_p->ih.tc.tni.num_sweeps; s++) {
	fprintf(out, "sweep %2d ", s);
	if ( !vol_p->sweep_ok[s] ) {
	    fprintf(out, "bad\n");
	} else {
	    int yr, mon, da, hr, min, sec;

	    if ( Tm_JulToCal(vol_p->sweep_time[s],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		fprintf(out, "%04d/%02d/%02d %02d:%02d:%02d ",
			yr, mon, da, hr, min, sec);
	    } else {
		fprintf(out, "0000/00/00 00:00:00 ");
	    }
	    fprintf(out, "%7.3f\n", vol_p->sweep_angle[s] * DEG_PER_RAD);
	}
    }
    return SIGMET_OK;
}

static int ray_headers_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s, r;

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		int yr, mon, da, hr, min, sec;

		if ( !vol_p->ray_ok || !vol_p->ray_ok[s][r] ) {
		    continue;
		}
		fprintf(out, "sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(vol_p->ray_time[s][r],
			    &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(err, "%s %s: bad ray time\n",
			    argv0, argv1);
		    return SIGMET_BAD_TIME;
		}
		fprintf(out, "%04d/%02d/%02d %02d:%02d:%02d | ",
			yr, mon, da, hr, min, sec);
		fprintf(out, "az %7.3f %7.3f | ",
			vol_p->ray_az0[s][r] * DEG_PER_RAD,
			vol_p->ray_az1[s][r] * DEG_PER_RAD);
		fprintf(out, "tilt %6.3f %6.3f\n",
			vol_p->ray_tilt0[s][r] * DEG_PER_RAD,
			vol_p->ray_tilt1[s][r] * DEG_PER_RAD);
	    }
	}
    }
    return SIGMET_OK;
}

static int new_field_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;			/* Data type abbreviation */
    char *d_s = NULL;			/* Initial value */
    double d;
    int status;				/* Result of a function */

    if ( argc == 3 ) {
	abbrv = argv[2];
    } else if ( argc == 4 ) {
	abbrv = argv[2];
	d_s = argv[3];
    } else {
	fprintf(err, "Usage: %s %s data_type [value] socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: No data type named %s. Please add with the "
		"new_data_type command.\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_NewField(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(err, "%s %s: could not add data type %s to volume\n",
		argv0, argv1, abbrv);
	return status;
    }
    if ( d_s ) {
	if ( sscanf(d_s, "%lf", &d) == 1 ) {
	    status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, d);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "%s %s: could not set %s to %lf in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d);
		return status;
	    }
	} else if ( strcmp(d_s, "r_beam") == 0 ) {
	    status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "%s %s: could not set %s to %s in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d_s);
		return status;
	    }
	} else {
	    status = Sigmet_Vol_Fld_Copy(vol_p, abbrv, d_s);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "%s %s: could not set %s to %s in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, d_s);
		return status;
	    }
	}
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

static int del_field_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *abbrv;			/* Data type abbreviation */
    int status;				/* Result of a function */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s data_type socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: No data type named %s.\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_DelField(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(err, "%s %s: could not remove data type %s from volume\n",
		argv0, argv1, abbrv);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Print volume memory usage.
 */

static int size_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(err, "Usage: %s %s socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    fprintf(out, "%lu\n", (unsigned long)vol_p->size);
    return SIGMET_OK;
}

/*
   Set value for a field.
 */

static int set_field_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s data_type value socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    d_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }

    /*
       Parse value and set in data array.
       "r_beam" => set bin value to distance along bin, in meters.
       Otherwise, value must be a floating point number.
     */

    if ( strcmp("r_beam", d_s) == 0 ) {
	if ( (status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not set %s to beam range in volume\n",
		    argv0, argv1, abbrv);
	    return status;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, d)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not set %s to %lf in volume\n",
		    argv0, argv1, abbrv, d);
	    return status;
	}
    } else {
	fprintf(err, "%s %s: field value must be a number or \"r_beam\"\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Add a scalar or another field to a field.
 */

static int add_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s type value|field socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_AddVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not add %s to %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_AddFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(err, "%s %s: could not add %s to %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Subtract a scalar or another field from a field.
 */

static int sub_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s data_type value|field socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SubVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not subtract %lf from %s in "
		    "volume\n", argv0, argv1, a, abbrv);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_SubFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(err, "%s %s: could not subtract %s from %s in volume\n",
		argv0, argv1, a_s, abbrv);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Multiply a field by a scalar or another field
 */

static int mul_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s type value|field socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_MulVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not multiply %s by %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_MulFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(err, "%s %s: could not multiply %s by %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Divide a field by a scalar or another field
 */

static int div_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s data_type value|field socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_DivVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not divide %s by %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_DivFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(err, "%s %s: could not divide %s by %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

/*
   Replace a field with it's log10.
 */

static int log10_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s data_type socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( !DataType_Get(abbrv) ) {
	fprintf(err, "%s %s: no data type named %s\n", argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( (status = Sigmet_Vol_Fld_Log10(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(err, "%s %s: could not compute log10 of %s in volume\n",
		argv0, argv1, abbrv);
	return status;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

static int incr_time_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *dt_s;
    double dt;				/* Time increment, seconds */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s dt socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    dt_s = argv[2];
    if ( sscanf(dt_s, "%lf", &dt) != 1) {
	fprintf(err, "%s %s: expected float value for time increment, got "
		"%s\n", argv0, argv1, dt_s);
	return SIGMET_BAD_ARG;
    }
    if ( !Sigmet_Vol_IncrTm(vol_p, dt / 86400.0) ) {
	fprintf(err, "%s %s: could not increment time in volume\n",
		argv0, argv1);
	return SIGMET_BAD_TIME;
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

static int data_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s, y, r, b;
    char *abbrv;
    double d;
    struct Sigmet_DatArr *dat_p;
    int all = -1;

    /*
       Identify input and desired output
       Possible forms:
       sigmet_ray data			(argc = 3)
       sigmet_ray data data_type		(argc = 4)
       sigmet_ray data data_type s		(argc = 5)
       sigmet_ray data data_type s r	(argc = 6)
       sigmet_ray data data_type s r b	(argc = 7)
     */

    abbrv = NULL;
    y = s = r = b = all;
    if ( argc >= 3 ) {
	abbrv = argv[2];
    }
    if ( argc >= 4 && sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 5 && sscanf(argv[4], "%d", &r) != 1 ) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 6 && sscanf(argv[5], "%d", &b) != 1 ) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 7 ) {
	fprintf(err, "Usage: %s %s [[[[data_type] sweep] ray] bin] socket\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }

    /*
       Validate.
     */

    if ( abbrv ) {
	if ( (dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	    y = dat_p - vol_p->dat;
	} else {
	    fprintf(err, "%s %s: no data type named %s\n",
		    argv0, argv1, abbrv);
	    return SIGMET_BAD_ARG;
	}
    }
    if ( s != all && s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r != all && r >= (int)vol_p->ih.ic.num_rays ) {
	fprintf(err, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b != all && b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(err, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < vol_p->num_types; y++) {
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		abbrv = vol_p->dat[y].data_type->abbrv;
		fprintf(out, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		    if ( !vol_p->ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(out, "ray %d: ", r);
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
			d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
			if ( Sigmet_IsData(d) ) {
			    fprintf(out, "%f ", d);
			} else {
			    fprintf(out, "nodat ");
			}
		    }
		    fprintf(out, "\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    fprintf(out, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( !vol_p->ray_ok[s][r] ) {
		    continue;
		}
		fprintf(out, "ray %d: ", r);
		for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		    if ( Sigmet_IsData(d) ) {
			fprintf(out, "%f ", d);
		    } else {
			fprintf(out, "nodat ");
		    }
		}
		fprintf(out, "\n");
	    }
	}
    } else if ( r == all && b == all ) {
	fprintf(out, "%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	    if ( !vol_p->ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(out, "ray %d: ", r);
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else if ( b == all ) {
	if ( vol_p->ray_ok[s][r] ) {
	    fprintf(out, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    fprintf(out, "%f ", d);
		} else {
		    fprintf(out, "nodat ");
		}
	    }
	    fprintf(out, "\n");
	}
    } else {
	if ( vol_p->ray_ok[s][r] ) {
	    fprintf(out, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
	    if ( Sigmet_IsData(d) ) {
		fprintf(out, "%f ", d);
	    } else {
		fprintf(out, "nodat ");
	    }
	    fprintf(out, "\n");
	}
    }
    return SIGMET_OK;
}

/*
   Print sweep data as a binary stream.
   sigmet_ray bdata data_type s
   Each output ray will have num_output_bins floats.
   Missing values will be Sigmet_NoData().
 */

static int bdata_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_DatArr *dat_p;
    int s, y, r, b;
    char *abbrv;
    static float *ray_p;	/* Buffer to receive ray data */
    int num_bins_out;
    int status, n;

    if ( argc != 4 ) {
	fprintf(err, "Usage: %s %s data_type sweep_index socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    if ( (dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	y = dat_p - vol_p->dat;
    } else {
	fprintf(err, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    n = num_bins_out = vol_p->ih.tc.tri.num_bins_out;
    if ( !ray_p && !(ray_p = CALLOC(num_bins_out, sizeof(float))) ) {
	fprintf(err, "Could not allocate output buffer for ray.\n");
	return SIGMET_ALLOC_FAIL;
    }
    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	for (b = 0; b < num_bins_out; b++) {
	    ray_p[b] = Sigmet_NoData();
	}
	if ( vol_p->ray_ok[s][r] ) {
	    status = Sigmet_Vol_GetRayDat(vol_p, y, s, r, &ray_p, &n);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "Could not get ray data for data type %s, "
			"sweep index %d, ray %d.\n", abbrv, s, r);
		return status;
	    }
	    if ( n > num_bins_out ) {
		fprintf(err, "Ray %d or sweep %d, data type %s has "
			"unexpected number of bins - %d instead of %d.\n",
			r, s, abbrv, n, num_bins_out);
		return SIGMET_BAD_VOL;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins_out, out)
		!= num_bins_out ) {
	    fprintf(err, "Could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n",
		    abbrv, s, r, strerror(errno));
	    return SIGMET_IO_FAIL;
	}
    }
    return SIGMET_OK;
}

static int bin_outline_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status;				/* Result of a function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];

    if ( argc != 5 ) {
	fprintf(err, "Usage: %s %s sweep ray bin socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];

    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(r_s, "%d", &r) != 1 ) {
	fprintf(err, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(b_s, "%d", &b) != 1 ) {
	fprintf(err, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r >= vol_p->ih.ic.num_rays ) {
	fprintf(err, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(err, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }
    if ( (status = Sigmet_Vol_BinOutl(vol_p, s, r, b, corners)) != SIGMET_OK ) {
	fprintf(err, "%s %s: could not compute bin outlines for bin "
		"%d %d %d in volume\n", argv0, argv1, s, r, b);
	return status;
    }
    fprintf(out, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * DEG_RAD, corners[1] * DEG_RAD,
	    corners[2] * DEG_RAD, corners[3] * DEG_RAD,
	    corners[4] * DEG_RAD, corners[5] * DEG_RAD,
	    corners[6] * DEG_RAD, corners[7] * DEG_RAD);

    return SIGMET_OK;
}

static int radar_lon_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *lon_s;			/* New longitude, degrees, in argv */
    double lon;				/* New longitude, degrees */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s new_lon socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lon_s = argv[2];
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for "
		"new longitude, got %s\n", argv0, argv1, lon_s);
	return SIGMET_BAD_ARG;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.longitude = Sigmet_RadBin4(lon);
    vol_p->mod = 1;

    return SIGMET_OK;
}

static int radar_lat_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *lat_s;			/* New latitude, degrees, in argv */
    double lat;				/* New latitude, degrees */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s new_lat socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lat_s = argv[2];
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(err, "%s %s: expected floating point value for "
		"new latitude, got %s\n", argv0, argv1, lat_s);
	return SIGMET_BAD_ARG;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.latitude = Sigmet_RadBin4(lat);
    vol_p->mod = 1;

    return SIGMET_OK;
}

static int shift_az_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *daz_s;			/* Degrees to add to each azimuth */
    double daz;				/* Radians to add to each azimuth */
    unsigned long idaz;			/* Binary angle to add to each
					   azimuth */
    int s, r;				/* Loop indeces */

    if ( argc != 3 ) {
	fprintf(err, "Usage: %s %s dz socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    daz_s = argv[2];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(err, "%s %s: expected float value for azimuth shift, "
		"got %s\n", argv0, argv1, daz_s);
	return SIGMET_BAD_ARG;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    idaz = Sigmet_RadBin4(daz);
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.rhi_info.az[s] += idaz;
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.ppi_info.left_az += idaz;
		vol_p->ih.tc.tni.scan_info.ppi_info.right_az += idaz;
	    }
	    break;
	case FILE_SCAN:
	    vol_p->ih.tc.tni.scan_info.file_info.az0 += idaz;
	case MAN_SCAN:
	    break;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
	    vol_p->ray_az0[s][r]
		= GeogLonR(vol_p->ray_az0[s][r] + daz, 180.0 * RAD_PER_DEG);
	    vol_p->ray_az1[s][r]
		= GeogLonR(vol_p->ray_az1[s][r] + daz, 180.0 * RAD_PER_DEG);
	}
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

static int outlines_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int status = SIGMET_OK;		/* Return value of this function */
    int bnr;				/* If true, send raw binary output */
    char *s_s;				/* Sweep index, as a string */
    char *abbrv;			/* Data type abbreviation */
    char *min_s, *max_s;		/* Bounds of data interval of interest
					 */
    char *outFlNm;			/* Name of output file */
    FILE *outlnFl;			/* Output file */
    struct DataType *data_type;		/* Information about the data type */
    int s;				/* Sweep index */
    double min, max;			/* Bounds of data interval of interest
					 */
    double re;				/* Earth radius */

    if ( argc == 7 ) {
	bnr = 0;
	abbrv = argv[2];
	s_s = argv[3];
	min_s = argv[4];
	max_s = argv[5];
	outFlNm = argv[6];
    } else if ( argc == 8 && strcmp(argv[2], "-b") == 0 ) {
	bnr = 1;
	abbrv = argv[3];
	s_s = argv[4];
	min_s = argv[5];
	max_s = argv[6];
	outFlNm = argv[7];
    } else {
	fprintf(err, "Usage: %s %s [-b] data_type sweep min max "
		"out_file socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( !(data_type = DataType_Get(abbrv)) ) {
	fprintf(err, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(err, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(min_s, "-inf") == 0 || strcmp(min_s, "-INF") == 0 ) {
	min = -DBL_MAX;
    } else if ( sscanf(min_s, "%lf", &min) != 1 ) {
	fprintf(err, "%s %s: expected float value or -INF for data min,"
		" got %s\n", argv0, argv1, min_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(max_s, "inf") == 0 || strcmp(max_s, "INF") == 0 ) {
	max = DBL_MAX;
    } else if ( sscanf(max_s, "%lf", &max) != 1 ) {
	fprintf(err, "%s %s: expected float value or INF for data max,"
		" got %s\n", argv0, argv1, max_s);
	return SIGMET_BAD_ARG;
    }
    if ( !(min < max)) {
	fprintf(err, "%s %s: minimum (%s) must be less than maximum (%s)\n",
		argv0, argv1, min_s, max_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(outFlNm, "-") == 0 ) {
	outlnFl = out;
    } else if ( !(outlnFl = fopen(outFlNm, "w")) ) {
	fprintf(err, "%s %s: could not open %s for output.\n%s\n",
		argv0, argv1, outFlNm, strerror(errno));
    }
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    re = GeogREarth(NULL) * 4 / 3;
	    status = Sigmet_Vol_RHI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n", argv0, argv1, abbrv, s);
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    status = Sigmet_Vol_PPI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(err, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n", argv0, argv1, abbrv, s);
	    }
	    break;
	case FILE_SCAN:
	case MAN_SCAN:
	    Err_Append("Can only print outlines for RHI and PPI. ");
	    status = SIGMET_BAD_ARG;
	    break;
    }
    if ( outlnFl != out ) {
	fclose(outlnFl);
    }
    return status;
}

static int dorade_cb(int argc, char *argv[], struct Sigmet_Vol *vol_p,
	FILE *out, FILE *err)
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;				/* Index of desired sweep,
					   or -1 for all */
    char *s_s;				/* String representation of s */
    int all = -1;
    int status;				/* Result of a function */
    struct Dorade_Sweep swp;

    if ( argc == 2 ) {
	s = all;
    } else if ( argc == 3 ) {
	s_s = argv[2];
	if ( strcmp(s_s, "all") == 0 ) {
	    s = all;
	} else if ( sscanf(s_s, "%d", &s) != 1 ) {
	    fprintf(err, "%s %s: expected integer for sweep index, got \"%s"
		    "\"\n", argv0, argv1, s_s);
	    return SIGMET_BAD_ARG;
	}
    } else {
	fprintf(err, "Usage: %s %s [s] socket\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(err, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( s == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
		fprintf(err, "%s %s: could not translate sweep %d of volume "
			"to DORADE format\n", argv0, argv1, s);
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(err, "%s %s: could not write DORADE file for sweep "
			"%d of volume\n", argv0, argv1, s);
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
	    fprintf(err, "%s %s: could not translate sweep %d of volume to "
		    "DORADE format\n", argv0, argv1, s);
	    goto error;
	}
	if ( !Dorade_Sweep_Write(&swp) ) {
	    fprintf(err, "%s %s: could not write DORADE file for sweep "
		    "%d of volume\n", argv0, argv1, s);
	    goto error;
	}
	Dorade_Sweep_Free(&swp);
    }

    return SIGMET_OK;

error:
    Dorade_Sweep_Free(&swp);
    return status;
}
