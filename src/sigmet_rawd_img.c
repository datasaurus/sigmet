/*
 -	sigmet_raw_proj.c --
 -		Manage image configuration in sigmet_raw.
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
 .	$Revision: 1.12 $ $Date: 2011/06/29 22:14:25 $
 */

#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include "sigmet_raw.h"
#include "alloc.h"
#include "strlcpy.h"
#include "err_msg.h"

static unsigned w_pxl = 600;		/* Width of image in display units,
					   pixels, points, cm */
static unsigned h_pxl = 600;		/* Height of image in display units,
					   pixels, points, cm */
static double alpha = 1.0;		/* alpha channel. 1.0 => translucent */
static char *img_app;			/* External application to
					   draw sweeps */

static void cleanup(void);
static void cleanup(void)
{
    FREE(img_app);
    img_app = NULL;
}

void SigmetRaw_SetImgSz(unsigned w, unsigned h)
{
    w_pxl = w;
    h_pxl = h;
}

void SigmetRaw_GetImgSz(unsigned *w, unsigned *h)
{
    *w = w_pxl;
    *h = h_pxl;
}

void SigmetRaw_SetImgAlpha(double a)
{
    alpha = a;
}

double SigmetRaw_GetImgAlpha(void)
{
    return alpha;
}

/*
   Set image application to nm.
 */

int SigmetRaw_SetImgApp(char *nm)
{
    char *argv[2];
    pid_t pid, p_t;
    int wr, rd;
    int si;
    static int init;
    size_t sz;

    if ( !init ) {
	atexit(cleanup);
	init = 1;
    }

    /*
       Check viability of nm.
     */
    
    argv[0] = nm;
    argv[1] = NULL;
    if ( (pid = Sigmet_Execvp_Pipe(argv, &wr, &rd)) == -1 ) {
	Err_Append("Could spawn image app for test. ");
	return SIGMET_BAD_ARG;
    }
    close(rd);
    close(wr);
    p_t = waitpid(pid, &si, 0);
    if ( p_t == pid ) {
	if ( WIFEXITED(si) && WEXITSTATUS(si) == EXIT_FAILURE ) {
	    Err_Append("Image app failed during test. ");
	    return SIGMET_HELPER_FAIL;
	} else if ( WIFSIGNALED(si) ) {
	    Err_Append("Image app exited on signal during test. ");
	    return SIGMET_HELPER_FAIL;
	}
    } else {
	Err_Append("Could not get exit status for image app. ");
	if (p_t == -1) {
	    Err_Append(strerror(errno));
	    Err_Append(". ");
	} else {
	    Err_Append("Unknown error. ");
	}
	return SIGMET_HELPER_FAIL;
    }

    /*
       nm works. Register it.
     */

    cleanup();
    sz = strlen(nm) + 1;
    if ( !(img_app = CALLOC(sz, 1)) ) {
	Err_Append("Could not allocate memory for image app name. ");
	return SIGMET_ALLOC_FAIL;
    }
    strlcpy(img_app, nm, sz);
    return SIGMET_OK;
}

char * SigmetRaw_GetImgApp(void)
{
    return img_app;
}
