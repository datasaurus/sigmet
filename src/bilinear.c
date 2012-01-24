int bilin_wt(struct Sigmet_Vol *, double, double, double, long,
	double, double, double, long, double, double, double, long,
	struct polar_coord ****);
static struct polar_coord ***alloc3_pc(long, long, long);
static void free3_pc(struct polar_coord ***);

/*
   This function compute information necessary for bilinear interpolation
   of a volume field to a Cartesian grid.

   The Cartesian grid is defined as follows.
    x0, dx, nx -
	x measures distance east from the radar in meters. The field will be
	interpolated to nx points with x coordinates ranging from x0 to
	x0 + (nx - 1) * dx, in increments of dx.
    y0, dy, y -
    	y measures distance north from the radar in meters.  The field will be
	interpolated to ny points with y coordinates ranging from y0 to
	y0 + (ny - 1) * dy, in increments of dy.
    double z0, dz, z -
    	z measures distance above ground in meters.  The field will be
	interpolated to nz points with z coordinates ranging from z0 to
	z0 + (nz - 1) * dz, in increments of dz.

    If all goes well, polar_coords will receive an array of polar coordinate
    structures, as declared above. MEMORY ASSOCIATED WITH THIS ARRAY
    MUST BE EVENTUALLY FREED WITH A CALL TO free3_pc. Return value will
    be SIGMET_OK.

    If something goes wrong, return value is an error code. See sigmet_vol (3).
    Error information can be retrieved with a call to Err_Get();
 */

int bilin_wt(struct Sigmet_Vol *vol_p,
	double z0, double dz, double z, long nz,
	double y0, double dy, double y, long ny,
	double x0, double dx, double x, long nx,
	struct polar_coord ****polar_coords)
{
    int i, j, k;
    double re;				/* Earth radius, meters */
    struct polar_coord ***p_c;		/* Return value */

    re = GeogREarth(NULL);

    if ( !(p_c = alloc3_pc(nz, ny, nx)) ) {
	Err_Append("Could not allocate array of polar coordinates. ");
	return SIGMET_ALLOC_FAIL;
    }

    for (k = 0, z = z0; k < nz; k++, z += dz) {
	for (j = 0, y = y0; j < ny; j++, y += dy) {
	    for (i = 0, x = x0; i < nx; i++, x += dx) {
		double lon1, lat1, lon2, lat2;
		double delta;		/* Distance along ground from origin
					   to (x, y, 0) */
		double ch, cu;		/* Computational */
		double az, tilt, rng;	/* Azimuth, tilt, and range from
					   origin to point (x, y, z) */
		double east = M_PI / 2.0, north = 0.0;

		GeogStep(0.0, 0.0, east, x / re, &lon1, &lat1);
		GeogStep(lon1, lat1, north, y / re, &lon2, &lat2);
		az = GeogAz(0.0, 0.0, lon2, lat2);
		delta = GeogDist(0.0, 0.0, lon2, lat2);
		ch = (re + z) * sin(delta);
		cu = (re + z) * cos(delta) - re;
		tilt = atan(cu / ch);
		rng = sqrt(cu * cu + ch * ch);
		p_c[k][j][i].rng = rng;
		p_c[k][j][i].az = az;
		p_c[k][j][i].tilt = tilt;
	    }
	}
    }
    *polar_coords = p_c;

    return SIGMET_OK;
}

/*
   Allocate array of polar coordinates.
 */

static struct polar_coord *** alloc3_pc(long kmax, long jmax, long imax)
{
    struct polar_coord ***dat = NULL;
    long k, j;
    size_t kk, jj, ii;

    /* Make sure casting to size_t does not overflow anything.  */
    if (kmax <= 0 || jmax <= 0 || imax <= 0) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    kk = (size_t)kmax;
    jj = (size_t)jmax;
    ii = (size_t)imax;
    if ((kk * jj) / kk != jj || (kk * jj * ii) / (kk * jj) != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (struct polar_coord ***)CALLOC(kk + 2,
	    sizeof(struct polar_coord **));
    if ( !dat ) {
	Err_Append("Could not allocate 2nd dimension.\n");
	return NULL;
    }
    dat[0] = (struct polar_coord **)CALLOC(kk * jj + 1,
	    sizeof(struct polar_coord *));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate 1st dimension.\n");
	return NULL;
    }
    dat[0][0] = (struct polar_coord *)CALLOC(kk * jj * ii,
	    sizeof(struct polar_coord));
    if ( !dat[0][0] ) {
	FREE(dat[0]);
	FREE(dat);
	Err_Append("Could not allocate array of values.\n");
	return NULL;
    }
    for (k = 1; k <= kmax; k++) {
	dat[k] = dat[k - 1] + jmax;
    }
    for (j = 1; j <= kmax * jmax; j++) {
	dat[0][j] = dat[0][j - 1] + imax;
    }
    return dat;
}

/*
   Free an array created by alloc3_pc
 */

static void free3_pc(struct polar_coord ***pc)
{
    if (pc) {
	if (pc[0]) {
	    if (pc[0][0]) {
		FREE(pc[0][0]);
	    }
	    FREE(pc[0]);
	}
	FREE(pc);
    }
}
