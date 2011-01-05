/*
 -	sigmet_raw_proj.c --
 -		Manage the geographic projection used in sigmet_raw.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.4 $ $Date: 2010/12/07 18:06:20 $
 */

#include "sigmet_raw.h"
#include "err_msg.h"

static char *dflt_proj[] = { "+proj=aeqd", "+ellps=sphere", NULL};
static char **proj;

int SigmetRaw_ProjInit(void)
{
    proj = dflt_proj;
    return 1;
}

int SigmetRaw_SetProj(int argc, char *argv[])
{
    return SIGMET_OK;
}

char **SigmetRaw_GetProj(void)
{
    return proj;
}
