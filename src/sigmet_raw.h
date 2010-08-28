/*
   -	sigmet_raw.h --
   -		Declarations for the sigmet_raw daemon and client.
   -
   .	Copyright (c) 2009 Gordon D. Carrie.
   .	All rights reserved.
   .
   .	Please send feedback to user0@tkgeomap.org
   .
   .	$Revision: 1.10 $ $Date: 2010/08/27 20:29:35 $
 */

#ifndef SIGMET_RAW_H_
#define SIGMET_RAW_H_

#include <stdio.h>
#include <proj_api.h>

/* Daemon socket */
#define SIGMET_RAWD_IN "sigmet.in"

/* Maximum number of arguments */
#define SIGMET_RAWD_ARGCX	512

int SigmetRaw_ProjInit(void);
int SigmetRaw_SetProj(int, char **);
projPJ SigmetRaw_GetProj(void);
void SigmetRaw_SetImgSz(unsigned, unsigned);
void SigmetRaw_GetImgSz(unsigned *, unsigned *);
void SigmetRaw_SetImgAlpha(double);
double SigmetRaw_GetImgAlpha(void);
int SigmetRaw_SetImgApp(char *);
char * SigmetRaw_GetImgApp(void);
void SigmetRaw_VolInit(void);
void SigmetRaw_VolFree(void);
int SigmetRaw_GoodVol(char *, int, FILE *);
struct Sigmet_Vol *SigmetRaw_ReadHdr(char *, FILE *, int);
struct Sigmet_Vol *SigmetRaw_ReadVol(char *, FILE *, int);
struct Sigmet_Vol *SigmetRaw_GetVol(char *, FILE *, int);
int SigmetRaw_Release(char *, FILE *);
int SigmetRaw_VolList(FILE *);
void SigmetRaw_Flush(void);

#endif
