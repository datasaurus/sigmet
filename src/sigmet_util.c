
/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_VolBinLatLon --
 *
 *	This function computes the location of a bin.
 *
 * Results:
 *	Latitude-longitude of a bin.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

GeoPt
Sigmet_VolBinLatLon(sigPtr, s, y, r, b)
    struct Sigmet_Vol *sigPtr;	/* Raw volume */
    unsigned s;			/* Sweep */
    unsigned y;			/* Type */
    unsigned r;			/* Ray */
    unsigned b;			/* Bin */
{
    GeoPt ctr;			/* Radar location */
    double az0, az1;		/* Start and end azimuths of ray */
    double az, tilt;		/* Ray azimuth and tilt */
    double d;			/* Distance to bin along ray, in meters */
    double R;			/* Distance from center of Earth to antenna */
    double delta;		/* Great circle distance to point under bin */

    if ( !sigPtr || s >= sigPtr->num_sweeps || y >= sigPtr->num_types
	    || r >= sigPtr->rays_in_sweep || b >= sigPtr->num_output_bins) {
	return GeoPtNowhere();
    }
    ctr.lat = sigPtr->latitude;
    ctr.lon = sigPtr->longitude;
    R = REarth() + sigPtr->ground_elevation + sigPtr->tower_height;
    tilt = 0.5 * (sigPtr->ray_tilt0[s][r] + sigPtr->ray_tilt1[s][r]);
    az0 = sigPtr->ray_az0[s][r];
    az1 = DomainLon(sigPtr->ray_az1[s][r], az0);
    az = 0.5 * (az0 + az1);
    d = 0.01 * (sigPtr->range_1st_bin_cm + b * sigPtr->output_bin_step);
    delta = asin(d * cos(tilt) / sqrt(R * R + d * d + 2 * R * d * sin(tilt)));
    return GeoStep(ctr, az, delta);
}

/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_VolLatLonToBin --
 *
 *	This function computes ray and bin index for the gate above a
 *	given location.
 *
 * Results:
 *	If the sweep has a gate over the given point, the ray and bin
 *	indeces are filled in with values for the gate and the function
 *	returns true.  Otherwise, return value is false.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

int
Sigmet_VolLatLonToBin(sigPtr, s, geoPt, rPtr, bPtr)
    struct Sigmet_Vol *sigPtr;	/* (in) Raw volume */
    unsigned s;			/* (in) Sweep */
    GeoPt geoPt;		/* (in) Lat-lon for which we want gate */
    unsigned *rPtr;		/* (out) Ray */
    unsigned *bPtr;		/* (out) Bin */
{
    GeoPt ctr;			/* Radar location */
    double R;			/* Distance from center of Earth to antenna */
    double tilt;		/* Sweep tilt */
    double delta;		/* Distance along ground from radar to point */
    double d;			/* Distance along beam from radar to point */
    double az;			/* Azimuth from radar to point */
    double *az0, *az1;		/* Sweep azimuths */
    int b, r;			/* Candidates for *bPtr and *rPtr */

    if ( !sigPtr
	    || (sigPtr->scan_mode != PPI_S && sigPtr->scan_mode != PPI_C)
	    || s >= sigPtr->num_sweeps) {
	return 0;
    }
    ctr.lat = sigPtr->latitude;
    ctr.lon = sigPtr->longitude;

    /*
     * Compute the bin index for the gate over the point.
     */

    R = REarth() + sigPtr->ground_elevation + sigPtr->tower_height;
    tilt = sigPtr->sweep_angle[s];
    delta = GeoDistance(ctr, geoPt);
    if (delta + tilt == PI_2) {
	return 0;
    }
    d = 100.0 * R * sin(delta) / cos(delta + tilt);
    b = (unsigned)((d - sigPtr->range_1st_bin_cm) / sigPtr->output_bin_step);
    if (b < 0 || b >= sigPtr->num_output_bins) {
	return 0;
    }

    /*
     * Compute the ray index for the gate over the point.
     */

    az0 = sigPtr->ray_az0[s];
    az1 = sigPtr->ray_az1[s];
    az = Azimuth(ctr, geoPt);
    for (r = 0; r < sigPtr->rays_in_sweep && !LonBtwn1(az, az0[r], az1[r]); r++) {
	continue;
    }
    if (r >= sigPtr->rays_in_sweep) {
	return 0;
    }

    *bPtr = b;
    *rPtr = r;
    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_VolBinOutline --
 *
 *	This function computes the corners of a bin.
 *
 * Results:
 *	A geoline (see geoLn (3)) with four points corresponding to the
 *	corners of a bin.
 *
 * Side effects:
 *	A geoline object is created.  The user should eventually destroy it
 *	with a call to GeoLnDestroy.
 *
 *-------------------------------------------------------------------------
 */

GeoLn
Sigmet_VolBinOutline(sigPtr, s, y, r, b)
    struct Sigmet_Vol *sigPtr;	/* Raw volume */
    unsigned s;			/* Sweep */
    unsigned y;			/* Type */
    unsigned r;			/* Ray */
    unsigned b;			/* Bin */
{
    GeoLn ln;			/* Result */
    GeoPt ctr;			/* Radar location */
    float R;			/* Distance from center of Earth to antenna */
    double az0, az1;		/* Ray azimuth at start and end of ray */
    double len;			/* Length of bin over ground */
    double tilt;		/* Ray tilt */
    double cos_tilt;		/* Cosine of sweep tilt (used several times) */
    double delta;		/* Distance to start of bin */
    double bin0, step;		/* Distance to first bin, bin size */
    double d;			/* Distance to bin */
    int n;

    if ( !sigPtr || s >= sigPtr->num_sweeps || y >= sigPtr->num_types
	    || r >= sigPtr->rays_in_sweep || b >= sigPtr->num_output_bins) {
	return NULL;
    }
    if ( !(ln = GeoLnCreate(4)) ) {
	return NULL;
    }
    ctr.lat = sigPtr->latitude;
    ctr.lon = sigPtr->longitude;
    R = REarth() + sigPtr->ground_elevation + sigPtr->tower_height;
    tilt = 0.5 * (sigPtr->ray_tilt0[s][r] + sigPtr->ray_tilt1[s][r]);
    cos_tilt = cos(tilt);
    az0 = sigPtr->ray_az0[s][r];
    az1 = DomainLon(sigPtr->ray_az1[s][r], az0);
    bin0 = 0.01 * sigPtr->range_1st_bin_cm;
    step = 0.01 * sigPtr->output_bin_step;
    d = bin0 + b * step;
    delta = asin(d * cos_tilt / sqrt(R * R + d * d + 2 * R * d * sin(tilt)));
    len = step * cos_tilt / REarth();
    GeoLnAddPt(GeoStep(ctr, az1, delta), ln);
    GeoLnAddPt(GeoStep(ctr, az1, delta + len), ln);
    GeoLnAddPt(GeoStep(ctr, az0, delta + len), ln);
    GeoLnAddPt(GeoStep(ctr, az0, delta), ln);
    return ln;
}

/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_VolBinOutlineW --
 *
 *	This function computes the corners of a bin.
 *	It behaves like Sigmet_VolBinOutline, except beam width is given
 *	by the caller instead of the Sigmet volume.
 *
 * Results:
 *	A geoline (see geoLn (3)) with four points corresponding to the
 *	corners of a bin.
 *
 * Side effects:
 *	A geoline object is created.  The user should eventually destroy it
 *	with a call to GeoLnDestroy.
 *
 *-------------------------------------------------------------------------
 */

GeoLn
Sigmet_VolBinOutlineW(sigPtr, s, y, r, width, b)
    struct Sigmet_Vol *sigPtr;	/* Raw volume */
    unsigned s;			/* Sweep */
    unsigned y;			/* Type */
    unsigned r;			/* Ray */
    double width;		/* Beam width */
    unsigned b;			/* Bin */
{
    GeoLn ln;			/* Result */
    GeoPt ctr;			/* Radar location.  */
    float R;			/* Distance from center of Earth to antenna */
    double az0, az1;		/* Ray azimuth at start and end of ray */
    double az;			/* Ray azimuth */
    double len;			/* Length of bin over ground */
    double tilt;		/* Ray tilt */
    double cos_tilt;		/* Cosine of sweep tilt (used several times) */
    double delta;		/* Distance to start of bin */
    double bin0, step;		/* Distance to first bin, bin size */
    double d;			/* Distance to bin */
    int n;

    if ( !sigPtr || s >= sigPtr->num_sweeps || y >= sigPtr->num_types
	    || r >= sigPtr->rays_in_sweep || b >= sigPtr->num_output_bins) {
	return NULL;
    }
    if ( !(ln = GeoLnCreate(4)) ) {
	return NULL;
    }
    ctr.lat = sigPtr->latitude;
    ctr.lon = sigPtr->longitude;
    R = REarth() + sigPtr->ground_elevation + sigPtr->tower_height;
    tilt = 0.5 * (sigPtr->ray_tilt0[s][r] + sigPtr->ray_tilt1[s][r]);
    cos_tilt = cos(tilt);
    az0 = sigPtr->ray_az0[s][r];
    az1 = DomainLon(sigPtr->ray_az1[s][r], az0);
    az = 0.5 * (az0 + az1);
    az0 = az - 0.5 * width;
    az1 = az + 0.5 * width;
    bin0 = 0.01 * sigPtr->range_1st_bin_cm;
    step = 0.01 * sigPtr->output_bin_step;
    d = bin0 + b * step;
    delta = asin(d * cos_tilt / sqrt(R * R + d * d + 2 * R * d * sin(tilt)));
    len = step * cos_tilt / REarth();
    GeoLnAddPt(GeoStep(ctr, az1, delta), ln);
    GeoLnAddPt(GeoStep(ctr, az1, delta + len), ln);
    GeoLnAddPt(GeoStep(ctr, az0, delta + len), ln);
    GeoLnAddPt(GeoStep(ctr, az0, delta), ln);
    return ln;
}

