/*
   -	sigDataType.c --
   -		Define functions that manipulate Sigmet data.
   -
   .	Copyright (c) 2004 Gordon D. Carrie
   .	All rights reserved
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.14 $ $Date: 2010/10/20 16:23:28 $
   .
   .	Reference: IRIS Programmers Manual
 */

#include <stdlib.h>
#include <math.h>
#include "err_msg.h"
#include "sigmet.h"

/* 2^16 and 2^32 */
#define TWO_16 ((double)((unsigned)0xFFFF) + 1.0)
#define TWO_32 ((double)((unsigned)0xFFFFFFFF) + 1.0)

/* Short names for Sigmet data types.  Index with enum Sigmet_DataType. */
static char *abbrv[SIGMET_NTYPES] = {
    "DB_XHDR",	"DB_DBT",	"DB_DBZ",	"DB_VEL",	"DB_WIDTH",
    "DB_ZDR",	"DB_DBZC",	"DB_DBT2",	"DB_DBZ2",	"DB_VEL2",
    "DB_WIDTH2","DB_ZDR2",	"DB_RAINRATE2",	"DB_KDP",	"DB_KDP2",
    "DB_PHIDP",	"DB_VELC",	"DB_SQI",	"DB_RHOHV",	"DB_RHOHV2",
    "DB_DBZC2",	"DB_VELC2",	"DB_SQI2",	"DB_PHIDP2",	"DB_LDRH",
    "DB_LDRH2",	"DB_LDRV",	"DB_LDRV2", 	"DB_ERROR"
};

/* Descriptors for Sigmet data types. Index with enum Sigmet_DataType. */
static char *descr[SIGMET_NTYPES] = {
    "Extended header",
    "Uncorrected reflectivity (1 byte)",
    "Reflectivity (1 byte)",
    "Velocity (1 byte)",
    "Width (1 byte)",
    "Differential reflectivity (1 byte)",
    "Corrected reflectivity (1 byte)",
    "Uncorrected reflectivity (2 byte)",
    "Reflectivity (2 byte)",
    "Velocity (2 byte)",
    "Width (2 byte)",
    "Differential reflectivity (2 byte)",
    "Rainfall rate (2 byte)",
    "Specific differential phase (1 byte)",
    "Specific differential phase (2 byte)",
    "Differential phase (1 byte)",
    "Unfolded velocity (1 byte)",
    "Signal quality index (1 byte)",
    "RhoHV (1 byte)",
    "RhoHV (2 byte)",
    "Corrected reflectivity (2 byte)",
    "Unfolded velocity (2 byte)",
    "Signal quality index (2 byte)",
    "Differential phase (2 byte)",
    "Horizontal linear depolarization ratio (1 byte)",
    "Horizontal linear depolarization ratio (2 byte)",
    "Vertical linear depolarization ratio (1 byte)",
    "Vertical linear depolarization ratio (2 byte)",
    "Error"
};

double Sigmet_Bin4Rad(unsigned long a)
{
    return (double)a / TWO_32 * 2 * PI;
}

double Sigmet_Bin2Rad(unsigned short a)
{
    return (double)a / TWO_16 * 2 * PI;
}

unsigned long Sigmet_RadBin4(double a)
{
    return a * TWO_32 / (2 * PI);
}

unsigned long Sigmet_RadBin2(double a)
{
    return a * TWO_16 / (2 * PI);
}

/* Fetch the short name of a Sigmet data type */
char * Sigmet_DataType_Abbrv(enum Sigmet_DataType y)
{
    return (y < SIGMET_NTYPES) ? abbrv[y] : NULL;
}

/* Fetch the descriptor of a Sigmet data type */
char * Sigmet_DataType_Descr(enum Sigmet_DataType y)
{
    return (y < SIGMET_NTYPES) ? descr[y] : NULL;
}

double Sigmet_NoData(void)
{
    return DBL_MAX;
}

int Sigmet_IsData(double v)
{
    return !(v == DBL_MAX);
}

int Sigmet_IsNoData(double v)
{
    return v == DBL_MAX;
}
