/*
   -	sigmet_dorade.c --
   -		Translate Sigmet data into DORADE.
   -		See sigmet (3).
   -
   .	Copyright (c) 2010 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.30 $ $Date: 2011/01/08 01:14:54 $
 */

#include <string.h>
#include <math.h>
#include "sigmet.h"
#include "alloc.h"
#include "err_msg.h"
#include "strlcpy.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "data_types.h"
#include "dorade_lib.h"

/*
   Round x to nearest integer
 */

static int n_int(double);
static int n_int(double x)
{
    return (int)floor(x + 0.5);
}

int Sigmet_Vol_ToDorade(struct Sigmet_Vol *vol_p, int s,
	struct Dorade_Sweep *swp_p)
{
    int status;					/* Return value for this call */
    double epoch;				/* 1970/01/01 */
    int year, mon, day, hr, min; double sec;	/* Sweep time */
    double wave_len;				/* Wavelength from vol_p */
    double prf;					/* PRF from vol_p */
    int p, r, c;				/* Loop parameters */
    float *ray_p = NULL;			/* Hold data for a ray */
    float *r_p;					/* Point into ray_p */
    int num_bins = 0;				/* Allocation at ray_p */
    int num_parms, num_rays, num_cells;		/* Convenience */

    /*
       This array specifies soloii equivalents for certain Sigmet data types.
       Index soloii_abbrv with Sigmet_DataTypeN enumerator to determine
       equivalent abbreviation, e.g. soloii_abbrv[DB_DBT] is "ZT" => use "ZT"
       in sweep files instead of "DB_DBT".
     */

    static char *soloii_abbrv[SIGMET_NTYPES] = {NULL, "ZT", "DZ", "VR", "SW", NULL};

    /*
       Convenience variables point into swp_p
     */

    struct Dorade_SSWB *sswb_p;
    struct Dorade_Sensor *sensor_p;
    struct Dorade_RADD *radd_p;
    struct Dorade_PARM *parm_p;
    struct Dorade_CSFD *csfd_p;
    struct Dorade_SWIB *swib_p;
    struct Dorade_Ray_Hdr *ray_hdr_p;
    struct Dorade_RYIB *ryib_p;
    struct Dorade_ASIB *asib_p;
    float ***dat_p;

    num_parms = num_rays = num_cells = -1;

    if ( s > vol_p->ih.ic.num_sweeps ) {
	Err_Append("Sweep index out of range. ");
	status = SIGMET_RNG_ERR;
	goto error;
    }

    /*
       Populate comm block
     */

    if ( snprintf(swp_p->comm.comment, 500, "Sigmet volume sweep %d, task %s", 
		s, vol_p->ph.pc.task_name) >= 500 ) {
	Err_Append("Could not set COMM block. String too big.");
	status = SIGMET_RNG_ERR;
	goto error;
    }

    /*
       Populate sswb block
     */

    sswb_p = &swp_p->sswb;
    epoch = Tm_CalToJul(1970, 1, 1, 0, 0, 0.0);
    sswb_p->i_start_time = n_int((vol_p->sweep_time[s] - epoch) * 86400);
    sswb_p->compression_flag = 0;
    num_parms = sswb_p->num_parms = vol_p->num_types;
    strlcpy(sswb_p->radar_name, vol_p->ih.ic.su_site_name, 9);

    /*
       Populate vold block
     */

    swp_p->vold.volume_num = 1;
    swp_p->vold.maximum_bytes = 65500;
    if ( !Tm_JulToCal(vol_p->sweep_time[s], &year, &mon, &day, &hr, &min, &sec) ) {
	Err_Append("Could not set sweep time. ");
	status = SIGMET_BAD_TIME;
	goto error;
    }
    swp_p->vold.year = year;
    swp_p->vold.month = mon;
    swp_p->vold.day = day;
    swp_p->vold.data_set_hour = hr;
    swp_p->vold.data_set_minute = min;
    swp_p->vold.data_set_second = n_int(sec);
    strncpy(swp_p->vold.gen_facility, vol_p->ih.ic.su_site_name, 8);
    swp_p->vold.num_sensors = 1;

    /*
       Populate sensor block: radd parm1 parm2 ... parmN celvORcsfd cfac
     */

    sensor_p = &swp_p->sensor;

    /*
       Populate radd block
     */

    radd_p = &sensor_p->radd;
    strlcpy(radd_p->radar_name, vol_p->ih.ic.su_site_name, 9);
    radd_p->radar_const
	= 0.01 * vol_p->ih.tc.tci.hpol_radar_const;	/* Ignore vpol */
    radd_p->peak_power = 0.001 * vol_p->ih.tc.tmi.power;
    radd_p->noise_power
	= 0.01 * vol_p->ih.tc.tci.hpol_noise;		/* Ignore vpol */
    radd_p->horz_beam_width
	= 360.0 / pow(2, 32) * vol_p->ih.tc.tmi.horiz_beam_width;
    radd_p->vert_beam_width
	= 360.0 / pow(2, 32) * vol_p->ih.tc.tmi.vert_beam_width;
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	case PPI_C:
	    radd_p->scan_mode = 1;			/* ppi */
	    break;
	case RHI:
	    radd_p->scan_mode = 3;			/* rhi */
	    break;
	case MAN_SCAN:
	    radd_p->scan_mode = 6;			/* manual */
	    break;
	case FILE_SCAN:
	    radd_p->scan_mode = DORADE_BAD_I;
	    break;
    }
    radd_p->num_parms = radd_p->total_num_des = num_parms;
    radd_p->data_compress = 0;
    radd_p->radar_longitude
	= GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD;
    radd_p->radar_latitude = Sigmet_Bin4Rad(vol_p->ih.ic.latitude) * DEG_PER_RAD;
    radd_p->radar_altitude
	= 0.001 * (vol_p->ih.ic.ground_elev + vol_p->ih.ic.radar_ht);
    wave_len = 0.0001 * vol_p->ih.tc.tmi.wave_len;	/* Convert 1/100 cm to m */
    prf = vol_p->ih.tc.tdi.prf;				/* Hertz */
    switch (vol_p->ih.tc.tdi.m_prf_mode) {
	case ONE_ONE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * prf;
	    break;
	case TWO_THREE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * 1.5 * prf * 2;
	    break;
	case THREE_FOUR:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * 4.0 / 3.0 * prf * 3;
	    break;
	case FOUR_FIVE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * (1.25 * prf) * 4;
	    break;
    }
    radd_p->eff_unamb_range = 0.5 * 2.9979e5 / prf;	/* km */
    radd_p->num_freq_trans = 1;
    radd_p->num_ipps_trans = 1;
    radd_p->freq1 = 1.0e-9 * 2.9979e8 / wave_len;	/* GHz */
    radd_p->interpulse_per1 = 1000.0 * 1.0 / prf;	/* millisec */
    strncpy(radd_p->config_name, vol_p->ph.pc.task_name, 8);
    radd_p->pulse_width = 0.01 * vol_p->ih.tc.tdi.pulse_w;
    strncpy(radd_p->site_name, vol_p->ih.ic.su_site_name, 20);

    /*
       Populate parm blocks
     */

    if ( !(sensor_p->parm = CALLOC(num_parms, sizeof(struct Dorade_PARM))) ) {
	Err_Append("Could not allocate array of parameter descriptors. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    parm_p = NULL;
    for (p = 0; p < num_parms; p++) {
	char *abbrv;				/* Data type abbreviation,
						   e.g. "DZ" or "DB_ZDR" */
	enum Sigmet_DataTypeN sig_type;
	struct DataType *data_type;

	abbrv = vol_p->dat[p].data_type->abbrv;
	if ( !abbrv ) {
	    continue;
	}
	data_type = vol_p->dat[p].data_type;
	parm_p = sensor_p->parm + p;
	Dorade_PARM_Init(parm_p);
	if ( Sigmet_DataType_GetN(abbrv, &sig_type) && soloii_abbrv[sig_type] )
	{
	    strncpy(parm_p->parameter_name, soloii_abbrv[sig_type], 8);
	} else {
	    strncpy(parm_p->parameter_name, abbrv, 8);
	}
	strncpy(parm_p->param_description, data_type->descr, 40);
	strncpy(parm_p->param_units, data_type->unit, 40);
	parm_p->xmitted_freq = n_int(1.0e-9 * 2.9979e8 / wave_len);
	parm_p->recvr_bandwidth = 1.0e-3 * vol_p->ih.tc.tci.bandwidth;
	parm_p->pulse_width
	    = n_int(vol_p->ih.tc.tdi.pulse_w * 0.01 * 1.0e-6 * 2.9979e8);
	parm_p->num_samples = vol_p->ih.tc.tdi.sampl_sz;
	parm_p->binary_format = DD_16_BITS;	/* To keep significant bits */
	strncpy(parm_p->threshold_field, "NONE", 8);
	parm_p->parameter_scale = 100.0;	/* From sample files */
	parm_p->parameter_bias = 0.0;		/* From sample files */
	parm_p->bad_data = DORADE_BAD_I;
	strncpy(parm_p->config_name, vol_p->ph.pc.task_name, 8);
	parm_p->offset_to_data = 0;
	num_cells = parm_p->num_cells = vol_p->ih.tc.tri.num_bins_out;
	parm_p->meters_to_first_cell = 0.01 * (vol_p->ih.tc.tri.rng_1st_bin
		+ 0.5 * vol_p->ih.tc.tri.step_out);
	parm_p->meters_between_cells = 0.01 * vol_p->ih.tc.tri.step_out;
	parm_p->eff_unamb_vel = radd_p->eff_unamb_vel;
    }

    /*
       Populate csfd block. Assume cell geometry for last parameter applies
       to all (parm_p). Initialize csfd member since dorade_lib assumes CFAC.
     */

    if ( !parm_p ) {
	Err_Append("Volume has no parameters. ");
	status = SIGMET_BAD_VOL;
	goto error;
    }
    sensor_p->cell_geo_t = CSFD;
    csfd_p = &sensor_p->cell_geo.csfd;
    Dorade_CSFD_Init(csfd_p);
    csfd_p->num_segments = 1;
    csfd_p->dist_to_first = parm_p->meters_to_first_cell;
    csfd_p->spacing[0] = parm_p->meters_between_cells;
    csfd_p->num_cells[0] = num_cells;

    /*
       Sigmet volume does not have "correction factors", so skip CFAC
     */

    /*
       Populate struct Dorade_SWIB swib block
     */

    swib_p = &swp_p->swib;
    strncpy(swib_p->radar_name, sswb_p->radar_name, 9);
    swib_p->sweep_num = 1;
    num_rays = swib_p->num_rays = vol_p->ih.ic.num_rays;
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	case PPI_C:
	    swib_p->start_angle = vol_p->ray_az0[s][0];
	    swib_p->stop_angle = vol_p->ray_az1[s][num_rays - 1];
	    break;
	case RHI:
	    swib_p->start_angle = vol_p->ray_tilt0[s][0];
	    swib_p->stop_angle = vol_p->ray_tilt1[s][num_rays - 1];
	    break;
	case FILE_SCAN:
	case MAN_SCAN:
	    swib_p->start_angle = DORADE_BAD_F;
	    swib_p->stop_angle = DORADE_BAD_F;
	    break;
    };
    swib_p->fixed_angle = vol_p->sweep_angle[s] * DEG_PER_RAD;

    /*
       Populate struct Dorade_Ray_Hdr *ray_hdr array
     */

    if ( !(swp_p->ray_hdr = CALLOC(num_rays, sizeof(struct Dorade_Ray_Hdr))) ) {
	Err_Append("Could not allocate space for ray headers. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    for (r = 0; r < num_rays; r++) {
	ray_hdr_p = swp_p->ray_hdr + r;
	ryib_p = &ray_hdr_p->ryib;
	asib_p = &ray_hdr_p->asib;
	Dorade_RYIB_Init(ryib_p);
	ryib_p->sweep_num = s;
	ryib_p->ray_status = 2;
	Dorade_ASIB_Init(asib_p);
    }
    for (r = 0; r < num_rays; r++) {
	double julian0;			/* 00:00 01 Jan of ray year */
	double az0, az1;		/* Ray start and end azimuth */

	if ( !vol_p->ray_ok ) {
	    continue;
	}

	ray_hdr_p = swp_p->ray_hdr + r;
	ryib_p = &ray_hdr_p->ryib;
	asib_p = &ray_hdr_p->asib;

	/*
	   Populate ryib block
	 */

	ryib_p->sweep_num = s;
	if ( !Tm_JulToCal(vol_p->ray_time[s][r],
		    &year, &mon, &day, &hr, &min, &sec) ) {
	    Err_Append("Could not get ray time. ");
	    status = SIGMET_BAD_VOL;
	    goto error;
	}
	julian0 = Tm_CalToJul(year, 1, 1, 0, 0, 0.0);
	ryib_p->julian_day = vol_p->ray_time[s][r] - julian0;
	ryib_p->hour = hr;
	ryib_p->minute = min;
	ryib_p->second = sec;
	ryib_p->millisecond = n_int((sec - ryib_p->second) * 1000);
	az0 = vol_p->ray_az0[s][r];
	az1 = vol_p->ray_az1[s][r];
	ryib_p->azimuth = 0.5 * (az0 + GeogLonR(az1, az0));
	ryib_p->azimuth = DEG_PER_RAD * GeogLonR(ryib_p->azimuth, 0.0);
	ryib_p->elevation
	    = DEG_PER_RAD * 0.5 * (vol_p->ray_tilt0[s][r] + vol_p->ray_tilt1[s][r]);
	ryib_p->peak_power = 0.001 * vol_p->ih.tc.tmi.power;
	ryib_p->ray_status = vol_p->ray_ok[s][r] ? 0 : 2;

	/*
	   Populate asib block. Assume stationary ground radar
	 */

	asib_p->longitude = radd_p->radar_longitude;
	asib_p->latitude = radd_p->radar_latitude;
	asib_p->altitude_msl = radd_p->radar_altitude;
	asib_p->altitude_agl = 0.001 * vol_p->ih.ic.radar_ht;
    }

    /*
       Populate int dat array.
     */

    if ( !Dorade_Sweep_AllocDat(swp_p, num_parms, num_rays, num_cells) ) {
	Err_Append("Could not allocate data array. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    dat_p = swp_p->dat;
    num_bins = num_cells;
    ray_p = NULL;
    if ( !(ray_p = MALLOC(num_bins * sizeof(float)))) {
	Err_Append("Could not allocate ray data buffer. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    for (r_p = ray_p ; r_p < ray_p + num_bins; r_p++) {
	*r_p = Sigmet_NoData();
    }
    for (p = 0; p < num_parms; p++) {
	int bad_data = swp_p->sensor.parm[p].bad_data;

	if ( !vol_p->dat[p].data_type ) {
	    continue;
	}
	for (r = 0; r < num_rays; r++) {
	    if ( !vol_p->ray_ok[s][r] ) {
		for (c = 0; c < num_cells; c++) {
		    dat_p[p][r][c] = bad_data;
		}
	    } else {
		status = Sigmet_Vol_GetRayDat(vol_p, p, s, r, &ray_p, &num_bins);
		if ( (status != SIGMET_OK) ) {
		    Err_Append("Could not retrieve ray data "
			    "from Sigmet volume");
		    goto error;
		}
		for (c = 0; c < num_cells; c++) {
		    if (Sigmet_IsData(ray_p[c])) {
			dat_p[p][r][c] = ray_p[c];
		    } else {
			dat_p[p][r][c] = bad_data;
		    }
		}
	    }
	}
    }
    FREE(ray_p);

    return SIGMET_OK;

error:
    Dorade_Sweep_Free(swp_p);
    FREE(ray_p);
    return status;
};
