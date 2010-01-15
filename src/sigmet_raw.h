/*
   -	sigmet_raw.h --
   -		This header file declares structures and functions
   -		for the sigmet_raw application.
   -
   .	Copyright (c) 2009 Gordon D. Carrie.
   .	All rights reserved.
   .
   .	Please send feedback to user0@tkgeomap.org
   .
   .	$Revision: 1.2 $ $Date: 2010/01/13 23:19:28 $
 */

#ifndef SIGMET_RAW_H_
#define SIGMET_RAW_H_

typedef int (SigmetRaw_Callback)(int , char **);
SigmetRaw_Callback SigmetRaw_Types_Cb;
SigmetRaw_Callback SigmetRaw_Good_Cb;
SigmetRaw_Callback SigmetRaw_Read_Cb;
SigmetRaw_Callback SigmetRaw_Volume_Headers_Cb;
SigmetRaw_Callback SigmetRaw_Ray_Headers_Cb;
SigmetRaw_Callback SigmetRaw_Data_Cb;
SigmetRaw_Callback SigmetRaw_Bin_Outline_Cb;
SigmetRaw_Callback SigmetRaw_Bintvls_Cb;

int SigmetRaw_AddFile(FILE *);
void SigmetRaw_RmFile(FILE *);
FILE ** SigmetRaw_GetFiles(long *);
void SigmetRaw_UseDeg(void);
void SigmetRaw_UseRad(void);
int SigmetRaw_CmdI(const char *);
SigmetRaw_Callback * SigmetRaw_Cmd(const char *);
void SigmetRaw_CleanUp(void);

#endif