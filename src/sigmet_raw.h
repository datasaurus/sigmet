/*
   -	sigmet_raw.h --
   -		Declarations for the sigmet_raw daemon and client.
   -
   .	Copyright (c) 2009 Gordon D. Carrie.
   .	All rights reserved.
   .
   .	Please send feedback to user0@tkgeomap.org
   .
   .	$Revision: 1.28 $ $Date: 2011/02/14 19:11:22 $
 */

#ifndef SIGMET_RAW_H_
#define SIGMET_RAW_H_

#include <stdio.h>
#include <unistd.h>
#include "sigmet.h"

/*
   Default name for daemon socket
 */

#define SIGMET_RAWD_IN ".sigmet.in"

/*
   Names of files for daemon output and error messages
 */

#define SIGMET_RAWD_LOG ".sigmet.log"
#define SIGMET_RAWD_ERR ".sigmet.err"

/* Maximum number of arguments */
#define SIGMET_RAWD_ARGCX	512

FILE *SigmetRaw_VolOpen(const char *, pid_t *, int);
void SigmetRaw_Load(const char *);
int SigmetRaw_Cmd(const char *);
int SigmetRaw_SetProj(int, char **);
char **SigmetRaw_GetProj(void);
void SigmetRaw_SetImgSz(unsigned, unsigned);
void SigmetRaw_GetImgSz(unsigned *, unsigned *);
void SigmetRaw_SetImgAlpha(double);
double SigmetRaw_GetImgAlpha(void);
int SigmetRaw_SetImgApp(char *);
char * SigmetRaw_GetImgApp(void);

#endif
