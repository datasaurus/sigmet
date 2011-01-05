/*
 -	sigmet_raw_proj.c --
 -		Manage the geographic projection used in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.5 $ $Date: 2011/01/05 17:04:54 $
 */

#include <string.h>
#include "alloc.h"
#include "err_msg.h"
#include "sigmet_raw.h"

#define DFLT_PROJ_ARGC 5
static char *dflt_proj[DFLT_PROJ_ARGC] = {
    "proj", "-b", "+proj=aeqd", "+ellps=sphere", (char *)NULL
};
static char **proj;

int SigmetRaw_ProjInit(void)
{
    return SigmetRaw_SetProj(DFLT_PROJ_ARGC - 1, dflt_proj);
}

int SigmetRaw_SetProj(int argc, char *argv[])
{
    char **t, **aa, *a, **pp, *p;
    size_t len;

    if ( !(t = REALLOC(proj, (argc + 1) * sizeof(char *))) ) {
	Err_Append("Could not allocate space for default projection array. ");
	return SIGMET_ALLOC_FAIL;
    }
    proj = t;
    for (len = 0, aa = argv; *aa; aa++) {
	len += strlen(*aa) + 1;
    }
    if ( !(*proj = CALLOC(len, 1)) ) {
	Err_Append("Could not allocate space for default projection content. ");
	return SIGMET_ALLOC_FAIL;
    }
    for (p = *proj, pp = proj, aa = argv; *aa; aa++) {
	*pp++ = p;
	for (a = *aa; *a; ) {
	    *p++ = *a++;
	}
	*p++ = '\0';
    }
    *pp = (char *)NULL;
    return SIGMET_OK;
}

char **SigmetRaw_GetProj(void)
{
    return proj;
}
