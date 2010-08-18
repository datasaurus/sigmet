/*
   -	sigmet_raw.h --
   -		Declarations for the sigmet_raw daemon and client.
   -
   .	Copyright (c) 2009 Gordon D. Carrie.
   .	All rights reserved.
   .
   .	Please send feedback to user0@tkgeomap.org
   .
   .	$Revision: 1.6 $ $Date: 2010/07/29 20:22:49 $
 */

#ifndef SIGMET_RAW_H_
#define SIGMET_RAW_H_

#include <proj_api.h>

/* Daemon socket */
#define SIGMET_RAWD_IN "sigmet.in"

/* Maximum number of arguments */
#define SIGMET_RAWD_ARGCX	512

int SigmetRaw_ProjInit(void);
int SigmetRaw_SetProj(int, char **);
projPJ SigmetRaw_GetProj(void);

#endif
