/*
   -	sigmet_data.c --
   -		This file defines functions that provide information about
   -		data types described in the IRIS Programmer's Manual.
   -		See sigmet (3).
   -
   .	Copyright (c) 2010 Gordon D. Carrie
   .	All rights reserved
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.20 $ $Date: 2010/11/15 20:09:54 $
 */

#include <stdlib.h>
#include <math.h>
#include "err_msg.h"
#include "data_types.h"
#include "sigmet.h"

/*
   Abbreviations for 2^16 and 2^32
 */

#define TWO_16 ((double)((unsigned)0xFFFF) + 1.0)
#define TWO_32 ((double)((unsigned)0xFFFFFFFF) + 1.0)

/*
   Short names for Sigmet data types.  Index with enum Sigmet_DataTypeN.
 */

static char *abbrv[SIGMET_NTYPES] = {
    "DB_XHDR",		"DB_DBT",	"DB_DBZ",	"DB_VEL",
    "DB_WIDTH",		"DB_ZDR",	"DB_DBZC",	"DB_DBT2",
    "DB_DBZ2",		"DB_VEL2",	"DB_WIDTH2",	"DB_ZDR2",
    "DB_RAINRATE2",	"DB_KDP",	"DB_KDP2",	"DB_PHIDP",
    "DB_VELC",		"DB_SQI",	"DB_RHOHV",	"DB_RHOHV2",
    "DB_DBZC2",		"DB_VELC2",	"DB_SQI2",	"DB_PHIDP2",
    "DB_LDRH",		"DB_LDRH2",	"DB_LDRV",	"DB_LDRV2"
};

/*
   Descriptors for Sigmet data types. Index with enum Sigmet_DataTypeN.
 */

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
};

/*
   Units for Sigmet data types.  Index with enum Sigmet_DataTypeN.
 */

static char *unit[SIGMET_NTYPES] = {
    "none",		"dBZ",		"dBZ",		"m/s",
    "m/s",		"dBZ",		"dBZ",		"dBZ",
    "dBZ",		"m/s",		"m/s",		"dBZ",
    "mm/hr",		"none",		"none",		"degrees",
    "m/s",		"none",		"none",		"none",
    "dBZ",		"m/s",		"none",		"degrees",
    "none",		"none",		"none",		"none",
};

/*
   Storage formats from data_types.h for Sigmet data types.
   Index with enum Sigmet_DataTypeN.
 */

static enum DataType_StorFmt stor_fmt[SIGMET_NTYPES] = {
    DATA_TYPE_MT,	DATA_TYPE_U1,	DATA_TYPE_U1,	DATA_TYPE_U1,
    DATA_TYPE_U1,	DATA_TYPE_U1,	DATA_TYPE_U1,	DATA_TYPE_U2,
    DATA_TYPE_U2,	DATA_TYPE_U2,	DATA_TYPE_U2,	DATA_TYPE_U2,
    DATA_TYPE_U2,	DATA_TYPE_U1,	DATA_TYPE_U2,	DATA_TYPE_U1,
    DATA_TYPE_U1,	DATA_TYPE_U1,	DATA_TYPE_U1,	DATA_TYPE_U2,
    DATA_TYPE_U2,	DATA_TYPE_U2,	DATA_TYPE_U2,	DATA_TYPE_U2,
    DATA_TYPE_U1,	DATA_TYPE_U2,	DATA_TYPE_U1,	DATA_TYPE_U2,
};

/*
   Functions to convert storage value to computation value.
 */

DataType_StorToCompFn stor_to_comp[SIGMET_NTYPES] = {
    Sigmet_XHDR_Comp,		Sigmet_DBT_Comp,	Sigmet_DBZ_Comp,
    Sigmet_VEL_Comp,		Sigmet_WIDTH_Comp,	Sigmet_ZDR_Comp,
    Sigmet_DBZC_Comp,		Sigmet_DBT2_Comp,	Sigmet_DBZ2_Comp,
    Sigmet_VEL2_Comp,		Sigmet_WIDTH2_Comp,	Sigmet_ZDR2_Comp,
    Sigmet_RAINRATE2_Comp,	Sigmet_KDP_Comp,	Sigmet_KDP2_Comp,
    Sigmet_PHIDP_Comp,		Sigmet_VELC_Comp,	Sigmet_SQI_Comp,
    Sigmet_RHOHV_Comp,		Sigmet_RHOHV2_Comp,	Sigmet_DBZC2_Comp,
    Sigmet_VELC2_Comp,		Sigmet_SQI2_Comp,	Sigmet_PHIDP2_Comp,
    Sigmet_LDRH_Comp,		Sigmet_LDRH2_Comp,	Sigmet_LDRV_Comp,
    Sigmet_LDRV2_Comp
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

char * Sigmet_DataType_Abbrv(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ? abbrv[y] : NULL;
}

char * Sigmet_DataType_Descr(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ? descr[y] : NULL;
}

char * Sigmet_DataType_Unit(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ? unit[y] : NULL;
}

enum DataType_StorFmt Sigmet_StorFmt(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ?  stor_fmt[y] : NULL;
}

DataType_StorToCompFn Sigmet_StorToCompFn(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ?  stor_to_comp[y] : NULL;
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

double Sigmet_XHDR_Comp(double v, void *meta)
{
    return Sigmet_NoData();
}

double Sigmet_DBT_Comp(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

double Sigmet_DBZ_Comp(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

double Sigmet_DBZC_Comp(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

double Sigmet_VEL_Comp(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;

    if ( !vol_p ) {
	return Sigmet_NoData();
    }
    return (v == 0 || v > 255)
	? Sigmet_NoData() : Sigmet_VNyquist(vol_p) * (v - 128.0) / 127.0;
}

double Sigmet_WIDTH_Comp(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;

    return (v == 0 || v > 255)
	? Sigmet_NoData() : Sigmet_VNyquist(vol_p) * v / 256.0;
}

double Sigmet_ZDR_Comp(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : (v - 128.0) / 16.0;
}

double Sigmet_KDP_Comp(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;
    double wav_len;

    if ( !vol_p ) {
	return Sigmet_NoData();
    }
    wav_len = 0.01 * vol_p->ih.tc.tmi.wave_len;
    if (v == 0 || v > 255) {
	return Sigmet_NoData();
    } else if (v > 128) {
	return 0.25 * pow(600.0, (v - 129.0) / 126.0) / wav_len;
    } else if (v == 128) {
	return 0.0;
    } else {
	return -0.25 * pow(600.0, (127.0 - v) / 126.0) / wav_len;
    }
}

double Sigmet_PHIDP_Comp(double v, void *meta)
{
    return (v == 0 || v > 255)
	? Sigmet_NoData() : 180.0 / 254.0 * (v - 1.0);
}

double Sigmet_VELC_Comp(double v, void *meta)
{
    return (v == 0 || v > 255)
	? Sigmet_NoData() : 75.0 / 127.0 * (v - 128.0);
}

double Sigmet_SQI_Comp(double v, void *meta)
{
    return (v == 0 || v > 254) ? Sigmet_NoData() : sqrt((v - 1) / 253.0);
}

double Sigmet_RHOHV_Comp(double v, void *meta)
{
    return (v == 0 || v > 254) ? Sigmet_NoData() : sqrt((v - 1) / 253.0);
}

double Sigmet_LDRH_Comp(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : 0.2 * (v - 1) - 45.0;
}

double Sigmet_LDRV_Comp(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : 0.2 * (v - 1) - 45.0;
}

double Sigmet_DBT2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_DBZ2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_VEL2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_ZDR2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_KDP2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_DBZC2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_VELC2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_LDRH2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_LDRV2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

double Sigmet_WIDTH2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * v;
}

double Sigmet_RAINRATE2_Comp(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;
    unsigned e;		/* 4 bit exponent */
    unsigned m;		/* 12 bit mantissa */

    if ( !vol_p ) {
	return Sigmet_NoData();
    }
    if (v == 0 || v > 65535)
    {
	return Sigmet_NoData();
    }
    e = (unsigned)(0xF000 & (unsigned)v) >> 12;
    m = 0x0FFF & (unsigned)v;
    if (e == 0) {
	return 0.0001 * (m - 1);
    } else {
	return 0.0001 * (((0x01000 | m) << (e - 1)) - 1);
    }
}

double Sigmet_RHOHV2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : (v - 1) / 65535.0;
}

double Sigmet_SQI2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : (v - 1) / 65535.0;
}

double Sigmet_PHIDP2_Comp(double v, void *meta)
{
    return (v == 0 || v > 65535)
	? Sigmet_NoData() : 360.0 / 65534.0 * (v - 1.0);
}
