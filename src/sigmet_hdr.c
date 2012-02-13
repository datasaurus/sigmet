/*
   -	sigmet_hdr.c --
   -		Print volume headers.
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
   .	$Revision: 1.6 $ $Date: 2012/01/24 22:52:59 $
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include "alloc.h"
#include "err_msg.h"
#include "geog_lib.h"
#include "sigmet.h"

/*
   Usage
   sigmet_hdr [-a] [raw_file]
 */

/*
   Local functions
 */

static int handle_signals(void);
static void handler(int signum);
static int print_vol_hdr(struct Sigmet_Vol *vol_p);

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *vol_fl_nm = "-";
    FILE *in;
    int abbrv = 0;		/* If true, give abbreviated output */
    struct Sigmet_Vol vol;

    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.",
		argv0, getpid());
	return EXIT_FAILURE;
    }

    Sigmet_Vol_Init(&vol);

    if ( argc == 1 ) {
	in = stdin;
    } else if ( argc == 2 ) {
	if ( strcmp(argv[1], "-v") == 0 ) {
	    printf("%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
		    "All rights reserved.\n", argv[0], SIGMET_VERSION);
	    return EXIT_SUCCESS;
	} else if ( strcmp(argv[1], "-a") == 0 ) {
	    abbrv = 1;
	    vol_fl_nm = "-";
	} else {
	    vol_fl_nm = argv[1];
	}
    } else if ( argc == 3 ) {
	if ( strcmp(argv[1], "-a") != 0 ) {
	    fprintf(stderr, "Usage: %s [-a] [raw_file]\n", argv0);
	    exit(EXIT_FAILURE);
	}
	abbrv = 1;
	vol_fl_nm = argv[2];
    } else {
	fprintf(stderr, "Usage: %s [-a] [raw_file]\n", argv0);
	exit(EXIT_FAILURE);
    }
    if ( strcmp(vol_fl_nm, "-") == 0 ) {
	in = stdin;
    } else {
	in = fopen(vol_fl_nm, "r");
    }
    if ( !in ) {
	fprintf(stderr, "%s: could not open %s for input\n%s\n",
		argv0, vol_fl_nm, Err_Get());
	exit(EXIT_FAILURE);
    }
    if ( Sigmet_Vol_ReadHdr(in, &vol) != SIGMET_OK ) {
	fprintf(stderr, "%s: read failed\n%s\n", argv0, Err_Get());
	exit(EXIT_FAILURE);
    }
    if ( abbrv ) {
	print_vol_hdr(&vol);
    } else {
	Sigmet_Vol_PrintHdr(stdout, &vol);
    }

    return EXIT_SUCCESS;
}

/*
   Print selected headers from vol_p.
 */

static int print_vol_hdr(struct Sigmet_Vol *vol_p)
{
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";

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
    if ( vol_p->dat[0].data_type->abbrv ) {
	printf("%s", vol_p->dat[0].data_type->abbrv);
    }
    for (y = 1; y < vol_p->num_types; y++) {
	if ( vol_p->dat[y].data_type->abbrv ) {
	    printf(" %s", vol_p->dat[y].data_type->abbrv);
	}
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

    msg = "sigmet_hdr exiting                          \n";
    switch (signum) {
	case SIGTERM:
	    msg = "sigmet_hdr exiting on termination signal    \n";
	    break;
	case SIGFPE:
	    msg = "sigmet_hdr exiting arithmetic exception     \n";
	    break;
	case SIGSYS:
	    msg = "sigmet_hdr exiting on bad system call       \n";
	    break;
	case SIGXCPU:
	    msg = "sigmet_hdr exiting: CPU time limit exceeded \n";
	    break;
	case SIGXFSZ:
	    msg = "sigmet_hdr exiting: file size limit exceeded\n";
	    break;
    }
    (void)write(STDERR_FILENO, msg, 45);
    _exit(EXIT_FAILURE);
}
