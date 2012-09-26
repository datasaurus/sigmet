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
   .	$Revision: 1.89 $ $Date: 2012/09/19 22:09:47 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include "sigmet_raw.h"

/*
   Local functions
 */

static int handle_signals(void);
static void handler(int signum);

/*
   Subcommand names and associated callbacks. The hash function defined
   below returns the index from cmd1v or cb1v corresponding to string argv1.
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
