/*
   -	sigmet_dorade.c --
   -		Translate Sigmet data into DORADE.
   -
   .	Copyright (c) 2010 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: $ $Date: $
 */

#include <string.h>
#include <math.h>
#include "sigmet.h"
#include "alloc.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "dorade_lib.h"

/*
   Copy metadata and data from sweep s of the Sigmet volume at vol_p to
   the DORADE sweep at swp_p.
   swp_p should have been initialized with Dorade_Sweep_Init.
   Leave error for Err_Get() and return 0 on failure.
 */
int Sigmet_ToDorade(struct Sigmet_Vol *vol_p, int s, struct Dorade_Sweep *swp_p)
{
    double epoch;				/* 1970/01/01 */
    int year, mon, day, hr, min; double sec;	/* Sweep time */
    double wave_len;				/* Wavelength from vol_p */
    double prf;					/* PRF from vol_p */
    int p, r, c;				/* Loop parameters */
    int num_parms, num_rays, num_cells;		/* Convenience */

    /* Convenience variables point into swp_p */
    struct Dorade_SSWB *sswb_p;
    struct Dorade_Sensor *sensor_p;
    struct Dorade_RADD *radd_p;
    struct Dorade_PARM *parm_p;
    struct Dorade_CELV *celv_p;
    struct Dorade_SWIB *swib_p;
    struct Dorade_Ray_Hdr *ray_hdr_p;
    struct Dorade_RYIB *ryib_p;
    struct Dorade_ASIB *asib_p;
    float ***dat_p;

    if ( s > vol_p->ih.ic.num_sweeps ) {
	Err_Append("Sweep index out of range. ");
	goto error;
    }

    /* Populate comm block */
    if ( snprintf(swp_p->comm.comment, 500, "Sigmet volume sweep %d, task %s", 
		s, vol_p->ph.pc.task_name) >= 500 ) {
	Err_Append("Could not set COMM block. String too big.");
	goto error;
    }

    /* Populate sswb block */
    sswb_p = &swp_p->sswb;
    epoch = Tm_CalToJul(1970, 1, 1, 0, 0, 0.0);
    sswb_p->i_start_time = (vol_p->sweep_time[s] - epoch) * 86400 + 0.5;
    sswb_p->compression_flag = 0;
    num_parms = sswb_p->num_parms = vol_p->num_types;
    strncpy(sswb_p->radar_name, vol_p->ih.ic.su_site_name, 8);

    /* Populate vold block */
    swp_p->vold.volume_num = 1;
    swp_p->vold.maximum_bytes = 65500;
    if ( !Tm_JulToCal(vol_p->sweep_time[s], &year, &mon, &day, &hr, &min, &sec) ) {
	Err_Append("Could not set sweep time. ");
	goto error;
    }
    swp_p->vold.year = year;
    swp_p->vold.month = mon;
    swp_p->vold.day = day;
    swp_p->vold.data_set_hour = hr;
    swp_p->vold.data_set_minute = min;
    swp_p->vold.data_set_second = sec;
    strncpy(swp_p->vold.gen_facility, vol_p->ih.ic.su_site_name, 8);
    swp_p->vold.num_sensors = 1;

    /* Populate sensor block: radd parm1 parm2 ... parmN celvORcsfd cfac */
    sensor_p = &swp_p->sensor;

    /* Populate radd block */
    radd_p = &sensor_p->radd;
    strncpy(radd_p->radar_name, vol_p->ih.ic.su_site_name, 8);
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
	    radd_p->scan_mode = 1;	/* ppi */
	    break;
	case RHI:
	    radd_p->scan_mode = 3;	/* rhi */
	    break;
	case MAN_SCAN:
	    radd_p->scan_mode = 6;	/* manual */
	    break;
	case FILE_SCAN:
	    radd_p->scan_mode = DORADE_BAD_I;
	    break;
    }
    radd_p->num_parms = num_parms;
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
    radd_p->freq1 = 2.9979e8 / wave_len;
    radd_p->interpulse_per1 = 1.0 / prf;
    strncpy(radd_p->config_name, vol_p->ph.pc.task_name, 8);
    radd_p->pulse_width = 0.01 * vol_p->ih.tc.tdi.pulse_w;
    strncpy(radd_p->site_name, vol_p->ih.ic.su_site_name, 20);

    /* Populate parm blocks */
    if ( !(sensor_p->parm = CALLOC(num_parms, sizeof(struct Dorade_PARM))) ) {
	Err_Append("Could not allocate array of parameter descriptors. ");
    }
    for (p = 0; p < num_parms; p++) {
	parm_p = sensor_p->parm + p;

	strncpy(parm_p->parameter_name, Sigmet_DataType_Abbrv(vol_p->types[p]), 8);
	strncpy(parm_p->param_description, Sigmet_DataType_Descr(vol_p->types[p]),
		40);
	switch (vol_p->types[p]) {
	    case DB_ERROR:
		Err_Append("Bad volume (unknown data type DB_ERROR)");
		goto error;
	    case DB_XHDR:
		Err_Append("Bad volume (extended headers stored inadvertently)");
		goto error;
	    case DB_KDP:
	    case DB_KDP2:
	    case DB_SQI:
	    case DB_SQI2:
	    case DB_RHOHV:
	    case DB_RHOHV2:
	    case DB_LDRH:
	    case DB_LDRH2:
	    case DB_LDRV:
	    case DB_LDRV2:
		strncpy(parm_p->param_units, "No unit", 8);
		break;
	    case DB_DBT:
	    case DB_DBZ:
	    case DB_ZDR:
	    case DB_DBZC:
	    case DB_DBT2:
	    case DB_DBZ2:
	    case DB_ZDR2:
	    case DB_DBZC2:
		strncpy(parm_p->param_units, "dB", 8);
		break;
	    case DB_VEL:
	    case DB_WIDTH:
	    case DB_VEL2:
	    case DB_WIDTH2:
	    case DB_VELC:
	    case DB_VELC2:
		strncpy(parm_p->param_units, "m/s", 8);
		break;
	    case DB_PHIDP:
	    case DB_PHIDP2:
		strncpy(parm_p->param_units, "degrees", 8);
		break;
	    case DB_RAINRATE2:
		strncpy(parm_p->param_units, "mm/hr", 8);
		break;
	}

	parm_p->xmitted_freq = 2.9979e8 / wave_len;
	parm_p->recvr_bandwidth = vol_p->ih.tc.tci.bandwidth;
	parm_p->pulse_width = vol_p->ih.tc.tdi.pulse_w * 1.0e-8 * 2.9979e8;
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

    /* Populate celv block */
    sensor_p->cell_geo_t = CELV;
    celv_p = &sensor_p->cell_geo.celv;
    celv_p->num_cells = num_cells;
    if ( !(celv_p->dist_cells = CALLOC(num_cells, sizeof(double))) ) {
	Err_Append("Could not allocate cell vector block. ");
	goto error;
    }
    for (c = 0; c < num_cells; c++) {
	celv_p->dist_cells[c]
	    = parm_p->meters_to_first_cell + c * parm_p->meters_between_cells;
    }

    /* Note: Sigmet volume does not have "correction factors", so skip CFAC */

    /* Populate struct Dorade_SWIB swib block */
    swib_p = &swp_p->swib;
    strncpy(swib_p->radar_name, sswb_p->radar_name, 8);
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

    /* Populate struct Dorade_Ray_Hdr *ray_hdr array */
    if ( !(swp_p->ray_hdr = CALLOC(num_rays, sizeof(struct Dorade_Ray_Hdr))) ) {
	Err_Append("Could not allocate space for ray headers. ");
	goto error;
    }
    for (r = 0; r < num_rays; r++) {
	ray_hdr_p = swp_p->ray_hdr + r;
	ryib_p = &ray_hdr_p->ryib;
	asib_p = &ray_hdr_p->asib;

	/* Initialize ryib block */
	ryib_p->sweep_num = s;
	ryib_p->julian_day = -999;
	ryib_p->hour = -999;
	ryib_p->minute = -999;
	ryib_p->second = -999;
	ryib_p->millisecond = -999;
	ryib_p->azimuth = -999.0;
	ryib_p->elevation = -999.0;
	ryib_p->peak_power = -999.0;
	ryib_p->ray_status = 2;

	/* Initialize asib block */
	asib_p->longitude = -999.0;
	asib_p->latitude = -999.0;
	asib_p->altitude_msl = -999.0;
	asib_p->altitude_agl = -999.0;
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

	/* Populate ryib block */
	ryib_p->sweep_num = s;
	if ( !Tm_JulToCal(vol_p->ray_time[s][r],
		    &year, &mon, &day, &hr, &min, &sec) ) {
	    Err_Append("Could not get ray time. ");
	    goto error;
	}
	julian0 = Tm_CalToJul(year, 1, 1, 0, 0, 0.0);
	ryib_p->julian_day = vol_p->ray_time[s][r] - julian0;
	ryib_p->hour = hr;
	ryib_p->minute = min;
	ryib_p->second = sec;
	ryib_p->millisecond = (sec - ryib_p->second) * 1000;
	az0 = vol_p->ray_az0[s][r];
	az1 = vol_p->ray_az1[s][r];
	ryib_p->azimuth = 0.5 * (az0 + GeogLonR(az1, az0));
	ryib_p->azimuth = DEG_PER_RAD * GeogLonR(ryib_p->azimuth, 0.0);
	ryib_p->elevation
	    = DEG_PER_RAD * 0.5 * (vol_p->ray_tilt0[s][r] + vol_p->ray_tilt1[s][r]);
	ryib_p->peak_power = 0.001 * vol_p->ih.tc.tmi.power;
	ryib_p->ray_status = vol_p->ray_ok[s][r] ? 0 : 2;

	/* Populate asib block. Assume stationary ground radar */
	asib_p->longitude = radd_p->radar_longitude;
	asib_p->latitude = radd_p->radar_latitude;
	asib_p->altitude_msl = radd_p->radar_altitude;
	asib_p->altitude_agl = 0.001 * vol_p->ih.ic.radar_ht;
    }

    /* Populate int ***dat array */
    if ( !Dorade_Sweep_AllocDat(swp_p, num_parms, num_rays, num_cells) ) {
	Err_Append("Could not allocate data array. ");
	goto error;
    }
    dat_p = swp_p->dat;
    for (p = 0; p < num_parms; p++) {
	int bad_data = swp_p->sensor.parm[p].bad_data;
	enum Sigmet_DataType type = vol_p->types[p];

	for (r = 0; r < num_rays; r++) {
	    if ( !vol_p->ray_ok[s][r] ) {
		for (c = 0; c < num_cells; c++) {
		    dat_p[p][r][c] = bad_data;
		}
	    } else {
		for (c = 0; c < num_cells; c++) {
		    float d;

		    d = Sigmet_DataType_ItoF(type, *vol_p, vol_p->dat[p][s][r][c]);
		    if (Sigmet_IsData(d)) {
			dat_p[p][r][c] = d;
		    } else {
			dat_p[p][r][c] = bad_data;
		    }
		}
	    }
	}
    }

    return 1;

error:
    Dorade_Sweep_Free(swp_p);
    return 0;
};
