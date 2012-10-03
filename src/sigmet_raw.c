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
   .	$Revision: 1.90 $ $Date: 2012/09/26 23:19:23 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include "alloc.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/*
   Local functions
 */

static int handle_signals(void);
static void handler(int signum);

/*
   Callbacks for the subcommands.
 */

typedef int (SigmetRaw_Callback)(int , char **);
SigmetRaw_Callback version_cb;
SigmetRaw_Callback pid_cb;
SigmetRaw_Callback load_cb;
SigmetRaw_Callback data_types_cb;
SigmetRaw_Callback volume_headers_cb;
SigmetRaw_Callback vol_hdr_cb;
SigmetRaw_Callback near_sweep_cb;
SigmetRaw_Callback sweep_headers_cb;
SigmetRaw_Callback ray_headers_cb;
SigmetRaw_Callback new_field_cb;
SigmetRaw_Callback del_field_cb;
SigmetRaw_Callback size_cb;
SigmetRaw_Callback set_field_cb;
SigmetRaw_Callback add_cb;
SigmetRaw_Callback sub_cb;
SigmetRaw_Callback mul_cb;
SigmetRaw_Callback div_cb;
SigmetRaw_Callback log10_cb;
SigmetRaw_Callback incr_time_cb;
SigmetRaw_Callback data_cb;
SigmetRaw_Callback bdata_cb;
SigmetRaw_Callback bin_outline_cb;
SigmetRaw_Callback radar_lon_cb;
SigmetRaw_Callback radar_lat_cb;
SigmetRaw_Callback shift_az_cb;
SigmetRaw_Callback outlines_cb;
SigmetRaw_Callback dorade_cb;

/*
   Subcommand names and associated callbacks. The hash function defined
   below returns the index from cmd1v or cb1v corresponding to string argv1.

   Array should be sized for perfect hashing. Parser does not search buckets.

   Hashing function from Kernighan, Brian W. and Rob Pike, The Practice of
   Programming, Reading, Massachusetts. 1999
 */

#define N_HASH_CMD 133
static char *cmd1v[N_HASH_CMD] = {
    "", "log10", "size", "", "radar_lon", "",
    "", "bdata", "", "", "", "",
    "", "", "", "", "", "",
    "", "", "", "", "del_field", "",
    "", "", "", "", "", "",
    "", "", "", "volume_headers", "bin_outline", "",
    "set_field", "", "", "", "", "",
    "", "", "near_sweep", "", "", "version",
    "", "", "", "", "", "",
    "", "", "", "", "", "",
    "", "", "", "", "", "pid",
    "outlines", "", "", "ray_headers", "", "",
    "", "incr_time", "data_types", "", "", "",
    "load", "", "", "", "", "",
    "", "", "", "", "dorade", "mul",
    "", "", "", "", "", "",
    "", "shift_az", "", "", "", "",
    "sweep_headers", "", "", "", "", "",
    "radar_lat", "new_field", "", "", "", "",
    "", "", "", "", "vol_hdr", "data",
    "", "", "div", "", "", "add",
    "sub", "", "", "", "", "", ""
};
static SigmetRaw_Callback *cb1v[N_HASH_CMD] = {
    NULL, log10_cb, size_cb, NULL, radar_lon_cb, NULL,
    NULL, bdata_cb, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, del_field_cb, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, volume_headers_cb, bin_outline_cb, NULL,
    set_field_cb, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, near_sweep_cb, NULL, NULL, version_cb,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, pid_cb,
    outlines_cb, NULL, NULL, ray_headers_cb, NULL, NULL,
    NULL, incr_time_cb, data_types_cb, NULL, NULL, NULL,
    load_cb, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, dorade_cb, mul_cb,
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, shift_az_cb, NULL, NULL, NULL, NULL,
    sweep_headers_cb, NULL, NULL, NULL, NULL, NULL,
    radar_lat_cb, new_field_cb, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, vol_hdr_cb, data_cb,
    NULL, NULL, div_cb, NULL, NULL, add_cb,
    sub_cb, NULL, NULL, NULL, NULL, NULL, NULL
};
#define HASH_X 31
static int hash(const char *);
static int hash(const char *argv1)
{
    unsigned h;

    for (h = 0 ; *argv1 != '\0'; argv1++) {
	h = HASH_X * h + (unsigned)*argv1;
    }
    return h % N_HASH_CMD;
}

/*
   See sigmet_raw (1)
 */

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1;
    int n;
    int status;

    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.",
		argv0, getpid());
	return EXIT_FAILURE;
    }
    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", argv0);
	exit(EXIT_FAILURE);
    }
    argv1 = argv[1];
    n = hash(argv1);
    if ( strcmp(argv1, cmd1v[n]) == 0 ) {
	status = ((cb1v[n])(argc, argv) == SIGMET_OK)
	    ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
	fprintf(stderr, "%s: unknown subcommand %s. Subcommand must be one of ",
		argv0, argv1);
	for (n = 0; n < N_HASH_CMD; n++) {
	    if ( strlen(cmd1v[n]) > 0 ) {
		fprintf(stderr, " %s", cmd1v[n]);
	    }
	}
	fprintf(stderr, "\n");
	status = EXIT_FAILURE;
    }
    return status;
}

/*
   Default static string length
 */

#define LEN 255

int version_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
	    "All rights reserved.\n", argv[0], SIGMET_VERSION);
    return EXIT_SUCCESS;
}

int pid_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("%d\n", getpid());
    return SIGMET_OK;
}

int load_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char **arg;
    int status;
    struct Sigmet_Vol *vol_p = NULL;	/* Volume to load */
    int shmid = -1;			/* Shared memory identifier */
    char shmid_s[LEN];			/* shmid as string */
    char *vol_fl_nm;
    FILE *vol_fl;
    pid_t pid;

    if ( argc < 4 ) {
	fprintf(stderr, "Usage: %s %s sigmet_volume command [args ...]\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    vol_fl_nm = argv[2];
    shmid = shmget(IPC_PRIVATE, sizeof(struct Sigmet_Vol), S_IRUSR | S_IWUSR);
    if ( shmid == -1 ) {
	fprintf(stderr, "%s %s: could not allocate volume in shared memory.\n"
		"%s\n", argv0, argv1, strerror(errno));
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p = shmat(shmid, NULL, 0);
    if ( vol_p == (void *)-1 ) {
	fprintf(stderr, "%s %s: could not attach to shared memory for "
		"volume.\n%s\n", argv0, argv1, strerror(errno));
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    Sigmet_Vol_Init(vol_p);
    vol_p->shm = 1;
    if ( !(vol_fl = fopen(vol_fl_nm, "r")) ) {
	fprintf(stderr, "%s %s: could not open %s for reading.\n%s\n",
		argv0, argv1, vol_fl_nm, strerror(errno));
	goto error;
    }
    if ( (status = Sigmet_Vol_Read(vol_fl, vol_p)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not read volume.\n", argv0, argv1);
	goto error;
    }
    if ( snprintf(shmid_s, LEN, "SIGMET_VOL_SHMEM=%d", shmid) > LEN ) {
	fprintf(stderr, "%s %s: could not create environment variable for "
		"volume shared memory identifier.\n", argv0, argv1);
	goto error;
    }
    if ( putenv(shmid_s) != 0 ) {
	fprintf(stderr, "%s %s: could not put shared memory identifier for "
		"volume into environment.\n%s\n",
		argv0, argv1, strerror(errno));
	goto error;
    }
    pid = fork();
    if ( pid == -1 ) {
	fprintf(stderr, "%s %s: could not fork\n%s\n",
		argv0, argv1, strerror(errno));
	goto error;
    } else if ( pid == 0 ) {
	execvp(argv[3], argv + 3);
	fprintf(stderr, "%s %s: failed to execute child process.\n%s\n",
		argv0, argv1, strerror(errno));
    }
    waitpid(pid, &status, 0);
    if ( WIFEXITED(status) ) {
	for (arg = argv + 3; *arg; arg++) {
	    printf("%s ", *arg);
	}
	printf("exited with status %d\n", WEXITSTATUS(status));
	status = (WEXITSTATUS(status) == EXIT_SUCCESS)
	    ? SIGMET_OK : SIGMET_HELPER_FAIL;
    } else if ( WIFSIGNALED(status) ) {
	fprintf(stderr, "%s: child process exited on signal %d\n",
		argv0, WTERMSIG(status));
	status = SIGMET_HELPER_FAIL;
    }
    printf("sigmet_raw exiting.\n");
    if ( !Sigmet_Vol_Free(vol_p) ) {
	fprintf(stderr, "%s %s: could not free memory for volume.\n",
		argv0, argv1);
    }
    if ( shmdt(vol_p) == -1 ) {
	fprintf(stderr, "%s %s: could not detach shared memory for "
		"volume.\n%s\n", argv0, argv1, strerror(errno));
    }
    if ( shmctl(shmid, IPC_RMID, NULL) == -1 ) {
	fprintf(stderr, "%s %s: could not remove shared memory for "
		"volume.\n%s\nPlease use ipcrm command for id %d\n",
		argv0, argv1, strerror(errno), shmid);
    }
    return status;

error:
    if ( vol_p && vol_p != (void *)-1 ) {
	if ( !Sigmet_Vol_Free(vol_p) ) {
	    fprintf(stderr, "%s %s: could not free memory for volume.\n",
		    argv0, argv1);
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "%s %s: could not detach shared memory for "
		    "volume.\n%s\n", argv0, argv1, strerror(errno));
	}
    }
    if ( shmid != -1 && shmctl(shmid, IPC_RMID, NULL) == -1 ) {
	fprintf(stderr, "%s %s: could not remove shared memory for "
		"volume.\n%s\nPlease use ipcrm command for id %d\n",
		argv0, argv1, strerror(errno), shmid);
    }
    return status;
}

int data_types_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int y;

    for (y = 0; y < vol_p->num_types; y++) {
	if ( strlen(vol_p->dat[y].abbrv) > 0 ) {
	    printf("%s | %s | %s\n", vol_p->dat[y].abbrv,
		    vol_p->dat[y].descr, vol_p->dat[y].unit);
	}
    }
    return SIGMET_OK;
}

int volume_headers_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    Sigmet_Vol_PrintHdr(stdout, vol_p);
    return SIGMET_OK;
}

int vol_hdr_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("site_name=\"%s\"\n", vol_p->ih.ic.su_site_name);
    printf("radar_lon=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD);
    printf("radar_lat=%.4lf\n",
	    GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.latitude), 0.0) * DEG_PER_RAD);
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	    printf("scan_mode=\"ppi sector\"\n");
	    break;
	case RHI:
	    printf("scan_mode=rhi\n");
	    break;
	case MAN_SCAN:
	    printf("scan_mode=manual\n");
	    break;
	case PPI_C:
	    printf("scan_mode=\"ppi continuous\"\n");
	    break;
	case FILE_SCAN:
	    printf("scan_mode=file\n");
	    break;
    }
    printf("task_name=\"%s\"\n", vol_p->ph.pc.task_name);
    printf("types=\"");
    printf("%s", vol_p->dat[0].abbrv);
    for (y = 1; y < vol_p->num_types; y++) {
	printf(" %s", vol_p->dat[y].abbrv);
    }
    printf("\"\n");
    printf("num_sweeps=%d\n", vol_p->ih.ic.num_sweeps);
    printf("num_rays=%d\n", vol_p->ih.ic.num_rays);
    printf("num_bins=%d\n", vol_p->ih.tc.tri.num_bins_out);
    printf("range_bin0=%d\n", vol_p->ih.tc.tri.rng_1st_bin);
    printf("bin_step=%d\n", vol_p->ih.tc.tri.step_out);
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
    printf("prf=%.2lf\n", prf);
    printf("prf_mode=%s\n", mp_s);
    printf("vel_ua=%.3lf\n", vel_ua);
    return SIGMET_OK;
}

int near_sweep_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;		/* Sweep angle, degrees */
    double ang;			/* Angle from command line */
    double da_min;		/* Angle difference, smallest difference */
    int s, nrst;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s angle\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    ang_s = argv[2];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point for sweep angle,"
		" got %s\n", argv0, argv1, ang_s);
	return SIGMET_BAD_ARG;
    }
    ang *= RAD_PER_DEG;
    if ( !vol_p->sweep_hdr ) {
	fprintf(stderr, "%s %s: sweep headers not loaded. "
		"Is volume truncated?.\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    nrst = -1;
    for (da_min = DBL_MAX, s = 0; s < vol_p->num_sweeps_ax; s++) {
	double swang, da;	/* Sweep angle, angle difference */

	swang = GeogLonR(vol_p->sweep_hdr[s].angle, ang);
	da = fabs(swang - ang);
	if ( da < da_min ) {
	    da_min = da;
	    nrst = s;
	}
    }
    printf("%d\n", nrst);
    return SIGMET_OK;
}

int sweep_headers_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < vol_p->ih.tc.tni.num_sweeps; s++) {
	printf("sweep %2d ", s);
	if ( !vol_p->sweep_hdr[s].ok ) {
	    printf("bad\n");
	} else {
	    int yr, mon, da, hr, min, sec;

	    if ( Tm_JulToCal(vol_p->sweep_hdr[s].time,
			&yr, &mon, &da, &hr, &min, &sec) ) {
		printf("%04d/%02d/%02d %02d:%02d:%02d ",
			yr, mon, da, hr, min, sec);
	    } else {
		printf("0000/00/00 00:00:00 ");
	    }
	    printf("%7.3f\n", vol_p->sweep_hdr[s].angle * DEG_PER_RAD);
	}
    }
    return SIGMET_OK;
}

int ray_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int s, r;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	if ( vol_p->sweep_hdr[s].ok ) {
	    for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		int yr, mon, da, hr, min, sec;

		if ( !vol_p->ray_hdr[s][r].ok ) {
		    continue;
		}
		printf("sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(vol_p->ray_hdr[s][r].time,
			    &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(stderr, "%s %s: bad ray time\n",
			    argv0, argv1);
		    return SIGMET_BAD_TIME;
		}
		printf("%04d/%02d/%02d %02d:%02d:%02d | ",
			yr, mon, da, hr, min, sec);
		printf("az %7.3f %7.3f | ",
			vol_p->ray_hdr[s][r].az0 * DEG_PER_RAD,
			vol_p->ray_hdr[s][r].az1 * DEG_PER_RAD);
		printf("tilt %6.3f %6.3f\n",
			vol_p->ray_hdr[s][r].tilt0 * DEG_PER_RAD,
			vol_p->ray_hdr[s][r].tilt1 * DEG_PER_RAD);
	    }
	}
    }
    return SIGMET_OK;
}

int new_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int a;
    char *abbrv;			/* Data type abbreviation */
    char *val_s = NULL;			/* Initial value */
    double val;
    char *descr = NULL;			/* Descriptor for new field */
    char *unit = NULL;			/* Unit for new field */
    int status;				/* Result of a function */

    /*
       Identify data type from command line. Fail if volume already has
       this data type.
     */

    if ( argc < 3 || argc > 9 ) {
	fprintf(stderr, "Usage: %s %s data_type [-d description] [-u unit] "
		"[-v value]\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];

    /*
       Obtain optional descriptor, units, and initial value, or use defaults.
     */

    for (a = 3; a < argc; a++) {
	if (strcmp(argv[a], "-d") == 0) {
	    descr = argv[++a];
	} else if (strcmp(argv[a], "-u") == 0) {
	    unit = argv[++a];
	} else if (strcmp(argv[a], "-v") == 0) {
	    val_s = argv[++a];
	} else {
	    fprintf(stderr, "%s %s: unknown option %s.\n", argv0, argv1, argv[a]);
	    return SIGMET_BAD_ARG;
	}
    }
    if ( !descr || strlen(descr) == 0 ) {
	descr = "No description";
    }
    if ( !unit || strlen(unit) == 0 ) {
	unit = "Dimensionless";
    }
    if ( (status = Sigmet_Vol_NewField(vol_p, abbrv, descr, unit))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add data type %s to volume\n",
		argv0, argv1, abbrv);
	return status;
    }

    /*
       If given optional value, initialize new field with it.
     */

    if ( val_s ) {
	if ( sscanf(val_s, "%lf", &val) == 1 ) {
	    status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, val);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %lf in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val);
		return status;
	    }
	} else if ( strcmp(val_s, "r_beam") == 0 ) {
	    status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val_s);
		return status;
	    }
	} else {
	    status = Sigmet_Vol_Fld_Copy(vol_p, abbrv, val_s);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val_s);
		return status;
	    }
	}
    }

    return SIGMET_OK;
}

int del_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    char *abbrv;			/* Data type abbreviation */
    int status;				/* Result of a function */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( (status = Sigmet_Vol_DelField(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not remove data type %s from volume\n",
		argv0, argv1, abbrv);
	return status;
    }
    return SIGMET_OK;
}

/*
   Print volume memory usage.
 */

int size_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    printf("%lu\n", (unsigned long)vol_p->size);
    return SIGMET_OK;
}

/*
   Set value for a field.
 */

int set_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    d_s = argv[3];

    /*
       Parse value and set in data array.
       "r_beam" => set bin value to distance along bin, in meters.
       Otherwise, value must be a floating point number.
     */

    if ( strcmp("r_beam", d_s) == 0 ) {
	if ( (status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to beam range in volume\n",
		    argv0, argv1, abbrv);
	    return status;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, d)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to %lf in volume\n",
		    argv0, argv1, abbrv, d);
	    return status;
	}
    } else {
	fprintf(stderr, "%s %s: field value must be a number or \"r_beam\"\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    return SIGMET_OK;
}

/*
   Add a scalar or another field to a field.
 */

int add_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_AddVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not add %s to %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_AddFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add %s to %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    return SIGMET_OK;
}

/*
   Subtract a scalar or another field from a field.
 */

int sub_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SubVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not subtract %lf from %s in "
		    "volume\n", argv0, argv1, a, abbrv);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_SubFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not subtract %s from %s in volume\n",
		argv0, argv1, a_s, abbrv);
	return status;
    }
    return SIGMET_OK;
}

/*
   Multiply a field by a scalar or another field
 */

int mul_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_MulVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not multiply %s by %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_MulFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not multiply %s by %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    return SIGMET_OK;
}

/*
   Divide a field by a scalar or another field
 */

int div_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_DivVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not divide %s by %lf in volume\n",
		    argv0, argv1, abbrv, a);
	    return status;
	}
    } else if ( (status = Sigmet_Vol_Fld_DivFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not divide %s by %s in volume\n",
		argv0, argv1, abbrv, a_s);
	return status;
    }
    return SIGMET_OK;
}

/*
   Replace a field with it's log10.
 */

int log10_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( (status = Sigmet_Vol_Fld_Log10(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute log10 of %s in volume\n",
		argv0, argv1, abbrv);
	return status;
    }
    return SIGMET_OK;
}

int incr_time_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    char *dt_s;
    double dt;				/* Time increment, seconds */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dt\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    dt_s = argv[2];
    if ( sscanf(dt_s, "%lf", &dt) != 1) {
	fprintf(stderr, "%s %s: expected float value for time increment, got "
		"%s\n", argv0, argv1, dt_s);
	return SIGMET_BAD_ARG;
    }
    if ( !Sigmet_Vol_IncrTm(vol_p, dt / 86400.0) ) {
	fprintf(stderr, "%s %s: could not increment time in volume\n",
		argv0, argv1);
	return SIGMET_BAD_TIME;
    }
    return SIGMET_OK;
}

int data_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int s, y, r, b;
    char *abbrv;
    double d;
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
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 5 && sscanf(argv[4], "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 6 && sscanf(argv[5], "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return SIGMET_BAD_ARG;
    }
    if ( argc >= 7 ) {
	fprintf(stderr, "Usage: %s %s [[[[data_type] sweep] ray] bin]\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }

    /*
       Validate.
     */

    if ( abbrv ) {
	for (y = 0;
		y < vol_p->num_types && strcmp(vol_p->dat[y].abbrv, abbrv) != 0;
		y++) {
	}
	if ( y == vol_p->num_types ) {
	    fprintf(stderr, "%s %s: no data type named %s\n",
		    argv0, argv1, abbrv);
	    return SIGMET_BAD_ARG;
	}
    }
    if ( s != all && s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r != all && r >= (int)vol_p->ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b != all && b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < vol_p->num_types; y++) {
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		printf("%s. sweep %d\n", vol_p->dat[y].abbrv, s);
		for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		    if ( !vol_p->ray_hdr[s][r].ok ) {
			continue;
		    }
		    printf("ray %d: ", r);
		    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
			d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
			if ( Sigmet_IsData(d) ) {
			    printf("%f ", d);
			} else {
			    printf("nodat ");
			}
		    }
		    printf("\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    printf("%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( !vol_p->ray_hdr[s][r].ok ) {
		    continue;
		}
		printf("ray %d: ", r);
		for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		    if ( Sigmet_IsData(d) ) {
			printf("%f ", d);
		    } else {
			printf("nodat ");
		    }
		}
		printf("\n");
	    }
	}
    } else if ( r == all && b == all ) {
	printf("%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	    if ( !vol_p->ray_hdr[s][r].ok ) {
		continue;
	    }
	    printf("ray %d: ", r);
	    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else if ( b == all ) {
	if ( vol_p->ray_hdr[s][r].ok ) {
	    printf("%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else {
	if ( vol_p->ray_hdr[s][r].ok ) {
	    printf("%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
	    if ( Sigmet_IsData(d) ) {
		printf("%f ", d);
	    } else {
		printf("nodat ");
	    }
	    printf("\n");
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

int bdata_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int s, y, r, b;
    char *abbrv;
    static float *ray_p;	/* Buffer to receive ray data */
    int num_bins_out;
    int status, n;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type sweep_index\n",
		argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    abbrv = argv[2];
    if ( sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return SIGMET_BAD_ARG;
    }
    for (y = 0;
	    y < vol_p->num_types && strcmp(vol_p->dat[y].abbrv, abbrv) != 0;
	    y++) {
    }
    if ( y == vol_p->num_types ) {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    n = num_bins_out = vol_p->ih.tc.tri.num_bins_out;
    if ( !ray_p && !(ray_p = CALLOC(num_bins_out, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate output buffer for ray.\n");
	return SIGMET_ALLOC_FAIL;
    }
    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	for (b = 0; b < num_bins_out; b++) {
	    ray_p[b] = Sigmet_NoData();
	}
	if ( vol_p->ray_hdr[s][r].ok ) {
	    status = Sigmet_Vol_GetRayDat(vol_p, y, s, r, &ray_p, &n);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "Could not get ray data for data type %s, "
			"sweep index %d, ray %d.\n", abbrv, s, r);
		return status;
	    }
	    if ( n > num_bins_out ) {
		fprintf(stderr, "Ray %d or sweep %d, data type %s has "
			"unexpected number of bins - %d instead of %d.\n",
			r, s, abbrv, n, num_bins_out);
		return SIGMET_BAD_VOL;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins_out, stdout)
		!= num_bins_out ) {
	    fprintf(stderr, "Could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n",
		    abbrv, s, r, strerror(errno));
	    return SIGMET_IO_FAIL;
	}
    }
    return SIGMET_OK;
}

int bin_outline_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;				/* Result of a function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];

    if ( argc != 5 ) {
	fprintf(stderr, "Usage: %s %s sweep ray bin\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];

    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(r_s, "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return SIGMET_BAD_ARG;
    }
    if ( sscanf(b_s, "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( r >= vol_p->ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	return SIGMET_RNG_ERR;
    }
    if ( b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	return SIGMET_RNG_ERR;
    }
    if ( (status = Sigmet_Vol_BinOutl(vol_p, s, r, b, corners)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute bin outlines for bin "
		"%d %d %d in volume\n", argv0, argv1, s, r, b);
	return status;
    }
    printf("%f %f %f %f %f %f %f %f\n",
	    corners[0] * DEG_RAD, corners[1] * DEG_RAD,
	    corners[2] * DEG_RAD, corners[3] * DEG_RAD,
	    corners[4] * DEG_RAD, corners[5] * DEG_RAD,
	    corners[6] * DEG_RAD, corners[7] * DEG_RAD);

    return SIGMET_OK;
}

int radar_lon_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    char *lon_s;			/* New longitude, degrees, in argv */
    double lon;				/* New longitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lon\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lon_s = argv[2];
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for "
		"new longitude, got %s\n", argv0, argv1, lon_s);
	return SIGMET_BAD_ARG;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.longitude = Sigmet_RadBin4(lon);
    vol_p->mod = 1;

    return SIGMET_OK;
}

int radar_lat_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    char *lat_s;			/* New latitude, degrees, in argv */
    double lat;				/* New latitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lat\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    lat_s = argv[2];
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for "
		"new latitude, got %s\n", argv0, argv1, lat_s);
	return SIGMET_BAD_ARG;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.latitude = Sigmet_RadBin4(lat);
    vol_p->mod = 1;

    return SIGMET_OK;
}

int shift_az_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    char *daz_s;			/* Degrees to add to each azimuth */
    double daz;				/* Radians to add to each azimuth */
    unsigned long idaz;			/* Binary angle to add to each
					   azimuth */
    int s, r;				/* Loop indeces */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dz\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    daz_s = argv[2];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(stderr, "%s %s: expected float value for azimuth shift, "
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
	    vol_p->ray_hdr[s][r].az0
		= GeogLonR(vol_p->ray_hdr[s][r].az0 + daz, 180.0 * RAD_PER_DEG);
	    vol_p->ray_hdr[s][r].az1
		= GeogLonR(vol_p->ray_hdr[s][r].az1 + daz, 180.0 * RAD_PER_DEG);
	}
    }
    vol_p->mod = 1;
    return SIGMET_OK;
}

int outlines_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status = SIGMET_OK;		/* Return value of this function */
    int bnr;				/* If true, send raw binary output */
    int fill;				/* If true, fill space between rays */
    char *s_s;				/* Sweep index, as a string */
    char *abbrv;			/* Data type abbreviation */
    char *min_s, *max_s;		/* Bounds of data interval of interest
					 */
    char *outlnFlNm;			/* Name of output file */
    FILE *outlnFl;			/* Output file */
    int s;				/* Sweep index */
    double min, max;			/* Bounds of data interval of interest
					 */
    int c;				/* Return value from getopt */
    extern char *optarg;
    extern int opterr, optind, optopt;	/* See getopt (3) */

    if ( argc < 7 ) {
	fprintf(stderr, "Usage: %s %s [-f] [-b] data_type sweep min max "
		"out_file\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    for (bnr = fill = 0, opterr = 0, optind = 1;
	    (c = getopt(argc - 1, argv + 1, "bf")) != -1; ) {
	switch(c) {
	    case 'b':
		bnr = 1;
		break;
	    case 'f':
		fill = 1;
		break;
	    case '?':
		fprintf(stderr, "%s %s: unknown option \"-%c\"\n",
			argv0, argv1, optopt);
		return SIGMET_BAD_ARG;
	}
    }
    abbrv = argv[argc - 5];
    s_s = argv[argc - 4];
    min_s = argv[argc - 3];
    max_s = argv[argc - 2];
    outlnFlNm = argv[argc - 1];
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(min_s, "-inf") == 0 || strcmp(min_s, "-INF") == 0 ) {
	min = -DBL_MAX;
    } else if ( sscanf(min_s, "%lf", &min) != 1 ) {
	fprintf(stderr, "%s %s: expected float value or -INF for data min,"
		" got %s\n", argv0, argv1, min_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(max_s, "inf") == 0 || strcmp(max_s, "INF") == 0 ) {
	max = DBL_MAX;
    } else if ( sscanf(max_s, "%lf", &max) != 1 ) {
	fprintf(stderr, "%s %s: expected float value or INF for data max,"
		" got %s\n", argv0, argv1, max_s);
	return SIGMET_BAD_ARG;
    }
    if ( !(min < max)) {
	fprintf(stderr, "%s %s: minimum (%s) must be less than maximum (%s)\n",
		argv0, argv1, min_s, max_s);
	return SIGMET_BAD_ARG;
    }
    if ( strcmp(outlnFlNm, "-") == 0 ) {
	outlnFl = stdout;
    } else if ( !(outlnFl = fopen(outlnFlNm, "w")) ) {
	fprintf(stderr, "%s %s: could not open %s for output.\n%s\n",
		argv0, argv1, outlnFlNm, strerror(errno));
    }
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    status = Sigmet_Vol_RHI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    fill, outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n", argv0, argv1, abbrv, s);
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    status = Sigmet_Vol_PPI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n", argv0, argv1, abbrv, s);
	    }
	    break;
	case FILE_SCAN:
	case MAN_SCAN:
	    Err_Append("Can only print outlines for RHI and PPI. ");
	    status = SIGMET_BAD_ARG;
	    break;
    }
    if ( outlnFl != stdout ) {
	fclose(outlnFl);
    }
    return status;
}

int dorade_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
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
	    fprintf(stderr, "%s %s: expected integer for sweep index, got \"%s"
		    "\"\n", argv0, argv1, s_s);
	    return SIGMET_BAD_ARG;
	}
    } else {
	fprintf(stderr, "Usage: %s %s [s]\n", argv0, argv1);
	return SIGMET_BAD_ARG;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	return SIGMET_RNG_ERR;
    }
    if ( s == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not translate sweep %d of volume "
			"to DORADE format\n", argv0, argv1, s);
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(stderr, "%s %s: could not write DORADE file for sweep "
			"%d of volume\n", argv0, argv1, s);
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not translate sweep %d of volume to "
		    "DORADE format\n", argv0, argv1, s);
	    goto error;
	}
	if ( !Dorade_Sweep_Write(&swp) ) {
	    fprintf(stderr, "%s %s: could not write DORADE file for sweep "
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
