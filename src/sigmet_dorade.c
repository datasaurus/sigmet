/*
   -	sigmet_dorade.c --
   -		Translate Sigmet data into DORADE.
   -		See sigmet (3).
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
   .	$Revision: 1.65 $ $Date: 2013/06/27 21:36:49 $
 */

#include <string.h>
#include <math.h>
#include "sigmet.h"
#include "alloc.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "dorade_lib.h"

enum SigmetStatus Sigmet_Vol_ToDorade(struct Sigmet_Vol *vol_p, int s,
	struct Dorade_Sweep *swp_p)
{
    int status;					/* Return value for this call */
    double epoch;				/* 1970/01/01 */
    int year, mon, day, hr, min;
    double sec;					/* Sweep time */
    double wave_len;				/* Wavelength from vol_p */
    double prf;					/* PRF from vol_p */
    int p, r, c;				/* Loop parameters */
    float *dist_cells = NULL;			/* Part of celv */
    float *ray_p = NULL;			/* Hold data for a ray */
    float *r_p;					/* Point into ray_p */
    int num_bins = 0;				/* Allocation at ray_p */
    int num_parms, num_rays, num_cells;		/* Convenience */
    int num_rays_d;				/* Number of rays in DORADE
						   sweep.  Will be <= number of
						   rays in Sigmet sweep. */
    char *data_type_s;				/* Data type abbreviation,
						   e.g. "DZ" or "DB_ZDR" */
    enum Sigmet_DataTypeN sig_type;
    int p_d;					/* Parameter index in DORADE
						   sweep */


    /*
       This array specifies soloii equivalents for certain Sigmet data types.
       Index soloii_abbrv with Sigmet_DataTypeN enumerator to determine
       equivalent abbreviation, e.g. soloii_abbrv[DB_DBT] is "ZT" => use "ZT"
       in sweep files instead of "DB_DBT".
     */

    static char *soloii_abbrv[SIGMET_NTYPES] = {
	NULL, "ZT", "DZ", "VR", "SW", NULL
    };

    /*
       Convenience variables point into swp_p
     */

    struct Dorade_SSWB *sswb_p;
    struct Dorade_Sensor *sensor_p;
    struct Dorade_RADD *radd_p;
    struct Dorade_PARM *parm_p;
    struct Dorade_CELV *celv_p;
    struct Dorade_CFAC *cfac_p;
    struct Dorade_SWIB *swib_p;
    struct Dorade_Ray_Hdr *ray_hdr_p;
    struct Dorade_RYIB *ryib_p;
    struct Dorade_ASIB *asib_p;
    float **dat_p;
    double start_time, stop_time;
    size_t comm_sz;

    /*
       Last parameter from volume. This becomes the "next" member of the next
       parameter read in, created a linked list recording the order in which
       parameters were read from the sweep file.
     */

    struct Dorade_PARM *prev_parm;

    num_parms = num_rays = num_cells = -1;

    if ( s > vol_p->ih.ic.num_sweeps ) {
	fprintf(stderr, "Sweep index out of range.\n");
	status = SIGMET_RNG_ERR;
	goto error;
    }
    if ( !vol_p->sweep_hdr[s].ok ) {
	fprintf(stderr, "Sweep %d is bad.\n", s);
	status = SIGMET_BAD_VOL;
	goto error;
    }

    /*
       Populate comm block
     */

    comm_sz = sizeof(swp_p->comm.comment);
    if ( snprintf(swp_p->comm.comment, comm_sz - 1,
		"Sigmet volume sweep %d, task %s", s, vol_p->ph.pc.task_name)
	    >= comm_sz - 1 ) {
	fprintf(stderr, "Could not set COMM block. String too big.\n");
	status = SIGMET_RNG_ERR;
	goto error;
    }

    /*
       Populate sswb block
     */

    num_rays = vol_p->ih.ic.num_rays;
    sswb_p = &swp_p->sswb;
    sswb_p->compression_flag = 0;
    num_parms = sswb_p->num_parms = vol_p->num_types;
    snprintf(sswb_p->radar_name, 9, "%s", vol_p->ih.ic.su_site_name);
    epoch = Tm_CalToJul(1970, 1, 1, 0, 0, 0.0);
    start_time = (vol_p->ray_hdr[s][0].time - epoch) * 86400;
    stop_time = (vol_p->ray_hdr[s][num_rays - 1].time - epoch) * 86400;
    if ( stop_time > start_time ) {
	sswb_p->i_start_time = round(start_time);
	sswb_p->i_stop_time = round(stop_time);
    } else {
	sswb_p->i_start_time = round(stop_time);
	sswb_p->i_stop_time = round(start_time);
    }

    /*
       Populate vold block
     */

    swp_p->vold.volume_num = 1;
    swp_p->vold.maximum_bytes = 65500;
    if ( !Tm_JulToCal(vol_p->ray_hdr[s][0].time,
		&year, &mon, &day, &hr, &min, &sec) ) {
	fprintf(stderr, "Could not set sweep time.\n");
	status = SIGMET_BAD_TIME;
	goto error;
    }
    swp_p->vold.year = year;
    swp_p->vold.month = mon;
    swp_p->vold.day = day;
    swp_p->vold.data_set_hour = hr;
    swp_p->vold.data_set_minute = min;
    swp_p->vold.data_set_second = round(sec);
    strncpy(swp_p->vold.gen_facility, vol_p->ih.ic.su_site_name, 8);
    swp_p->vold.gen_year = vol_p->ph.pc.ingest_sweep_tm.year;
    swp_p->vold.gen_month = vol_p->ph.pc.ingest_sweep_tm.month;
    swp_p->vold.gen_day = vol_p->ph.pc.ingest_sweep_tm.day;
    swp_p->vold.num_sensors = 1;

    /*
       Populate sensor block: radd parm1 parm2 ... parmN celvORcsfd cfac
     */

    sensor_p = &swp_p->sensor;

    /*
       Populate radd block
     */

    radd_p = &sensor_p->radd;
    snprintf(radd_p->radar_name, 9, "%s", vol_p->ih.ic.su_site_name);
    epoch = Tm_CalToJul(1970, 1, 1, 0, 0, 0);
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
	    radd_p->scan_mode = DORADE_BAD_I4;
	    break;
    }
    radd_p->num_parms = radd_p->total_num_des = num_parms;
    radd_p->data_compress = 0;
    radd_p->radar_longitude
	= GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD;
    radd_p->radar_latitude
	= Sigmet_Bin4Rad(vol_p->ih.ic.latitude) * DEG_PER_RAD;
    radd_p->radar_altitude
	= 0.001 * (vol_p->ih.ic.ground_elev + vol_p->ih.ic.radar_ht);
    wave_len = 0.0001 * vol_p->ih.tc.tmi.wave_len; /* Convert 1/100 cm to m */
    prf = vol_p->ih.tc.tdi.prf;			   /* Hertz */
    switch (vol_p->ih.tc.tdi.m_prf_mode) {
	case ONE_ONE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * prf;
	    break;
	case TWO_THREE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * prf * 2;
	    break;
	case THREE_FOUR:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * prf * 3;
	    break;
	case FOUR_FIVE:
	    radd_p->eff_unamb_vel = 0.25 * wave_len * prf * 4;
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

    parm_p = prev_parm = NULL;
    for (p = 0; p < num_parms; p++) {
	data_type_s = vol_p->dat[p].data_type_s;
	if ( Sigmet_DataType_GetN(data_type_s, &sig_type)
		&& soloii_abbrv[sig_type] ) {
	    data_type_s = soloii_abbrv[sig_type];
	}
	if ( strlen(data_type_s) == 0 ) {
	    continue;
	}
	p_d = Dorade_Parm_NewIdx(swp_p, data_type_s);
	if ( p_d == -1 ) {
	    fprintf(stderr, "Could not find place for new parameter %s.\n",
		    vol_p->dat[p].data_type_s);
	    status = SIGMET_BAD_VOL;
	    goto error;
	}
	parm_p = sensor_p->parms + p_d;
	if ( !sensor_p->parm0 ) {
	    sensor_p->parm0 = parm_p;
	} else {
	    prev_parm->next = parm_p;
	}
	prev_parm = parm_p;
	Dorade_PARM_Init(parm_p);
	strncpy(parm_p->parm_nm, data_type_s, 8);
	strncpy(parm_p->parm_description, vol_p->dat[p].descr, 40);
	strncpy(parm_p->parm_units, vol_p->dat[p].unit, 8);
	parm_p->xmitted_freq = round(1.0e-9 * 2.9979e8 / wave_len);
	parm_p->recvr_bandwidth = 1.0e-3 * vol_p->ih.tc.tci.bandwidth;
	parm_p->pulse_width
	    = round(vol_p->ih.tc.tdi.pulse_w * 0.01 * 1.0e-6 * 2.9979e8);
	parm_p->num_samples = vol_p->ih.tc.tdi.sampl_sz;
	parm_p->binary_format = DD_16_BITS;	/* To keep significant bits */
	strncpy(parm_p->threshold_field, "NONE", 8);
	parm_p->parameter_scale = 100.0;	/* From sample files */
	parm_p->parameter_bias = 0.0;		/* From sample files */
	parm_p->bad_data = DORADE_BAD_I2;
	strncpy(parm_p->config_name, vol_p->ph.pc.task_name, 8);
	parm_p->offset_to_data = 0;
	num_cells = parm_p->num_cells = vol_p->ih.tc.tri.num_bins_out;
	parm_p->meters_to_first_cell = 0.01 * (vol_p->ih.tc.tri.rng_1st_bin
		+ 0.5 * vol_p->ih.tc.tri.step_out);
	parm_p->meters_between_cells = 0.01 * vol_p->ih.tc.tri.step_out;
	parm_p->eff_unamb_vel = radd_p->eff_unamb_vel;
    }
    if ( !parm_p ) {
	fprintf(stderr, "Volume has no parameters.\n");
	status = SIGMET_BAD_VOL;
	goto error;
    }

    /*
       Populate CELV block. Assume cell geometry for last parameter applies
       to all (parm_p).
     */

    sensor_p->cell_geo_t = CG_CELV;
    celv_p = &sensor_p->cell_geo.celv;
    Dorade_CELV_Init(celv_p);
    if ( !(dist_cells = CALLOC(num_cells, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate memory for cell vector.\n");
	status = SIGMET_MEM_FAIL;
	goto error;
    }
    for (c = 0 ; c < num_cells; c++) {
	float meters_to_first_cell = parm_p->meters_to_first_cell;
	float meters_between_cells = parm_p->meters_between_cells;

	dist_cells[c] = meters_to_first_cell + c * meters_between_cells;
    }
    celv_p->num_cells = num_cells;
    celv_p->dist_cells = dist_cells;

    /*
       Sigmet volume does not have "correction factors".
     */
    
    cfac_p = &sensor_p->cfac;
    cfac_p->azimuth_corr = 0.0;
    cfac_p->elevation_corr = 0.0;
    cfac_p->range_delay_corr = 0.0;
    cfac_p->longitude_corr = 0.0;
    cfac_p->latitude_corr = 0.0;
    cfac_p->pressure_alt_corr = 0.0;
    cfac_p->radar_alt_corr = 0.0;
    cfac_p->ew_gndspd_corr = 0.0;
    cfac_p->ns_gndspd_corr = 0.0;
    cfac_p->vert_vel_corr = 0.0;
    cfac_p->heading_corr = 0.0;
    cfac_p->roll_corr = 0.0;
    cfac_p->pitch_corr = 0.0;
    cfac_p->drift_corr = 0.0;
    cfac_p->rot_angle_corr = 0.0;
    cfac_p->tilt_corr = 0.0;

    /*
       Populate struct Dorade_SWIB swib block
     */

    swib_p = &swp_p->swib;
    snprintf(swib_p->radar_name, 9, "%s", sswb_p->radar_name);
    swib_p->sweep_num = 1;
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	case PPI_C:
	    swib_p->start_angle = DEG_PER_RAD * vol_p->ray_hdr[s][0].az0;
	    swib_p->stop_angle
		= DEG_PER_RAD * vol_p->ray_hdr[s][num_rays - 1].az1;
	    break;
	case RHI:
	    swib_p->start_angle = DEG_PER_RAD * vol_p->ray_hdr[s][0].tilt0;
	    swib_p->stop_angle
		= DEG_PER_RAD * vol_p->ray_hdr[s][num_rays - 1].tilt1;
	    break;
	case FILE_SCAN:
	case MAN_SCAN:
	    swib_p->start_angle = DORADE_BAD_F;
	    swib_p->stop_angle = DORADE_BAD_F;
	    break;
    };
    swib_p->fixed_angle = vol_p->sweep_hdr[s].angle * DEG_PER_RAD;

    /*
       Populate struct Dorade_Ray_Hdr *ray_hdr array
     */

    if ( !(swp_p->ray_hdr = CALLOC(num_rays, sizeof(struct Dorade_Ray_Hdr))) ) {
	fprintf(stderr, "Could not allocate space for ray headers.\n");
	status = SIGMET_MEM_FAIL;
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
    for (r = 0, ray_hdr_p = swp_p->ray_hdr, num_rays_d = 0;
	    r < num_rays;
	    r++) {

	double julian0;			/* 00:00 01 Jan of ray year */
	double az0, az1;		/* Ray start and end azimuth */

	if ( !vol_p->ray_hdr[s][r].ok ) {
	    continue;
	}
	ray_hdr_p = swp_p->ray_hdr + num_rays_d++;
	ryib_p = &ray_hdr_p->ryib;
	asib_p = &ray_hdr_p->asib;

	/*
	   Populate ryib block
	 */

	ryib_p->sweep_num = s;
	if ( !Tm_JulToCal(vol_p->ray_hdr[s][r].time,
		    &year, &mon, &day, &hr, &min, &sec) ) {
	    fprintf(stderr, "Could not get ray time.\n");
	    status = SIGMET_BAD_VOL;
	    goto error;
	}
	julian0 = Tm_CalToJul(year, 1, 1, 0, 0, 0);
	ryib_p->julian_day = vol_p->ray_hdr[s][r].time - julian0 + 1;
	ryib_p->hour = hr;
	ryib_p->minute = min;
	ryib_p->second = sec;
	ryib_p->millisecond = round((sec - ryib_p->second) * 1000);
	az0 = vol_p->ray_hdr[s][r].az0;
	az1 = vol_p->ray_hdr[s][r].az1;
	ryib_p->azimuth = 0.5 * (az0 + GeogLonR(az1, az0));
	ryib_p->azimuth = DEG_PER_RAD * GeogLonR(ryib_p->azimuth, 0.0);
	ryib_p->elevation = DEG_PER_RAD * 0.5
	    * (vol_p->ray_hdr[s][r].tilt0 + vol_p->ray_hdr[s][r].tilt1);
	ryib_p->peak_power = 0.001 * vol_p->ih.tc.tmi.power;
	ryib_p->ray_status = 0;

	/*
	   Populate asib block. Assume stationary ground radar
	 */

	asib_p->longitude = radd_p->radar_longitude;
	asib_p->latitude = radd_p->radar_latitude;
	asib_p->altitude_msl = radd_p->radar_altitude;
	asib_p->altitude_agl = 0.001 * vol_p->ih.ic.radar_ht;
    }
    if ( num_rays_d == 0 ) {
	fprintf(stderr, "Sweep has no good rays.\n");
	status = SIGMET_BAD_VOL;
	goto error;
    }
    swib_p->num_rays = num_rays_d;

    /*
       Populate dat array.
     */

    num_bins = num_cells;
    ray_p = NULL;
    if ( !(ray_p = MALLOC(num_bins * sizeof(float)))) {
	fprintf(stderr, "Could not allocate ray data buffer.\n");
	status = SIGMET_MEM_FAIL;
	goto error;
    }
    for (r_p = ray_p ; r_p < ray_p + num_bins; r_p++) {
	*r_p = NAN;
    }
    for (p = 0; p < num_parms; p++) {
	data_type_s = vol_p->dat[p].data_type_s;
	if ( strlen(data_type_s) == 0 ) {
	    continue;
	}
	if ( Sigmet_DataType_GetN(data_type_s, &sig_type)
		&& soloii_abbrv[sig_type] ) {
	    p_d = Dorade_Parm_Idx(swp_p, soloii_abbrv[sig_type]);
	} else {
	    p_d = Dorade_Parm_Idx(swp_p, data_type_s);
	}
	if ( p_d == -1 ) {
	    fprintf(stderr, "Could not find parameter %s in sweep.\n",
		    data_type_s);
	    status = SIGMET_BAD_VOL;
	    goto error;
	}
	swp_p->dat[p_d] = Dorade_Alloc2F(num_rays, num_cells);
	if ( !swp_p->dat[p_d] ) {
	    fprintf(stderr, "Failed to allocate memory for data array with %d "
		    "parameters, %d rays, and %d cells.\n",
		    num_parms, num_rays, num_cells);
	    status = SIGMET_MEM_FAIL;
	    goto error;
	}
	dat_p = swp_p->dat[p_d];
	for (r = 0; r < num_rays_d; r++) {
	    if ( vol_p->ray_hdr[s][r].ok ) {
		status = Sigmet_Vol_GetRayDat(vol_p, p, s, r, &ray_p);
		if ( (status != SIGMET_OK) ) {
		    fprintf(stderr, "Could not retrieve ray data "
			    "from Sigmet volume\n");
		    status = SIGMET_BAD_VOL;
		    goto error;
		}
		for (c = 0; c < num_cells; c++) {
		    if (isfinite(ray_p[c])) {
			dat_p[r][c] = ray_p[c];
		    } else {
			dat_p[r][c] = NAN;
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
}
