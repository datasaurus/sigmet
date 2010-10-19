/*
 * dorade.c --
 *
 *	This file defines functions that write Sigmet volumes to DORADE
 *	(Solo sweep) files.
 *
 * Copyright (c) 2004 Gordon D. Carrie
 *
 * Licensed under the Open Software License version 2.1
 *
 * Please send feedback to user0@tkgeomap.org
 * @(#) $Id: dorade.c,v 1.29 2007/05/16 18:00:30 tkgeomap Exp $
 *
 **********************************************************************
 *
 */

#include "sigmet.h"

/*
 * Descriptor sizes.
 */

#define SITE_NAME_SZ 8
#define SSWB_SZ 196
#define ROT_ANG_TABLE_SZ 32
#define VOL_DESCR_SZ 72
#define RAD_DESCR_SZ 144
#define PARM_DESCR_SZ 104
#define CFAC_SZ 72
#define SWIB_SZ 40
#define RYIB_SZ 44
#define PLATFM_INFO_BLK_SZ 80
#define NULL_SZ 8
#define RKTB_ENTRY_SZ 12

/*
 * Local function declaration.
 */

static char * abbrv(enum Sigmet_DataType y);

/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_ToDorade --
 *
 *	This function writes a sweep from a Sigmet raw volume DORADE
 *	sweep structure.
 *
 * Results:
 *	Success/failure code.  If the function fails, and error message will
 *	be appended with Radar_ErrMsgAppend.
 *
 * Side effects:
 * 	The given Dorade_Sweep structure is modified.
 *
 *-------------------------------------------------------------------------
 */

int
Sigmet_ToDorade(sigPtr, s, doradePtr)
    struct Sigmet_Vol *sigPtr;		/* Sigmet volume */
    unsigned s;				/* Sweep index */
    struct Dorade_Sweep *doradePtr;	/* DORADE sweep */
{
    float *log_r = NULL;		/* log10(r) array, needed for dM
					 * calculation */
    float **dM = NULL;			/* Returned power array, dimensioned
					 * [ray][bin] */
    float *distPtr = NULL;		/* Distance from radar to cell, m */
    struct Dorade_ParmDesc *parmPtr = NULL;
    struct Dorade_RayHdr *rayHdrPtr = NULL;
    float ***dat = NULL;
    unsigned n_good_rays;		/* Number of good rays */
    unsigned n_dorade_rays;		/* Number of rays in DORADE sweep */
    unsigned n_sigmet_rays;		/* Number of rays in Sigmet sweep
					 * (includes bad rays) */
    unsigned n_types, n_bins;		/* Convenience variables */
    int y, b;				/* Loop parameters */
    int r_s;				/* Index of ray in sigmet sweep */
    int r_d;				/* Index of ray in dorade sweep */
    double wavelength;
    double beam_width;			/* Average azimuth or tilt difference */
    double vel_ua;			/* Unambiguous velocity */
    Angle az0, az1;			/* Initial and final azimuths for ray */
    Angle tilt0, tilt1;			/* Initial and final tilts for ray */

    if (sigPtr == NULL) {
	Radar_ErrMsgAppend("Sigmet to Dorade function given "
		"non-existent volume.  ");
	return 0;
    }

    n_types = sigPtr->num_types;
    n_sigmet_rays = sigPtr->rays_in_sweep;
    n_bins = sigPtr->num_output_bins;

    if (s >= sigPtr->num_sweeps) {
	Radar_ErrMsgAppend("Sigmet sweep index out of range.  ");
	return 0;
    }

    /*
     * If volume has reflectivity dZ, compute returned power dM and add
     * it to the output.  Assume reflectivity is stored as either DB_DBZ
     * or DB_DBZ2.
     */

    for (y = 0; y < n_types; y++) {
	if (sigPtr->types[y] == DB_DBZ || sigPtr->types[y] == DB_DBZ2) {
	    size_t sz;
	    int n;		/* Loop parameters */
	    float bin0;		/* Range to middle of first bin in meters */
	    float bin_step;	/* Bin size in meters */

	    n_types++;

	    /*
	     * Allocate arrays for log10(r) and db
	     */

	    if ( !(log_r = (float *)RADAR_MALLOC(n_bins * sizeof(float))) ) {
		Radar_ErrMsgAppend("Could not allocate distance correction"
			" array.");
		goto error;
	    }
	    sz = n_sigmet_rays * (sizeof(float *) + n_bins * sizeof(float));
	    if ( !(dM =  (float **)RADAR_MALLOC(sz)) ) {
		Radar_ErrMsgAppend("Could not allocate receiver power array.");
		goto error;
	    }
	    dM[0] = (float *)(dM + n_sigmet_rays);
	    for (n = 1; n < n_sigmet_rays; n++) {
		dM[n] = dM[n - 1] + n_bins;
	    }

	    /*
	     * Compute dM
	     */

	    bin_step = 0.01 * sigPtr->output_bin_step;
	    bin0 = 0.01 * sigPtr->range_1st_bin_cm + 0.5 * bin_step;
	    for (b = 0; b < n_bins; b++) {
		log_r[b] = log10(bin0 + bin_step * b);
	    }
	    for (r_s = 0; r_s < n_sigmet_rays; r_s++) {
		for (b = 0; b < sigPtr->ray_nbins[s][r_s]; b++) {
		    dM[r_s][b] = Radar_ValIsData(sigPtr->dat[s][y][r_s][b])
			? sigPtr->dat[s][y][r_s][b] - 20.0 * log_r[b]
			: Radar_NoData();
		}
	    }

	    break;
	}
    }

    for (r_s = 0, n_good_rays = 0; r_s < n_sigmet_rays; r_s++) {
	if ( !Sigmet_BadRay(sigPtr, s, r_s) ) {
	    n_good_rays++;
	}
    }

    strncpy(doradePtr->radar_name, sigPtr->site_name, 8);
    doradePtr->n_parms = n_types; 
    doradePtr->vol_num = 1;
    doradePtr->time = sigPtr->sweep_time[s];
    doradePtr->n_sensors = 1;
    doradePtr->peak_power = sigPtr->transmit_power;
    switch (sigPtr->scan_mode) {
	case PPI_S:
	case PPI_C:
	    for (beam_width = 0.0, r_s = 0; r_s < n_sigmet_rays; r_s++) {
		Angle az0, az1;

		if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
		    continue;
		}
		az0 = sigPtr->ray_az0[s][r_s];
		az1 = DomainLon(sigPtr->ray_az1[s][r_s], az0);
		beam_width += fabs(az1 - az0);
	    }
	    beam_width /= n_good_rays;
	    break;
	case RHI:
	    for (beam_width = 0.0, r_s = 0; r_s < n_sigmet_rays; r_s++) {
		Angle tilt0, tilt1;

		if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
		    continue;
		}
		tilt0 = DomainLat(sigPtr->ray_tilt0[s][r_s]);
		tilt1 = DomainLat(sigPtr->ray_tilt1[s][r_s]);
		beam_width += fabs(tilt1 - tilt0);
	    }
	    beam_width /= n_good_rays;
	    break;
    }
    doradePtr->horz_beam_width = beam_width;
    doradePtr->vert_beam_width = beam_width;
    doradePtr->radar_type = 0;
    switch (sigPtr->scan_mode) {
	case PPI_S:
	case PPI_C:
	    doradePtr->scan_mode = DORADE_PPI;
	    break;
	case RHI:
	    doradePtr->scan_mode = DORADE_RHI;
	    break;
	case MAN_SCAN:
	    doradePtr->scan_mode = DORADE_TARGET_MANUAL;	
	    break;
	case FILE_SCAN:
	    doradePtr->scan_mode = DORADE_CALIBRATION;	
	    break;
    }
    doradePtr-> compression = 0;
    doradePtr->radar_location.lat = sigPtr->latitude;
    doradePtr->radar_location.lon = sigPtr->longitude;
    doradePtr->radar_altitude
	=  0.001 * (sigPtr->ground_height + sigPtr->tower_height);

    wavelength = 0.0001 * sigPtr->wavelength;
    switch (sigPtr->multi_prf_mode_flag) {
	case ONE_ONE:
	    vel_ua = 0.25 * wavelength * sigPtr->prf;
	    break;
	case TWO_THREE:
	    vel_ua = 0.25 * wavelength * 1.5 * sigPtr->prf * 2;
	    break;
	case THREE_FOUR:
	    vel_ua = 0.25 * wavelength * 4.0 / 3.0 * sigPtr->prf * 3;
	    break;
	case FOUR_FIVE:
	    vel_ua = 0.25 * wavelength * (1.25 * sigPtr->prf) * 4;
	    break;
    }
    doradePtr->eff_unamb_vel = vel_ua;
    doradePtr->eff_unamb_range = 0.5 * 3.0e5 / sigPtr->prf;
    doradePtr->num_freq_trans = 1;
    doradePtr->freq1 = 3.0e8 / wavelength;
    doradePtr->n_cells = n_bins;
    if ( !(distPtr = RADAR_MALLOC(n_bins * sizeof(float))) ) {
	Radar_ErrMsgAppend("Could not allocate cell vector.");
	goto error;
    }
    for (b = 0; b < n_bins; b++) {
	distPtr[b] = 0.01
	    * (sigPtr->range_1st_bin_cm + b * sigPtr->output_bin_step);
    }
    doradePtr->distPtr = distPtr;
    if ( !(parmPtr = RADAR_MALLOC(n_types * sizeof(struct Dorade_ParmDesc))) ) {
	Radar_ErrMsgAppend("Could not allocate parameter array.");
	goto error;
    }
    for (y = 0; y < sigPtr->num_types; y++) {
	enum Sigmet_DataType type = sigPtr->types[y];

	Dorade_InitParm(parmPtr + y);
	strncpy(parmPtr[y].name, abbrv(type), 8);
	strncpy(parmPtr[y].description, Sigmet_DataType_Descr(type), 40);
	switch (type) {
	    case DB_XHDR:
		Radar_ErrMsgAppend("Sigmet read failed "
			"(extended headers stored inadvertently)");
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
		strncpy(parmPtr[y].units, "No unit", 8);
		break;
	    case DB_DBT:
	    case DB_DBZ:
	    case DB_ZDR:
	    case DB_DBZC:
	    case DB_DBT2:
	    case DB_DBZ2:
	    case DB_ZDR2:
	    case DB_DBZC2:
		strncpy(parmPtr[y].units, "dB", 8);
		break;
	    case DB_VEL:
	    case DB_WIDTH:
	    case DB_VEL2:
	    case DB_WIDTH2:
	    case DB_VELC:
	    case DB_VELC2:
		strncpy(parmPtr[y].units, "m/s", 8);
		break;
	    case DB_PHIDP:
	    case DB_PHIDP2:
		strncpy(parmPtr[y].units, "degrees", 8);
		break;
	    case DB_RAINRATE2:
		strncpy(parmPtr[y].units, "mm/hr", 8);
		break;
	}
	parmPtr[y].binary_format = 2;
	strncpy(parmPtr[y].threshold_field, "NONE    ", 8);
	parmPtr[y].scale = 1.0;
	parmPtr[y].bias = 0.0;
	parmPtr[y].bad_data = SHRT_MIN;
    }

    if (dM) {
	Dorade_InitParm(parmPtr + y);
	strncpy(parmPtr[y].name, "DM", 8);
	strncpy(parmPtr[y].description, "Returned power", 40);
	strncpy(parmPtr[y].units, "dB", 8);
	parmPtr[y].binary_format = 2;
	strncpy(parmPtr[y].threshold_field, "NONE    ", 8);
	parmPtr[y].scale = 1.0;
	parmPtr[y].bias = 0.0;
	parmPtr[y].bad_data = SHRT_MIN;
    }
    doradePtr->parmPtr = parmPtr;

    strncpy(doradePtr->swib_comment, sigPtr->site_name, 8);
    doradePtr->sweep_num = s + 1;
    doradePtr->n_rays = doradePtr->n_good_rays = n_dorade_rays = n_good_rays;
    switch (sigPtr->scan_mode) {
	case PPI_S:
	case PPI_C:
	    az0 = sigPtr->ray_az0[s][0];
	    az1 = DomainLon(sigPtr->ray_az1[s][0], az0);
	    doradePtr->start_angle = GwchLon(0.5 * (az0 + az1));
	    az0 = sigPtr->ray_az0[s][n_sigmet_rays - 1];
	    az1 = DomainLon(sigPtr->ray_az1[s][n_sigmet_rays - 1], az0);
	    doradePtr->stop_angle = GwchLon(0.5 * (az0 + az1));
	    doradePtr->fixed_angle = sigPtr->sweep_angle[s];
	    doradePtr->filter_flag = 0;
	    break;
	case RHI:
	    tilt0 = sigPtr->ray_tilt0[s][0];
	    tilt1 = sigPtr->ray_tilt1[s][0];
	    doradePtr->start_angle = AngleToDeg(0.5 * (tilt0 + tilt1));

	    tilt0 = sigPtr->ray_tilt0[s][n_sigmet_rays - 1];
	    tilt1 = sigPtr->ray_tilt1[s][n_sigmet_rays - 1];
	    doradePtr->stop_angle = AngleToDeg(0.5 * (tilt0 + tilt1));
	    doradePtr->fixed_angle = sigPtr->sweep_angle[s];
	    doradePtr->filter_flag = 0;
	    break;
	case MAN_SCAN:
	case FILE_SCAN:
	    break;
    }

    rayHdrPtr = RADAR_MALLOC(n_dorade_rays * sizeof(struct Dorade_RayHdr));
    if ( !rayHdrPtr ) {
	Radar_ErrMsgAppend("Could not allocate ray header array.");
	goto error;
    }
    for (r_s = 0, r_d = 0; r_s < n_sigmet_rays; r_s++) {
	if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
	    continue;
	}
	rayHdrPtr[r_d].good = 1;
	rayHdrPtr[r_d].time = sigPtr->ray_time[s][r_s];
	az0 = sigPtr->ray_az0[s][r_s];
	az1 = DomainLon(sigPtr->ray_az1[s][r_s], az0);
	rayHdrPtr[r_d].azimuth = GwchLon(0.5 * (az0 + az1));
	tilt0 = sigPtr->ray_tilt0[s][r_s];
	tilt1 = sigPtr->ray_tilt1[s][r_s];
	rayHdrPtr[r_d].elevation = 0.5 * (tilt0 + tilt1);
	rayHdrPtr[r_d].latitude = sigPtr->latitude;
	rayHdrPtr[r_d].longitude = GwchLon(sigPtr->longitude);
	rayHdrPtr[r_d].altitude_msl = sigPtr->ground_height;
	rayHdrPtr[r_d].altitude_agl = sigPtr->tower_height;
	r_d++;
    };
    doradePtr->rayHdrPtr = rayHdrPtr;
    if ( !(dat = Dorade_AllocDat(n_types, n_dorade_rays, n_bins)) ) {
	Radar_ErrMsgAppend("Could not allocate data array.  ");
	goto error;
    }
    for (y = 0; y < sigPtr->num_types; y++) {
	switch (sigPtr->types[y]) {
	    case DB_XHDR:
		Radar_ErrMsgAppend("Sigmet read failed "
			"(extended headers stored inadvertently)");
		goto error;
	    case DB_DBT:
	    case DB_DBZ:
	    case DB_ZDR:
	    case DB_DBZC:
	    case DB_DBT2:
	    case DB_DBZ2:
	    case DB_VEL2:
	    case DB_WIDTH2:
	    case DB_ZDR2:
	    case DB_RAINRATE2:
	    case DB_KDP:
	    case DB_KDP2:
	    case DB_PHIDP:
	    case DB_VELC:
	    case DB_SQI:
	    case DB_RHOHV:
	    case DB_RHOHV2:
	    case DB_DBZC2:
	    case DB_VELC2:
	    case DB_SQI2:
	    case DB_PHIDP2:
	    case DB_LDRH:
	    case DB_LDRH2:
	    case DB_LDRV:
	    case DB_LDRV2:
		for (r_s = 0, r_d = 0; r_s < n_sigmet_rays; r_s++) {
		    if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
			continue;
		    }
		    for (b = 0; b < sigPtr->ray_nbins[s][r_s]; b++) {
			dat[y][r_d][b] = sigPtr->dat[s][y][r_s][b];
		    }
		    for ( ; b < n_bins; b++) {
			dat[y][r_d][b] = Radar_NoData();
		    }
		    r_d++;
		}
		break;
	    case DB_VEL:
	    case DB_WIDTH:
		/*
		 * In Sigmet volumes, 1 byte velocity values are
		 * scaled by unambiguous velocity.
		 */

		for (r_s = 0, r_d = 0; r_s < n_sigmet_rays; r_s++) {
		    if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
			continue;
		    }
		    for (b = 0; b < sigPtr->ray_nbins[s][r_s]; b++) {
			if (Radar_ValIsData(sigPtr->dat[s][y][r_s][b])) {
			    dat[y][r_d][b] = vel_ua * sigPtr->dat[s][y][r_s][b];
			} else {
			    dat[y][r_d][b] = Radar_NoData();
			}
		    }
		    for ( ; b < n_bins; b++) {
			dat[y][r_d][b] = Radar_NoData();
		    }
		    r_d++;
		}
		break;
	}
    }

    /*
     * Add dM type to Dorade sweep, if available.
     */

    if (dM) {
	for (r_s = 0, r_d = 0; r_s < n_sigmet_rays; r_s++) {
	    if ( Sigmet_BadRay(sigPtr, s, r_s) ) {
		continue;
	    }
	    for (b = 0; b < sigPtr->ray_nbins[s][r_s]; b++) {
		dat[y][r_d][b] = dM[r_s][b];
	    }
	    for ( ; b < n_bins; b++) {
		dat[y][r_d][b] = Radar_NoData();
	    }
	    r_d++;
	}
    }
    doradePtr->dat = dat;

    /*
     * Give back memory.
     */

    RADAR_FREE(log_r);
    RADAR_FREE(dM);
    return 1;

error:
    RADAR_FREE(log_r);
    RADAR_FREE(dM);
    RADAR_FREE(distPtr);
    doradePtr->distPtr = NULL;
    RADAR_FREE(parmPtr);
    doradePtr->parmPtr = NULL;
    RADAR_FREE(rayHdrPtr);
    doradePtr->rayHdrPtr = NULL;
    if (dat) {
	Radar_FreeArr3(dat);
    }
    doradePtr->dat = NULL;
    return 0;
}

/*
 *------------------------------------------------------------------------
 *
 * abbrv --
 *
 * 	This function makes an abbreviation for a Sigmet data type.
 * 	Usually it returns the Sigmet abbreviation.  For some data types,
 * 	it returns an alternate abbreviation that Solo seems to need.
 *
 * Results:
 * 	Return value is a character string.  It should not be modified by the
 * 	user.
 *
 * Side effects:
 * 	None.
 *
 *------------------------------------------------------------------------
 */

static char *
abbrv(y)
    enum Sigmet_DataType y;
{
    static char *abbrv[SIGMET_NTYPES] = {
	NULL, "ZT", "DZ", "VR", "SW", NULL};

    return (abbrv[y] != NULL) ? abbrv[y] : Sigmet_DataType_Abbrv(y);
}
