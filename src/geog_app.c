/*
   -	geog_app.c --
   -		This file defines an application that does geography
   -		calculations.
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
   .	$Revision: 1.56 $ $Date: 2014/10/08 09:14:03 $
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include "alloc.h"
#include "geog_lib.h"
#include "geog_proj.h"

/* Application name and subcommand name */
char *argv0, *argv1;

/* Default array size */
#define LEN 1024

/* Number of subcommands */
#define NCMD 15

/* Callback functions.  There should be one for each subcommand. */
typedef int (callback)(int , char **);
callback version_cb;
callback dms_cb;
callback rearth_cb;
callback lonr_cb;
callback latn_cb;
callback dist_cb;
callback sum_dist_cb;
callback az_cb;
callback step_cb;
callback beam_ht_cb;
callback contain_pt_cb;
callback contain_pts_cb;
callback vproj_cb;
callback lonlat_to_xy_cb;
callback xy_to_lonlat_cb;

int main(int argc, char *argv[])
{
    int i;		/* Index for subcommand in argv[1] */
    int rslt;		/* Return code */

    /* Arrays of subcommand names and associated callbacks */
    char *argv1v[NCMD] = {"-v", "dms", "rearth", "lonr", "latn", "dist",
	"sum_dist", "az", "step", "beam_ht", "contain_pt", "contain_pts",
	"vproj", "lonlat_to_xy", "xy_to_lonlat"};
    callback *cb1v[NCMD] = {version_cb, dms_cb, rearth_cb, lonr_cb, latn_cb,
	dist_cb, sum_dist_cb, az_cb, step_cb, beam_ht_cb, contain_pt_cb,
	contain_pts_cb, vproj_cb, lonlat_to_xy_cb, xy_to_lonlat_cb};

    argv0 = argv[0];
    if (argc < 2) {
	fprintf(stderr, "Usage: %s subcommand [subcommand_options ...]\n",
		argv0);
	exit(1);
    }
    argv1 = argv[1];

    /* Search argv1v for argv1.  When match is found, evaluate the associated
     * callback from cb1v. */
    for (i = 0; i < NCMD; i++) {
	if (strcmp(argv1v[i], argv1) == 0) {
	    rslt = (cb1v[i])(argc, argv);
	    if ( !rslt ) {
		fprintf(stderr, "%s %s failed.\n", argv0, argv1);
		break;
	    } else {
		break;
	    }
	}
    }
    if (i == NCMD) {
	fprintf(stderr, "%s: No option or subcommand named %s\n", argv0, argv1);
	fprintf(stderr, "Subcommand must be one of: ");
	for (i = 0; i < NCMD; i++) {
	    fprintf(stderr, "%s ", argv1v[i]);
	}
	fprintf(stderr, "\n");
	rslt = 0;
    }
    return !rslt;
}

int version_cb(int argc, char *argv[])
{
    printf("%s %s\n", argv0, GEOG_VERSION);
    return 1;
}

int dms_cb(int argc, char *argv[])
{
    double d;				/* Degrees */
    char *d_s;				/* String representation of d */
    double deg, min, sec;		/* d broken into degrees, minutes,
					   seconds */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s degrees\n", argv0, argv1);
	return 0;
    }
    d_s = argv[2];
    if (sscanf(d_s, "%lf", &d) != 1) {
	fprintf(stderr, "Expected float value for degrees, got %s\n", d_s);
	return 0;
    }
    GeogDMS(d, &deg, &min, &sec, "%f");
    printf("%.0lf %.0lf %lf\n", deg, min, sec);
    return 1;
}

int rearth_cb(int argc, char *argv[])
{
    if (argc == 2) {
	printf("%f\n", GeogREarth(NULL));
    } else {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    return 1;
}

int lonr_cb(int argc, char *argv[])
{
    char *l_s, *r_s;		/* Strings from command line */
    double l, r;		/* Values from command line */

    if (argc != 4) {
	fprintf(stderr, "Usage: %s %s lon reflon\n", argv0, argv1);
	return 0;
    }
    l_s = argv[2];
    if (sscanf(l_s, "%lf", &l) != 1) {
	fprintf(stderr, "Expected float value for longitude, got %s.\n", l_s);
	return 0;
    }
    r_s = argv[3];
    if (sscanf(r_s, "%lf", &r) != 1) {
	fprintf(stderr, "Expected float value for reference longitude, got "
		"%s.\n", r_s);
	return 0;
    }
    printf("%lf\n", GeogLonR(l * RAD_DEG, r * RAD_DEG) * DEG_RAD);
    return 1;
}

int latn_cb(int argc, char *argv[])
{
    char *l_s;			/* String from command line */
    double l;			/* Latitude value from command line */

    if (argc != 3) {
	fprintf(stderr, "Usage: %s %s lat\n", argv0, argv1);
	return 0;
    }
    l_s = argv[2];
    if (sscanf(l_s, "%lf", &l) != 1) {
	fprintf(stderr, "Expected float value for latitude, got %s\n", l_s);
	return 0;
    }
    printf("%f\n", GeogLatN(l * RAD_DEG) * DEG_RAD);
    return 1;
}

int dist_cb(int argc, char *argv[])
{
    char *lon1_s, *lat1_s, *lon2_s, *lat2_s;
    double lon1, lat1, lon2, lat2;

    if (argc != 6) {
	fprintf(stderr, "Usage: %s %s lon1 lat1 lon2 lat2\n", argv0, argv1);
	return 0;
    }
    lon1_s = argv[2];
    lat1_s = argv[3];
    lon2_s = argv[4];
    lat2_s = argv[5];

    /* Get coordinates from command line arguments */
    if (sscanf(lon1_s, "%lf", &lon1) != 1) {
	fprintf(stderr, "Expected float value for lon1, got %s\n", lon1_s);
	return 0;
    }
    if (sscanf(lat1_s, "%lf", &lat1) != 1) {
	fprintf(stderr, "Expected float value for lat1, got %s\n", lat1_s);
	return 0;
    }
    if (sscanf(lon2_s, "%lf", &lon2) != 1) {
	fprintf(stderr, "Expected float value for lon2, got %s\n", lon2_s);
	return 0;
    }
    if (sscanf(lat2_s, "%lf", &lat2) != 1) {
	fprintf(stderr, "Expected float value for lat2, got %s\n", lat2_s);
	return 0;
    }
    printf("%f\n", GeogDist(lon1 * RAD_DEG, lat1 * RAD_DEG,
		lon2 * RAD_DEG, lat2 * RAD_DEG) * DEG_RAD);
    return 1;
}

/*
   Compute length of track given as lon lat pairs in stdin.
   Input and output are in degrees.
 */

int sum_dist_cb(int argc, char *argv[])
{
    double lon0, lon, lat0, lat;	/* Longitude, latitude from input */
    double tot;				/* Total distance */

    if (argc != 2) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if (scanf(" %lf %lf", &lon0, &lat0) != 2) {
	fprintf(stderr, "No input.\n");
	return 0;
    }
    for (tot = 0.0; scanf(" %lf %lf", &lon, &lat) == 2 ; ) {
	tot += GeogDist(lon0 * RAD_DEG, lat0 * RAD_DEG,
		lon * RAD_DEG, lat * RAD_DEG);
	lat0 = lat;
	lon0 = lon;
    }
    printf("%lf\n", tot * DEG_RAD);
    return 1;
}

int az_cb(int argc, char *argv[])
{
    char *lon1_s, *lat1_s, *lon2_s, *lat2_s;
    double lon1, lat1, lon2, lat2;

    if (argc != 6) {
	fprintf(stderr, "Usage: %s %s lon1 lat1 lon2 lat2\n", argv0, argv1);
	return 0;
    }
    lon1_s = argv[2];
    lat1_s = argv[3];
    lon2_s = argv[4];
    lat2_s = argv[5];

    /* Get coordinates from command line arguments */
    if (sscanf(lon1_s, "%lf", &lon1) != 1) {
	fprintf(stderr, "Expected float value for lon1, got %s\n", lon1_s);
	return 0;
    }
    if (sscanf(lat1_s, "%lf", &lat1) != 1) {
	fprintf(stderr, "Expected float value for lat1, got %s\n", lat1_s);
	return 0;
    }
    if (sscanf(lon2_s, "%lf", &lon2) != 1) {
	fprintf(stderr, "Expected float value for lon2, got %s\n", lon2_s);
	return 0;
    }
    if (sscanf(lat2_s, "%lf", &lat2) != 1) {
	fprintf(stderr, "Expected float value for lat2, got %s\n", lat2_s);
	return 0;
    }
    printf("%f\n", GeogAz(lon1 * RAD_DEG, lat1 * RAD_DEG,
		lon2 * RAD_DEG,  lat2 * RAD_DEG) * DEG_RAD);
    return 1;
}

int step_cb(int argc, char *argv[])
{
    double lon1, lat1, dirn, dist, lon2, lat2;

    if (argc == 2) {
	while (scanf("%lf %lf %lf %lf", &lon1, &lat1, &dirn, &dist) == 4) {
	    GeogStep(lon1 * RAD_DEG, lat1 * RAD_DEG,
		    dirn * RAD_DEG, dist * RAD_DEG, &lon2, &lat2);
	    printf("%f %f\n", lon2 * DEG_RAD, lat2 * DEG_RAD);
	}
    } else if (argc == 6) {
	char *lon1_s, *lat1_s, *dirn_s, *dist_s;

	lon1_s = argv[2];
	if (sscanf(lon1_s, "%lf", &lon1) != 1) {
	    fprintf(stderr, "Expected float value for lon1, got %s\n", lon1_s);
	    return 0;
	}
	lat1_s = argv[3];
	if (sscanf(lat1_s, "%lf", &lat1) != 1) {
	    fprintf(stderr, "Expected float value for lat1, got %s\n", lat1_s);
	    return 0;
	}
	dirn_s = argv[4];
	if (sscanf(dirn_s, "%lf", &dirn) != 1) {
	    fprintf(stderr, "Expected float value for azimuth, got %s\n",
		    dirn_s);
	    return 0;
	}
	dist_s = argv[5];
	if (sscanf(dist_s, "%lf", &dist) != 1) {
	    fprintf(stderr, "Expected float value for range, got %s\n",
		    dist_s);
	    return 0;
	}
	GeogStep(lon1 * RAD_DEG, lat1 * RAD_DEG,
		dirn * RAD_DEG, dist * RAD_DEG, &lon2, &lat2);
	printf("%f %f\n", lon2 * DEG_RAD, lat2 * DEG_RAD);
    } else {
	fprintf(stderr, "Usage: %s %s [lon lat direction distance]\n",
		argv0, argv1);
	return 0;
    }
    return 1;
}

int beam_ht_cb(int argc, char *argv[])
{
    char *dist_s, *tilt_s, *a0_s;
    double d, tilt, a0;

    if (argc != 5) {
	fprintf(stderr, "Usage: %s %s distance tilt earth_radius\n",
		argv0, argv1);
	return 0;
    }
    dist_s = argv[2];
    tilt_s = argv[3];
    a0_s = argv[4];
    if (sscanf(dist_s, "%lf", &d) != 1) {
	fprintf(stderr, "Expected float value for distance, got %s\n", dist_s);
	return 0;
    }
    if (sscanf(tilt_s, "%lf", &tilt) != 1) {
	fprintf(stderr, "Expected float value for tilt, got %s\n", tilt_s);
	return 0;
    }
    if (sscanf(a0_s, "%lf", &a0) != 1) {
	fprintf(stderr, "Expected float value for Earth radius, got %s\n",
		a0_s);
	return 0;
    }
    printf("%lf\n", GeogBeamHt(d, tilt * RAD_DEG, a0));
    return 1;
}

int contain_pt_cb(int argc, char *argv[])
{
    char *lon_s, *lat_s;
    char **lon_sp, **lat_sp;
    struct GeogPt pt, *pts, *pts_p;
    size_t n_pts;

    if ( argc < 10 || argc % 2 != 0 ) {
	fprintf(stderr, "Usage: %s %s lon lat lon1 lat1 lon2 lat2 ...\n",
		argv0, argv1);
	return 0;
    }
    lon_s = argv[2];
    lat_s = argv[3];
    if ( sscanf(lon_s, "%lf", &pt.lon) != 1 ) {
	fprintf(stderr, "Expected float value for longitude, got %s\n", lon_s);
	return 0;
    }
    pt.lon *= RAD_DEG;
    if ( sscanf(lat_s, "%lf", &pt.lat) != 1 ) {
	fprintf(stderr, "Expected float value for latitude, got %s\n", lat_s);
	return 0;
    }
    pt.lat *= RAD_DEG;
    n_pts = (argc - 4) / 2;
    if ( !(pts = CALLOC(n_pts, sizeof(struct GeogPt))) ) {
	fprintf(stderr, "Could not allocate memory for polygon\n");
	return 0;
    }
    for (lon_sp = argv + 4, lat_sp = argv + 5, pts_p = pts;
	    lat_sp < argv + argc; lon_sp += 2, lat_sp += 2, pts_p++) {
	if ( sscanf(*lon_sp, "%lf", &pts_p->lon) != 1 ) {
	    fprintf(stderr, "Expected float value for longitude, got %s\n",
		    *lon_sp);
	    return 0;
	}
	pts_p->lon *= RAD_DEG;
	if ( sscanf(*lat_sp, "%lf", &pts_p->lat) != 1 ) {
	    fprintf(stderr, "Expected float value for latitude, got %s\n",
		    *lat_sp);
	    return 0;
	}
	pts_p->lat *= RAD_DEG;
    }
    printf("%s\n", GeogContainPt(pt, pts, n_pts) ? "in" : "out");
    return 1;
}

int contain_pts_cb(int argc, char *argv[])
{
    char **lon_sp, **lat_sp;
    struct GeogPt pt, *pts, *pts_p;
    size_t n_pts;
    char buf[LEN];

    if ( argc < 8 || argc % 2 != 0 ) {
	fprintf(stderr, "Usage: %s %s lon1 lat1 lon2 lat2 ...\n", argv0, argv1);
	return 0;
    }
    n_pts = (argc - 2) / 2;
    if ( !(pts = CALLOC(n_pts, sizeof(struct GeogPt))) ) {
	fprintf(stderr, "Could not allocate memory for polygon.\n");
	return 0;
    }
    for (lon_sp = argv + 2, lat_sp = argv + 3, pts_p = pts;
	    lat_sp < argv + argc; lon_sp += 2, lat_sp += 2, pts_p++) {
	if ( sscanf(*lon_sp, "%lf", &pts_p->lon) != 1 ) {
	    fprintf(stderr, "Expected float value for longitude, got %s\n",
		    *lon_sp);
	    return 0;
	}
	pts_p->lon *= RAD_DEG;
	if ( sscanf(*lat_sp, "%lf", &pts_p->lat) != 1 ) {
	    fprintf(stderr, "Expected float value for latitude, got %s\n",
		    *lat_sp);
	    return 0;
	}
	pts_p->lat *= RAD_DEG;
    }
    while ( fgets(buf, LEN, stdin) ) {
	if ( sscanf(buf, " %lf %lf ", &pt.lon , &pt.lat) == 2 ) {
	    pt.lon *= RAD_DEG;
	    pt.lat *= RAD_DEG;
	    if ( GeogContainPt(pt, pts, n_pts) ) {
		fputs(buf, stdout);
	    }
	}
    }
    return 1;
}

int vproj_cb(int argc, char *argv[])
{
    char *rlon_s, *rlat_s, *azg_s, *a0_s;
    double rlon, rlat;
    double azg;			/* Azimuth of proj plane from (rlon rlat) */
    double lon, lat;
    double az;
    double a0;			/* Earth radius */
    double d;			/* Distance along ground from (rlat rlon) to
				   an input point */
    double x, y, z;

    if ( argc == 6 ) {
	rlon_s = argv[2];
	rlat_s = argv[3];
	azg_s = argv[4];
	a0_s = argv[5];
    } else {
	fprintf(stderr, "Usage: %s %s lon lat earth_radius\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(rlat_s, "%lf", &rlat) != 1 ) {
	fprintf(stderr, "Expected float value for latitude of reference point,"
		" got %s\n", rlat_s);
	return 0;
    }
    if ( sscanf(rlon_s, "%lf", &rlon) != 1 ) {
	fprintf(stderr, "Expected float value for longitude of reference point,"
		" got %s\n", rlon_s);
	return 0;
    }
    if ( sscanf(azg_s, "%lf", &azg) != 1 ) {
	fprintf(stderr, "Expected float value for azimuth, got %s\n", azg_s);
	return 0;
    }
    if ( sscanf(a0_s, "%lf", &a0) != 1 ) {
	fprintf(stderr, "Expected float value for earth radius, got %s\n",
		a0_s);
	return 0;
    }
    rlat *= RAD_DEG;
    rlon *= RAD_DEG;
    azg *= RAD_DEG;
    while (scanf(" %lf %lf %lf", &lon, &lat, &z) == 3) {
	lon *= RAD_DEG;
	lat *= RAD_DEG;
	d = a0 * GeogDist(rlon, rlat, lon, lat);
	az = GeogAz(rlon, rlat, lon, lat) - azg;
	x = d * cos(az);
	y = -d * sin(az);		/* Right handed Cartesian axes */
	printf("%.1lf %.1lf %.1lf\n", x, y, z);
    }
    return 1;
}

/*
   Read projection specifier on command line. Read longitude latitude pairs
   from standard input. Write corresponding x y values to standard output.
 */

int lonlat_to_xy_cb(int argc, char *argv[])
{
    char **arg;				/* Argument from command line */
    size_t len;				/* Length of command line */
    char *ln;				/* Projection specifier string */
    struct GeogProj proj;		/* Projection */
    double lon, lat;			/* Input geographic coordinates */
    double x, y;			/* Output map coordinates */
    char *l, *a;			/* Point into ln, arg */

    if ( argc < 3 ) {
	fprintf(stderr, "Usage: %s %s projection\n", argv0, argv1);
	return 0;
    }
    for (arg = argv + 2, len = 0; *arg; arg++) {
	len += strlen(*arg) + 1;
    }
    if ( !(ln = CALLOC(len + 1, 1)) ) {
	fprintf(stderr, "%s %s: failed to allocate internal projection "
		"specifier.\n", argv0, argv1);
	return 0;
    }
    for (l = ln, arg = argv + 2; *arg; arg++) {
	for (a = *arg; *a; a++, l++) {
	    *l = *a;
	}
	*l++ = ' ';
    }
    if ( !GeogProjSetFmStr(ln, &proj) ) {
	fprintf(stderr, "%s %s: failed to set projection %s\n",
		argv0, argv1, ln);
	return 0;
    }
    FREE(ln);
    while ( scanf(" %lf %lf", &lon, &lat) == 2 ) {
	lon *= RAD_DEG;
	lat *= RAD_DEG;
	if ( GeogProjLonLatToXY(lon, lat, &x, &y, &proj) ) {
	    printf("%lf %lf ", x, y);
	} else {
	    printf("**** **** ");
	}
	printf("\n");
    }
    return 1;
}

/*
   Read projection specifier on command line. Read latitude longitude pairs
   from standard input. Write corresponding x y values to standard output.
 */

int xy_to_lonlat_cb(int argc, char *argv[])
{
    char **arg;				/* Argument from command line */
    size_t len;				/* Length of command line */
    char *ln;				/* Projection specifier string */
    struct GeogProj proj;		/* Projection */
    double x, y;			/* Input map coordinates */
    double lon, lat;			/* Output geographic coordinates */
    char *l, *a;			/* Point into ln, arg */

    if ( argc < 3 ) {
	fprintf(stderr, "Usage: %s %s projection\n", argv0, argv1);
	return 0;
    }
    for (arg = argv + 2, len = 0; *arg; arg++) {
	len += strlen(*arg) + 1;
    }
    if ( !(ln = CALLOC(len + 1, 1)) ) {
	fprintf(stderr, "%s %s: failed to allocate internal projection "
		"specifier.\n", argv0, argv1);
	return 0;
    }
    for (l = ln, arg = argv + 2; *arg; arg++) {
	for (a = *arg; *a; a++, l++) {
	    *l = *a;
	}
	*l++ = ' ';
    }
    if ( !GeogProjSetFmStr(ln, &proj) ) {
	fprintf(stderr, "%s %s: failed to set projection %s\n",
		argv0, argv1, ln);
	return 0;
    }
    FREE(ln);
    while ( scanf(" %lf %lf", &x, &y) == 2 ) {
	if ( GeogProjXYToLonLat(x, y, &lon, &lat, &proj) ) {
	    printf("%lf %lf ", lon * DEG_RAD, lat * DEG_RAD);
	} else {
	    printf("**** **** ");
	}
	printf("\n");
    }
    return 1;
}
