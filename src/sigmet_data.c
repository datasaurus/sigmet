/*
   -	sigmet_data.c --
   -		This file defines functions that provide information about
   -		data types described in the IRIS Programmer's Manual.
   -		See sigmet (3).
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
   .	$Revision: 1.36 $ $Date: 2012/02/02 16:56:55 $
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "err_msg.h"
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
   This hash table maps abbreviations to Sigmet_DataTypeN enumerators.
   N_BUCKETS = 182 makes a perfect table.
 */

#define HASH_X 31
#define N_BUCKETS 182
static unsigned hash(const char *);
static enum Sigmet_DataTypeN sig_data_type[N_BUCKETS];

/*
   If init is true, data type interface is initialized.
   Otherwise, need to call data_type_init.
 */

static int init;
static void data_type_init(void);

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
    "mm/hr",		"deg/km",	"deg/km",	"degrees",
    "m/s",		"none",		"none",		"none",
    "dBZ",		"m/s",		"none",		"degrees",
    "none",		"none",		"none",		"none",
};

/*
   Storage formats from data_types.h for Sigmet data types.
   Index with enum Sigmet_DataTypeN.
 */

static enum Sigmet_StorFmt stor_fmt[SIGMET_NTYPES] = {
    SIGMET_MT,	SIGMET_U1,	SIGMET_U1,	SIGMET_U1,
    SIGMET_U1,	SIGMET_U1,	SIGMET_U1,	SIGMET_U2,
    SIGMET_U2,	SIGMET_U2,	SIGMET_U2,	SIGMET_U2,
    SIGMET_U2,	SIGMET_U1,	SIGMET_U2,	SIGMET_U1,
    SIGMET_U1,	SIGMET_U1,	SIGMET_U1,	SIGMET_U2,
    SIGMET_U2,	SIGMET_U2,	SIGMET_U2,	SIGMET_U2,
    SIGMET_U1,	SIGMET_U2,	SIGMET_U1,	SIGMET_U2,
};

/*
   These functions convert storage values from Sigmet raw volumes to
   measurement values for use in computations.
 */

static double stor_comp_XHDR(double, void *);
static double stor_comp_DBT(double, void *);
static double stor_comp_DBZ(double, void *);
static double stor_comp_DBZC(double, void *);
static double stor_comp_VEL(double, void *);
static double stor_comp_WIDTH(double, void *);
static double stor_comp_ZDR(double, void *);
static double stor_comp_KDP(double, void *);
static double stor_comp_PHIDP(double, void *);
static double stor_comp_VELC(double, void *);
static double stor_comp_SQI(double, void *);
static double stor_comp_RHOHV(double, void *);
static double stor_comp_LDRH(double, void *);
static double stor_comp_LDRV(double, void *);
static double stor_comp_DBT2(double, void *);
static double stor_comp_DBZ2(double, void *);
static double stor_comp_VEL2(double, void *);
static double stor_comp_ZDR2(double, void *);
static double stor_comp_KDP2(double, void *);
static double stor_comp_DBZC2(double, void *);
static double stor_comp_VELC2(double, void *);
static double stor_comp_LDRH2(double, void *);
static double stor_comp_LDRV2(double, void *);
static double stor_comp_WIDTH2(double, void *);
static double stor_comp_RAINRATE2(double, void *);
static double stor_comp_RHOHV2(double, void *);
static double stor_comp_SQI2(double, void *);
static double stor_comp_PHIDP2(double, void *);

/*
   Functions to convert storage value to computation value.
   Index with enum Sigmet_DataTypeN.
 */

Sigmet_StorToMxFn stor_to_comp[SIGMET_NTYPES] = {
    stor_comp_XHDR,		stor_comp_DBT,	stor_comp_DBZ,
    stor_comp_VEL,		stor_comp_WIDTH,	stor_comp_ZDR,
    stor_comp_DBZC,		stor_comp_DBT2,	stor_comp_DBZ2,
    stor_comp_VEL2,		stor_comp_WIDTH2,	stor_comp_ZDR2,
    stor_comp_RAINRATE2,	stor_comp_KDP,	stor_comp_KDP2,
    stor_comp_PHIDP,		stor_comp_VELC,	stor_comp_SQI,
    stor_comp_RHOHV,		stor_comp_RHOHV2,	stor_comp_DBZC2,
    stor_comp_VELC2,		stor_comp_SQI2,	stor_comp_PHIDP2,
    stor_comp_LDRH,		stor_comp_LDRH2,	stor_comp_LDRV,
    stor_comp_LDRV2
};

double Sigmet_Bin4Rad(unsigned long a)
{
    return (double)a / TWO_32 * 2 * M_PI;
}

double Sigmet_Bin2Rad(unsigned short a)
{
    return (double)a / TWO_16 * 2 * M_PI;
}

unsigned long Sigmet_RadBin4(double a)
{
    return round(a * TWO_32 / (2 * M_PI));
}

unsigned long Sigmet_RadBin2(double a)
{
    return round(a * TWO_16 / (2 * M_PI));
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

/*
   Initialize the hash table.
 */

static void data_type_init(void)
{
    int sig_type;		/* Loop index */
    unsigned i;

    if ( init ) {
	return;
    }
    for (sig_type = 0; sig_type < SIGMET_NTYPES; sig_type++) {
	i = hash(abbrv[sig_type]);
	assert(i < N_BUCKETS);
	sig_data_type[i] = sig_type;
    }
    init = 1;
}

/*
   Get Sigmet_DataTypeN enumerator corresponding to abbreviation a.
   If successful, put the enumerator at y_p and return true. Otherwise,
   return false.
 */

int Sigmet_DataType_GetN(char *a, enum Sigmet_DataTypeN *y_p)
{
    unsigned i;
    enum Sigmet_DataTypeN y;

    if ( !a ) {
	return 0;
    }
    if ( !init ) {
	data_type_init();
    }
    i = hash(a);
    if ( i < N_BUCKETS ) {
	y = sig_data_type[i];
	if ( strcmp(a, abbrv[sig_data_type[i]]) == 0 ) {
	    if ( y_p ) {
		*y_p = y;
	    }
	    return 1;
	}
    }
    return 0;
}

enum Sigmet_StorFmt Sigmet_DataType_StorFmt(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ?  stor_fmt[y] : SIGMET_MT;
}

Sigmet_StorToMxFn Sigmet_DataType_StorToComp(enum Sigmet_DataTypeN y)
{
    return (y < SIGMET_NTYPES) ?  stor_to_comp[y] : NULL;
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

double Sigmet_DblDbl(double v, void *meta)
{
    return v;
}

static double stor_comp_XHDR(double v, void *meta)
{
    return Sigmet_NoData();
}

static double stor_comp_DBT(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

static double stor_comp_DBZ(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

static double stor_comp_DBZC(double v, void *meta)
{
    return (v == 0)
	? Sigmet_NoData() : (v > 255) ? 95.5 : 0.5 * (v - 64.0);
}

static double stor_comp_VEL(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;

    if ( !vol_p ) {
	return Sigmet_NoData();
    }
    return (v == 0 || v > 255)
	? Sigmet_NoData() : Sigmet_Vol_VNyquist(vol_p) * (v - 128.0) / 127.0;
}

static double stor_comp_WIDTH(double v, void *meta)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)meta;
    double wav_len, prf;

    prf = vol_p->ih.tc.tdi.prf;
    wav_len = 0.01 * 0.01 * vol_p->ih.tc.tmi.wave_len;
    return (v == 0 || v > 255) ? Sigmet_NoData()
	: 0.25 * wav_len * prf * v / 256.0;
}

static double stor_comp_ZDR(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : (v - 128.0) / 16.0;
}

static double stor_comp_KDP(double v, void *meta)
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

static double stor_comp_PHIDP(double v, void *meta)
{
    return (v == 0 || v > 255)
	? Sigmet_NoData() : 180.0 / 254.0 * (v - 1.0);
}

static double stor_comp_VELC(double v, void *meta)
{
    return (v == 0 || v > 255)
	? Sigmet_NoData() : 75.0 / 127.0 * (v - 128.0);
}

static double stor_comp_SQI(double v, void *meta)
{
    return (v == 0 || v > 254) ? Sigmet_NoData() : sqrt((v - 1) / 253.0);
}

static double stor_comp_RHOHV(double v, void *meta)
{
    return (v == 0 || v > 254) ? Sigmet_NoData() : sqrt((v - 1) / 253.0);
}

static double stor_comp_LDRH(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : 0.2 * (v - 1) - 45.0;
}

static double stor_comp_LDRV(double v, void *meta)
{
    return (v == 0 || v > 255) ? Sigmet_NoData() : 0.2 * (v - 1) - 45.0;
}

static double stor_comp_DBT2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_DBZ2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_VEL2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_ZDR2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_KDP2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_DBZC2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_VELC2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_LDRH2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_LDRV2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * (v - 32768.0);
}

static double stor_comp_WIDTH2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : 0.01 * v;
}

static double stor_comp_RAINRATE2(double v, void *meta)
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

static double stor_comp_RHOHV2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : (v - 1) / 65535.0;
}

static double stor_comp_SQI2(double v, void *meta)
{
    return (v == 0 || v > 65535) ? Sigmet_NoData() : (v - 1) / 65535.0;
}

static double stor_comp_PHIDP2(double v, void *meta)
{
    return (v == 0 || v > 65535)
	? Sigmet_NoData() : 360.0 / 65534.0 * (v - 1.0);
}

/*
 * hash - return a random index h, 0 < h < N_BUCKETS, for key k.
 *
 * Reference:
 *   Kernighan, Brian W. and Rob Pike.
 *   The Practice of Programming.
 *   Reading, Massachusetts. 1999
 */

static unsigned hash(const char *k)
{
    unsigned h;

    for (h = 0 ; *k != '\0'; k++) {
	h = HASH_X * h + (unsigned)*k;
    }
    return h % N_BUCKETS;
}
