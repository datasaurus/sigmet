/*
   -	sigDataType.c --
   -		Define functions that manipulate Sigmet data.
   -
   .	Copyright (c) 2004 Gordon D. Carrie
   .	All rights reserved
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.11 $ $Date: 2010/02/26 16:21:44 $
   .
   .	Reference: IRIS Programmers Manual
 */

#include <stdlib.h>
#include <math.h>
#include "err_msg.h"
#include "sigmet.h"

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
    "Total power (1 byte)",
    "Reflectivity (1 byte)",
    "Velocity (1 byte)",
    "Width (1 byte)",
    "Differential reflectivity (1 byte)",
    "Corrected reflectivity (1 byte)",
    "Total power (2 byte)",
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

static float itof_XHDR(struct Sigmet_Vol v, unsigned i);
static float itof_DBT(struct Sigmet_Vol v, unsigned i);
static float itof_DBZ(struct Sigmet_Vol v, unsigned i);
static float itof_VEL(struct Sigmet_Vol v, unsigned i);
static float itof_WIDTH(struct Sigmet_Vol v, unsigned i);
static float itof_ZDR(struct Sigmet_Vol v, unsigned i);
static float itof_DBZC(struct Sigmet_Vol v, unsigned i);
static float itof_DBT2(struct Sigmet_Vol v, unsigned i);
static float itof_DBZ2(struct Sigmet_Vol v, unsigned i);
static float itof_VEL2(struct Sigmet_Vol v, unsigned i);
static float itof_WIDTH2(struct Sigmet_Vol v, unsigned i);
static float itof_ZDR2(struct Sigmet_Vol v, unsigned i);
static float itof_RAINRATE2(struct Sigmet_Vol v, unsigned i);
static float itof_KDP(struct Sigmet_Vol v, unsigned i);
static float itof_KDP2(struct Sigmet_Vol v, unsigned i);
static float itof_PHIDP(struct Sigmet_Vol v, unsigned i);
static float itof_VELC(struct Sigmet_Vol v, unsigned i);
static float itof_SQI(struct Sigmet_Vol v, unsigned i);
static float itof_RHOHV(struct Sigmet_Vol v, unsigned i);
static float itof_RHOHV2(struct Sigmet_Vol v, unsigned i);
static float itof_DBZC2(struct Sigmet_Vol v, unsigned i);
static float itof_VELC2(struct Sigmet_Vol v, unsigned i);
static float itof_SQI2(struct Sigmet_Vol v, unsigned i);
static float itof_PHIDP2(struct Sigmet_Vol v, unsigned i);
static float itof_LDRH(struct Sigmet_Vol v, unsigned i);
static float itof_LDRH2(struct Sigmet_Vol v, unsigned i);
static float itof_LDRV(struct Sigmet_Vol v, unsigned i);
static float itof_LDRV2(struct Sigmet_Vol v, unsigned i);

static double v_nq(struct Sigmet_Vol v);

typedef float (*itof_proc)(struct Sigmet_Vol, unsigned);
static itof_proc (itof)[SIGMET_NTYPES] = {
    itof_XHDR,	itof_DBT,	itof_DBZ,	itof_VEL,	itof_WIDTH,
    itof_ZDR,	itof_DBZC,	itof_DBT2,	itof_DBZ2,	itof_VEL2,
    itof_WIDTH2,itof_ZDR2,	itof_RAINRATE2,	itof_KDP,	itof_KDP2,
    itof_PHIDP,	itof_VELC,	itof_SQI,	itof_RHOHV,	itof_RHOHV2,
    itof_DBZC2,	itof_VELC2,	itof_SQI2,	itof_PHIDP2,	itof_LDRH,
    itof_LDRH2,	itof_LDRV,	itof_LDRV2
};

double Sigmet_Bin4Rad(unsigned long a)
{
    return (double)a / (unsigned)0xFFFFFFFF * 2 * PI;
}

double Sigmet_Bin2Rad(unsigned short a)
{
    return (double)a / (unsigned)0xFFFF * 2 * PI;
}

unsigned long Sigmet_RadBin4(double a)
{
    return (unsigned)0xFFFFFFFF * a / (2 * PI);
}

unsigned long Sigmet_RadBin2(double a)
{
    return (unsigned)0xFFFF * a / (2 * PI);
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

float Sigmet_NoData(void)
{
    return FLT_MAX;
}

int Sigmet_IsData(float v)
{
    return !(v == FLT_MAX);
}

int Sigmet_IsNoData(float v)
{
    return v == FLT_MAX;
}


/* Convert Sigmet volume integer storage value to float measurement */
float Sigmet_DataType_ItoF(enum Sigmet_DataType y, struct Sigmet_Vol v, unsigned i)
{
    return (*itof[y])(v, i);
}

/* Extended header is not really a "data type." */
static float itof_XHDR(struct Sigmet_Vol v, unsigned i)
{
    return Sigmet_NoData();
}

static float itof_DBT(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0) ? Sigmet_NoData() : (i > 255) ? 95.5 : 0.5 * (i - 64.0);
}

static float itof_DBZ(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0) ? Sigmet_NoData() : (i > 255) ? 95.5 : 0.5 * (i - 64.0);
}

/* Nyquist velocity */
static double v_nq(struct Sigmet_Vol vol)
{
    double wav_len, prf;

    prf = vol.ih.tc.tdi.prf;
    wav_len = 0.01 * 0.01 * vol.ih.tc.tmi.wave_len;
    switch (vol.ih.tc.tdi.m_prf_mode) {
	case ONE_ONE:
	    return 0.25 * wav_len * prf;
	case TWO_THREE:
	    return 2 * 0.25 * wav_len * prf;
	case THREE_FOUR:
	    return 3 * 0.25 * wav_len * prf;
	case FOUR_FIVE:
	    return 3 * 0.25 * wav_len * prf;
    }
    return Sigmet_NoData();
}

/* Scale velocity to Nyquist. */
static float itof_VEL(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : v_nq(v) * (i - 128.0) / 127.0;
}

/* Scale spectrum width to Nyquist velocity. */
static float itof_WIDTH(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : v_nq(v) * i / 256.0;
}

static float itof_ZDR(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : (i - 128.0) / 16.0;
}

static float itof_DBZC(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0) ? Sigmet_NoData() : (i > 255) ? 95.5 : 0.5 * (i - 64.0);
}

static float itof_DBT2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_DBZ2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_VEL2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_WIDTH2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * i;
}

static float itof_ZDR2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_RAINRATE2(struct Sigmet_Vol v, unsigned i)
{
    unsigned e;		/* 4 bit exponent */
    unsigned m;		/* 12 bit mantissa */

    if (i == 0 || i > 65535)
    {
	return Sigmet_NoData();
    }
    e = (unsigned)(0xF000 & i) >> 12;
    m = 0x0FFF & i;
    if (e == 0) {
	return 0.0001 * (m - 1);
    } else {
	return 0.0001 * (((0x01000 | m) << (e - 1)) - 1);
    }
}

static float itof_KDP(struct Sigmet_Vol v, unsigned i)
{
    double wav_len;

    wav_len = 0.01 * v.ih.tc.tmi.wave_len;
    if (i == 0 || i > 255) {
	return Sigmet_NoData();
    } else if (i > 128) {
	return 0.25 * pow(600.0, (i - 129.0) / 126.0) / wav_len;
    } else if (i == 128) {
	return 0.0;
    } else {
	return -0.25 * pow(600.0, (127.0 - i) / 126.0) / wav_len;
    }
}

static float itof_KDP2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_PHIDP(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : 180.0 / 254.0 * (i - 1.0);
}

static float itof_VELC(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : 75.0 / 127.0 * (i - 128.0);
}

static float itof_SQI(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 254) ? Sigmet_NoData() : sqrt((i - 1) / 253.0);
}

static float itof_RHOHV(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 254) ? Sigmet_NoData() : sqrt((i - 1) / 253.0);
}

static float itof_RHOHV2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : (i - 1) / 65535.0;
}

static float itof_DBZC2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_VELC2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_SQI2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : (i - 1) / 65535.0;
}

static float itof_PHIDP2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 360.0 / 65534.0 * (i - 1.0);
}

static float itof_LDRH(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : 0.2 * (i - 1) - 45.0;
}

static float itof_LDRH2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}

static float itof_LDRV(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 255) ? Sigmet_NoData() : 0.2 * (i - 1) - 45.0;
}

static float itof_LDRV2(struct Sigmet_Vol v, unsigned i)
{
    return (i == 0 || i > 65535) ? Sigmet_NoData() : 0.01 * (i - 32768.0);
}
