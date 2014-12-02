/*
   -    dorade_lib.c --
   -            This source file defines functions
   -            that store and access DORADE sweep files.
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
   .    Please send feedback to dev0@trekix.net
   .
   .    $Revision: 1.61 $ $Date: 2014/09/23 21:31:29 $
   .
   .    Reference: 
   .            NCAR/EOL
   .            DORADE FORMAT
   .            Revised July 2010
   .            National Center for Atmospheric Research
   .		NCAR Earth Observing Laboratory EOL
   .            DORADE Doppler Radar Exchange Format DORADE
   .            Originally: Wen-Chau Lee, Craig Walther, Richard Oye
   .		Atmospheric Technology Division (ATD)
   .		P. O. Box 3000, Boulder, CO 80307
   .		October 1994, Revised 2003
   .            Extensively revised by Mike Dixon EOL July 2010
   .            (Unpublished PDF)
 */

#include "unix_defs.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "alloc.h"
#include "hash.h"
#include "val_buf.h"
#include "type_nbit.h"
#include "strlcpy.h"
#include "tm_calc_lib.h"
#include "dorade_lib.h"

/* Default cell geometry type */
static enum Dorade_Cell_Geo cell_geo = CG_CELV;

/* File name length */
#define LEN 1024

/* Sizes of fixed blocks */
static size_t comm_sz = 508;
static size_t sswb_sz = 196;
static size_t vold_sz = 72;
static size_t radd_sz = 300;
static size_t cfac_sz = 72;
static size_t parm_sz = 216;
static size_t celv_sz = 6012;
static size_t csfd_sz = 64;
static size_t swib_sz = 40;
static size_t asib_sz = 80;
static size_t ryib_sz = 44;
static size_t rdat_sz = 16;
static size_t rktb_sz = 28;
static size_t null_sz = 8;

/* RKTB things */
#define NDX_QUE_SIZE 480
struct rotation_table_entry {
    float angle;
    int offset;
    int size;
};

/*
   These compiler variables determine the hash function that lets
   Dorade_Sweep_Read quickly associate four character block type identifiers
   with code to process the indicated block.
   N_BUCKETS buckets makes the hash table perfect that maps block_id strings
   to block type enumerators using the hash function defined below.
 */

#define N_BUCKETS 63
enum BLOCK_TYPE {
    BT_NUL00, BT_RKTB, BT_NUL01, BT_NUL02, BT_NUL03, BT_NUL04, BT_CSFD, BT_NUL05, 
    BT_NUL06, BT_NUL07, BT_RYIB, BT_RDAT, BT_NUL08, BT_NUL09, BT_NUL10, BT_NUL11, 
    BT_NUL12, BT_NUL13, BT_NULL, BT_NUL14, BT_CFAC, BT_NUL15, BT_NUL16, BT_NUL17, 
    BT_NUL18, BT_SSWB, BT_NUL19, BT_NUL20, BT_NUL21, BT_NUL22, BT_NUL23, BT_NUL24, 
    BT_NUL25, BT_SWIB, BT_NUL26, BT_NUL27, BT_NUL28, BT_NUL29, BT_NUL30, BT_VOLD, 
    BT_RADD, BT_NUL31, BT_COMM, BT_NUL32, BT_NUL33, BT_NUL34, BT_NUL35, BT_NUL36, 
    BT_SEDS, BT_CELV, BT_ASIB, BT_NUL37, BT_NUL38, BT_NUL39, BT_NUL40, BT_NUL41, 
    BT_NUL42, BT_NUL43, BT_PARM, BT_NUL44, BT_NUL45, BT_NUL46, BT_NUL47
};
static char *block_id[N_BUCKETS] = {
    "", "RKTB", "", "", "", "", "CSFD", "", 
    "", "", "RYIB", "RDAT", "", "", "", "", 
    "", "", "NULL", "", "CFAC", "", "", "", 
    "", "SSWB", "", "", "", "", "", "", 
    "", "SWIB", "", "", "", "", "", "VOLD", 
    "RADD", "", "COMM", "", "", "", "", "", 
    "SEDS", "CELV", "ASIB", "", "", "", "", "", 
    "", "", "PARM", "", "", "", "", 
};

static int lfread(char *, size_t, size_t, FILE *, char *);
/*
   Find a place for a new parameter named parm_nm in sensor.parms and
   dat arrays. Return an index for it, or -1 if something goes wrong.
 */

int Dorade_Parm_NewIdx(struct Dorade_Sweep *swp_p, char *parm_nm)
{
    struct Dorade_PARM *parms = swp_p->sensor.parms;
    int h0, h;

    h0 = Hash(parm_nm, DORADE_MAX_PARMS);
    if ( strlen(parms[h0].parm_nm) == 0 ) {
	return h0;
    }
    if ( strcmp(parms[h0].parm_nm, parm_nm) == 0 ) {
	fprintf(stderr, "%s already exists in sweep.\n", parm_nm);
	return -1;
    }
    for (h = (h0 + 1) % DORADE_MAX_PARMS;
	    h != h0;
	    h = (h + 1) % DORADE_MAX_PARMS) {
	if ( strlen(parms[h].parm_nm) == 0 ) {
	    return h;
	}
	if ( strcmp(parms[h].parm_nm, parm_nm) == 0 ) {
	    fprintf(stderr, "%s already exists in sweep.\n", parm_nm);
	    return -1;
	}
    }
    return -1;
}

/*
   Return index for parameter name in sensor.parms and dat arrays of the
   sweep at swp_p, or -1 if there is no parameter by that name.
 */

int Dorade_Parm_Idx(struct Dorade_Sweep *swp_p, char *parm_nm)
{
    struct Dorade_PARM *parms = swp_p->sensor.parms;
    int h0, h;

    if ( !parm_nm || strlen(parm_nm) == 0 ) {
	return -1;
    }
    h0 = Hash(parm_nm, DORADE_MAX_PARMS);
    if ( strcmp(parms[h0].parm_nm, parm_nm) == 0 ) {
	return h0;
    }
    for (h = (h0 + 1) % DORADE_MAX_PARMS;
	    h != h0;
	    h = (h + 1) % DORADE_MAX_PARMS) {
	if ( strcmp(parms[h].parm_nm, parm_nm) == 0 ) {
	    return h;
	}
    }
    return -1;
}

/*
   Copy a field. New field will be named new_parm_nm. If not NULL,
   new_parm_description will be copied to the parm_description of the new
   field. Everything else will be copied from the field named parm_nm.
 */

int Dorade_Parm_Cpy(struct Dorade_Sweep *swp_p, char *parm_nm,
	char *new_parm_nm, char *new_parm_description)
{
    int p0, p1;				/* Old parameter, new parameter */
    float **dat;			/* Data array for new parameter */
    int num_parms, num_rays, num_cells;	/* Sweep dimensions */
    struct Dorade_PARM *prev_parm, *parm_p; /* Loop parameters */

    num_parms = swp_p->sensor.radd.num_parms;
    if ( num_parms + 1 > DORADE_MAX_PARMS ) {
	fprintf(stderr, "Sweep cannot have more than %d "
		"parameters.\n", DORADE_MAX_PARMS);
	return 0;
    }
    if ( (num_rays = swp_p->swib.num_rays) == DORADE_BAD_I4 ) {
	fprintf(stderr, "Could not copy %s to %s. Number of rays not known.",
		parm_nm, new_parm_nm);
	return 0;
    }
    if ( (num_cells = Dorade_NCells(swp_p)) == -1 ) {
	fprintf(stderr, "Could not copy %s to %s. Number of cells not known.",
		parm_nm, new_parm_nm);
	return 0;
    }
    if ( (p0 = Dorade_Parm_Idx(swp_p, parm_nm)) == -1 ) {
	fprintf(stderr, "No parameter named %s in sweep.\n", parm_nm);
	return 0;
    }
    if ( (p1 = Dorade_Parm_NewIdx(swp_p, new_parm_nm)) == -1 ) {
	fprintf(stderr, "While copying %s, could not obtain index for new "
		"parameter %s\n", parm_nm, new_parm_nm);
	return 0;
    }
    if ( !(dat = Dorade_Alloc2F(num_rays, num_cells)) ) {
	fprintf(stderr, "Failed to allocate memory for "
		"data array with %d rays and %d cells.\n",
		num_rays, num_cells);
	return 0;
    }
    swp_p->dat[p1] = dat;
    Dorade_PARM_Init(swp_p->sensor.parms + p1);
    swp_p->sensor.parms[p1] = swp_p->sensor.parms[p0];
    strlcpy(swp_p->sensor.parms[p1].parm_nm, new_parm_nm, 8);
    if ( new_parm_description ) {
	strlcpy(swp_p->sensor.parms[p1].parm_description,
		new_parm_description, 40);
    }
    memcpy(swp_p->dat[p1][0], swp_p->dat[p0][0],
	    num_rays * num_cells * sizeof(float));
    for (prev_parm = swp_p->sensor.parm0, parm_p = prev_parm->next;
	    parm_p;
	    prev_parm = parm_p, parm_p = parm_p->next) {
    }
    prev_parm->next = swp_p->sensor.parms + p1;
    swp_p->sensor.parms[p1].next = NULL;
    swp_p->sswb.num_parms++;
    swp_p->sensor.radd.num_parms++;
    swp_p->mod = 1;
    return 1;
}

/*
   Global functions. See dorade_lib (1).
 */

void Dorade_COMM_Init(struct Dorade_COMM *comm_p)
{
    memset(comm_p->comment, 0, sizeof(comm_p->comment));
}

int Dorade_COMM_Read(struct Dorade_COMM *comm_p, char *buf)
{
    ValBuf_GetBytes(&buf, comm_p->comment, 500);
    return 1;
}

int Dorade_COMM_Write(struct Dorade_COMM *comm_p, FILE *out)
{
    char *blk_id = "COMM";
    unsigned blk_len = 508;
    char buf[508], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutBytes(&buf_p, comm_p->comment, 500);
    if ( fwrite(buf, 1, 508, out) != 508 ) {
	fprintf(stderr, "Could not write COMM block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_COMM_Print(struct Dorade_COMM *comm_p, FILE *out)
{
    fprintf(out, "COMM: %s\n", comm_p->comment);
}

void Dorade_SSWB_Init(struct Dorade_SSWB *sswb_p)
{
    sswb_p->last_used = DORADE_BAD_I4;
    sswb_p->i_start_time = DORADE_BAD_I4;
    sswb_p->i_stop_time = DORADE_BAD_I4;
    sswb_p->compression_flag = DORADE_BAD_I4;
    sswb_p->volume_time_stamp = DORADE_BAD_I4;
    sswb_p->num_parms = DORADE_BAD_I4;
    memset(sswb_p->radar_name, 0, sizeof(sswb_p->radar_name));
    sswb_p->start_time = DORADE_BAD_D;
    sswb_p->stop_time = DORADE_BAD_D;
    sswb_p->version_num = DORADE_BAD_I4;
    sswb_p->status = DORADE_BAD_I4;
}

int Dorade_SSWB_Read(struct Dorade_SSWB *sswb_p, char *buf)
{
    sswb_p->last_used = ValBuf_GetI4BYT(&buf);
    sswb_p->i_start_time = ValBuf_GetI4BYT(&buf);
    sswb_p->i_stop_time = ValBuf_GetI4BYT(&buf);
    sswb_p->sizeof_file = ValBuf_GetI4BYT(&buf);
    sswb_p->compression_flag = ValBuf_GetI4BYT(&buf);
    sswb_p->volume_time_stamp = ValBuf_GetI4BYT(&buf);
    sswb_p->num_parms = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, sswb_p->radar_name, 8);
    sswb_p->start_time = ValBuf_GetF8BYT(&buf);
    sswb_p->stop_time = ValBuf_GetF8BYT(&buf);
    sswb_p->version_num = ValBuf_GetI4BYT(&buf);
    ValBuf_GetI4BYT(&buf);
    sswb_p->status = ValBuf_GetI4BYT(&buf);
    return 1;
}

/* Write super sweep identification block to out. */
int Dorade_SSWB_Write(struct Dorade_Sweep *swp_p, FILE *out)
{
    char *blk_id = "SSWB";
    unsigned blk_len = 196;
    struct Dorade_SSWB *sswb_p;
    char buf[196], *buf_p;
    int i;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    sswb_p = &swp_p->sswb;

    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutI4BYT(&buf_p, sswb_p->last_used);
    ValBuf_PutI4BYT(&buf_p, sswb_p->i_start_time);
    ValBuf_PutI4BYT(&buf_p, sswb_p->i_stop_time);

    /* Will write file size when done writing */
    ValBuf_PutI4BYT(&buf_p, DORADE_BAD_I4);

    ValBuf_PutI4BYT(&buf_p, sswb_p->compression_flag);
    ValBuf_PutI4BYT(&buf_p, sswb_p->volume_time_stamp);
    ValBuf_PutI4BYT(&buf_p, sswb_p->num_parms);
    ValBuf_PutBytes(&buf_p, sswb_p->radar_name, 8);
    ValBuf_PutF8BYT(&buf_p, sswb_p->start_time);
    ValBuf_PutF8BYT(&buf_p, sswb_p->stop_time);
    ValBuf_PutI4BYT(&buf_p, sswb_p->version_num);
    ValBuf_PutI4BYT(&buf_p, 0);		/* Number of key tables */
    ValBuf_PutI4BYT(&buf_p, sswb_p->status);
    for (i = 0; i < 7; i++) {
	ValBuf_PutI4BYT(&buf_p, 0);	/* "place_holder" */
    }
    if ( fwrite(buf, 1, 196, out) != 196 ) {
	fprintf(stderr, "Could not write SSWB block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_SSWB_Print(struct Dorade_SSWB *sswb_p, FILE *out)
{
    fprintf(out, "SSWB:last_used = %d\n", sswb_p->last_used);
    fprintf(out, "SSWB:i_start_time = %d\n", sswb_p->i_start_time);
    fprintf(out, "SSWB:i_stop_time = %d\n", sswb_p->i_stop_time);
    fprintf(out, "SSWB:sizeof_file = %d\n", sswb_p->sizeof_file);
    fprintf(out, "SSWB:compression_flag = %d\n", sswb_p->compression_flag);
    fprintf(out, "SSWB:volume_time_stamp = %d\n", sswb_p->volume_time_stamp);
    fprintf(out, "SSWB:num_parms = %d\n", sswb_p->num_parms);
    fprintf(out, "SSWB:radar_name = %s\n", sswb_p->radar_name);
    fprintf(out, "SSWB:start_time = %lf\n", sswb_p->start_time);
    fprintf(out, "SSWB:stop_time = %lf\n", sswb_p->stop_time);
    fprintf(out, "SSWB:version_num = %d\n", sswb_p->version_num);
    fprintf(out, "SSWB:status = %d\n", sswb_p->status);
}

void Dorade_VOLD_Init(struct Dorade_VOLD *vold_p)
{
    vold_p->format_version = DORADE_BAD_I2;
    vold_p->volume_num = DORADE_BAD_I2;
    vold_p->maximum_bytes = DORADE_BAD_I4;
    memset(vold_p->proj_name, 0, sizeof(vold_p->proj_name));
    vold_p->year = DORADE_BAD_I2;
    vold_p->month = DORADE_BAD_I2;
    vold_p->day = DORADE_BAD_I2;
    vold_p->data_set_hour = DORADE_BAD_I2;
    vold_p->data_set_minute = DORADE_BAD_I2;
    vold_p->data_set_second = DORADE_BAD_I2;
    memset(vold_p->flight_number, 0, sizeof(vold_p->flight_number));
    memset(vold_p->gen_facility, 0, sizeof(vold_p->gen_facility));
    vold_p->gen_year = DORADE_BAD_I2;
    vold_p->gen_month = DORADE_BAD_I2;
    vold_p->gen_day = DORADE_BAD_I2;
    vold_p->num_sensors = DORADE_BAD_I2;
}

int Dorade_VOLD_Read(struct Dorade_VOLD *vold_p, char *buf)
{
    vold_p->format_version = ValBuf_GetI2BYT(&buf);
    vold_p->volume_num = ValBuf_GetI2BYT(&buf);
    vold_p->maximum_bytes = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, vold_p->proj_name, 20);
    vold_p->year = ValBuf_GetI2BYT(&buf);
    vold_p->month = ValBuf_GetI2BYT(&buf);
    vold_p->day = ValBuf_GetI2BYT(&buf);
    vold_p->data_set_hour = ValBuf_GetI2BYT(&buf);
    vold_p->data_set_minute = ValBuf_GetI2BYT(&buf);
    vold_p->data_set_second = ValBuf_GetI2BYT(&buf);
    ValBuf_GetBytes(&buf, vold_p->flight_number, 8);
    ValBuf_GetBytes(&buf, vold_p->gen_facility, 8);
    vold_p->gen_year = ValBuf_GetI2BYT(&buf);
    vold_p->gen_month = ValBuf_GetI2BYT(&buf);
    vold_p->gen_day = ValBuf_GetI2BYT(&buf);
    vold_p->num_sensors = ValBuf_GetI2BYT(&buf);
    return 1;
}

int Dorade_VOLD_Write(struct Dorade_VOLD *vold_p, FILE *out)
{
    char *blk_id = "VOLD";
    unsigned blk_len = 72;
    char buf[72], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutI2BYT(&buf_p, vold_p->format_version);
    ValBuf_PutI2BYT(&buf_p, vold_p->volume_num);
    ValBuf_PutI4BYT(&buf_p, vold_p->maximum_bytes);
    ValBuf_PutBytes(&buf_p, vold_p->proj_name, 20);
    ValBuf_PutI2BYT(&buf_p, vold_p->year);
    ValBuf_PutI2BYT(&buf_p, vold_p->month);
    ValBuf_PutI2BYT(&buf_p, vold_p->day);
    ValBuf_PutI2BYT(&buf_p, vold_p->data_set_hour);
    ValBuf_PutI2BYT(&buf_p, vold_p->data_set_minute);
    ValBuf_PutI2BYT(&buf_p, vold_p->data_set_second);
    ValBuf_PutBytes(&buf_p, vold_p->flight_number, 8);
    ValBuf_PutBytes(&buf_p, vold_p->gen_facility, 8);
    ValBuf_PutI2BYT(&buf_p, vold_p->gen_year);
    ValBuf_PutI2BYT(&buf_p, vold_p->gen_month);
    ValBuf_PutI2BYT(&buf_p, vold_p->gen_day);
    ValBuf_PutI2BYT(&buf_p, vold_p->num_sensors);
    if ( fwrite(buf, 1, 72, out) != 72 ) {
	fprintf(stderr, "Could not write VOLD block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_VOLD_Print(struct Dorade_VOLD *vold_p, FILE *out)
{
    fprintf(out, "VOLD:format_version = %d\n", vold_p->format_version);
    fprintf(out, "VOLD:volume_num = %d\n", vold_p->volume_num);
    fprintf(out, "VOLD:maximum_bytes = %d\n", vold_p->maximum_bytes);
    fprintf(out, "VOLD:proj_name = %s\n", vold_p->proj_name);
    fprintf(out, "VOLD:year = %d\n", vold_p->year);
    fprintf(out, "VOLD:month = %d\n", vold_p->month);
    fprintf(out, "VOLD:day = %d\n", vold_p->day);
    fprintf(out, "VOLD:data_set_hour = %d\n", vold_p->data_set_hour);
    fprintf(out, "VOLD:data_set_minute = %d\n", vold_p->data_set_minute);
    fprintf(out, "VOLD:data_set_second = %d\n", vold_p->data_set_second);
    fprintf(out, "VOLD:flight_number = %s\n", vold_p->flight_number);
    fprintf(out, "VOLD:gen_facility = %s\n", vold_p->gen_facility);
    fprintf(out, "VOLD:gen_year = %d\n", vold_p->gen_year);
    fprintf(out, "VOLD:gen_month = %d\n", vold_p->gen_month);
    fprintf(out, "VOLD:gen_day = %d\n", vold_p->gen_day);
    fprintf(out, "VOLD:num_sensors = %d\n", vold_p->num_sensors);
}

void Dorade_RADD_Init(struct Dorade_RADD *radd_p)
{
    int i;

    memset(radd_p->radar_name, 0, sizeof(radd_p->radar_name));
    radd_p->radar_const = DORADE_BAD_F;
    radd_p->peak_power = DORADE_BAD_F;
    radd_p->noise_power = DORADE_BAD_F;
    radd_p->receiver_gain = DORADE_BAD_F;
    radd_p->antenna_gain = DORADE_BAD_F;
    radd_p->system_gain = DORADE_BAD_F;
    radd_p->horz_beam_width = DORADE_BAD_F;
    radd_p->vert_beam_width = DORADE_BAD_F;
    radd_p->radar_type = DORADE_BAD_I2;
    radd_p->scan_mode = DORADE_BAD_I2;
    radd_p->req_rotat_vel = DORADE_BAD_F;
    radd_p->scan_mode_pram0 = DORADE_BAD_F;
    radd_p->scan_mode_pram1 = DORADE_BAD_F;
    radd_p->num_parms = DORADE_BAD_I2;
    radd_p->total_num_des = DORADE_BAD_I2;
    radd_p->data_compress = DORADE_BAD_I2;
    radd_p->data_reduction = DORADE_BAD_I2;
    radd_p->data_red_parm0 = DORADE_BAD_F;
    radd_p->data_red_parm1 = DORADE_BAD_F;
    radd_p->radar_longitude = DORADE_BAD_F;
    radd_p->radar_latitude = DORADE_BAD_F;
    radd_p->radar_altitude = DORADE_BAD_F;
    radd_p->eff_unamb_vel = DORADE_BAD_F;
    radd_p->eff_unamb_range = DORADE_BAD_F;
    radd_p->num_freq_trans = DORADE_BAD_I2;
    radd_p->num_ipps_trans = DORADE_BAD_I2;
    radd_p->freq1 = DORADE_BAD_F;
    radd_p->freq2 = DORADE_BAD_F;
    radd_p->freq3 = DORADE_BAD_F;
    radd_p->freq4 = DORADE_BAD_F;
    radd_p->freq5 = DORADE_BAD_F;
    radd_p->interpulse_per1 = DORADE_BAD_F;
    radd_p->interpulse_per2 = DORADE_BAD_F;
    radd_p->interpulse_per3 = DORADE_BAD_F;
    radd_p->interpulse_per4 = DORADE_BAD_F;
    radd_p->interpulse_per5 = DORADE_BAD_F;
    radd_p->extension_num = DORADE_BAD_I4;
    memset(radd_p->config_name, 0, sizeof(radd_p->config_name));
    radd_p->config_num = DORADE_BAD_I4;
    radd_p->aperture_size = DORADE_BAD_F;
    radd_p->field_of_view = DORADE_BAD_F;
    radd_p->aperture_eff = DORADE_BAD_F;
    for (i = 0; i < 11; i++) {
	radd_p->freq[i] = DORADE_BAD_F;
	radd_p->interpulse_per[i] = DORADE_BAD_F;
    }
    radd_p->pulse_width = DORADE_BAD_F;
    radd_p->primary_cop_baseln = DORADE_BAD_F;
    radd_p->secondary_cop_baseln = DORADE_BAD_F;
    radd_p->pc_xmtr_bandwidth = DORADE_BAD_F;
    radd_p->pc_waveform_type = DORADE_BAD_I4;
    memset(radd_p->site_name, 0, sizeof(radd_p->site_name));
}

int Dorade_RADD_Read(struct Dorade_RADD *radd_p, char *buf)
{
    int i;

    ValBuf_GetBytes(&buf, radd_p->radar_name, 8);
    radd_p->radar_const = ValBuf_GetF4BYT(&buf);
    radd_p->peak_power = ValBuf_GetF4BYT(&buf);
    radd_p->noise_power = ValBuf_GetF4BYT(&buf);
    radd_p->receiver_gain = ValBuf_GetF4BYT(&buf);
    radd_p->antenna_gain = ValBuf_GetF4BYT(&buf);
    radd_p->system_gain = ValBuf_GetF4BYT(&buf);
    radd_p->horz_beam_width = ValBuf_GetF4BYT(&buf);
    radd_p->vert_beam_width = ValBuf_GetF4BYT(&buf);
    radd_p->radar_type = ValBuf_GetI2BYT(&buf);
    radd_p->scan_mode = ValBuf_GetI2BYT(&buf);
    radd_p->req_rotat_vel = ValBuf_GetF4BYT(&buf);
    radd_p->scan_mode_pram0 = ValBuf_GetF4BYT(&buf);
    radd_p->scan_mode_pram1 = ValBuf_GetF4BYT(&buf);
    radd_p->num_parms = ValBuf_GetI2BYT(&buf);
    radd_p->total_num_des = ValBuf_GetI2BYT(&buf);
    radd_p->data_compress = ValBuf_GetI2BYT(&buf);
    radd_p->data_reduction = ValBuf_GetI2BYT(&buf);
    radd_p->data_red_parm0 = ValBuf_GetF4BYT(&buf);
    radd_p->data_red_parm1 = ValBuf_GetF4BYT(&buf);
    radd_p->radar_longitude = ValBuf_GetF4BYT(&buf);
    radd_p->radar_latitude = ValBuf_GetF4BYT(&buf);
    radd_p->radar_altitude = ValBuf_GetF4BYT(&buf);
    radd_p->eff_unamb_vel = ValBuf_GetF4BYT(&buf);
    radd_p->eff_unamb_range = ValBuf_GetF4BYT(&buf);
    radd_p->num_freq_trans = ValBuf_GetI2BYT(&buf);
    radd_p->num_ipps_trans = ValBuf_GetI2BYT(&buf);
    radd_p->freq1 = ValBuf_GetF4BYT(&buf);
    radd_p->freq2 = ValBuf_GetF4BYT(&buf);
    radd_p->freq3 = ValBuf_GetF4BYT(&buf);
    radd_p->freq4 = ValBuf_GetF4BYT(&buf);
    radd_p->freq5 = ValBuf_GetF4BYT(&buf);
    radd_p->interpulse_per1 = ValBuf_GetF4BYT(&buf);
    radd_p->interpulse_per2 = ValBuf_GetF4BYT(&buf);
    radd_p->interpulse_per3 = ValBuf_GetF4BYT(&buf);
    radd_p->interpulse_per4 = ValBuf_GetF4BYT(&buf);
    radd_p->interpulse_per5 = ValBuf_GetF4BYT(&buf);
    radd_p->extension_num = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, radd_p->config_name, 8);
    radd_p->config_num = ValBuf_GetI4BYT(&buf);
    radd_p->aperture_size = ValBuf_GetF4BYT(&buf);
    radd_p->field_of_view = ValBuf_GetF4BYT(&buf);
    radd_p->aperture_eff = ValBuf_GetF4BYT(&buf);
    for (i = 0; i < 11; i++) {
	radd_p->freq[i] = ValBuf_GetF4BYT(&buf);
    }
    for (i = 0; i < 11; i++) {
	radd_p->interpulse_per[i] = ValBuf_GetF4BYT(&buf);
    }
    radd_p->pulse_width = ValBuf_GetF4BYT(&buf);
    radd_p->primary_cop_baseln = ValBuf_GetF4BYT(&buf);
    radd_p->secondary_cop_baseln = ValBuf_GetF4BYT(&buf);
    radd_p->pc_xmtr_bandwidth = ValBuf_GetF4BYT(&buf);
    radd_p->pc_waveform_type = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, radd_p->site_name, 20);
    return 1;
}

int Dorade_RADD_Write(struct Dorade_RADD *radd_p, FILE *out)
{
    char *blk_id = "RADD";
    unsigned blk_len = 300;
    int i;
    char buf[300], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutBytes(&buf_p, radd_p->radar_name, 8);
    ValBuf_PutF4BYT(&buf_p, radd_p->radar_const);
    ValBuf_PutF4BYT(&buf_p, radd_p->peak_power);
    ValBuf_PutF4BYT(&buf_p, radd_p->noise_power);
    ValBuf_PutF4BYT(&buf_p, radd_p->receiver_gain);
    ValBuf_PutF4BYT(&buf_p, radd_p->antenna_gain);
    ValBuf_PutF4BYT(&buf_p, radd_p->system_gain);
    ValBuf_PutF4BYT(&buf_p, radd_p->horz_beam_width);
    ValBuf_PutF4BYT(&buf_p, radd_p->vert_beam_width);
    ValBuf_PutI2BYT(&buf_p, radd_p->radar_type);
    ValBuf_PutI2BYT(&buf_p, radd_p->scan_mode);
    ValBuf_PutF4BYT(&buf_p, radd_p->req_rotat_vel);
    ValBuf_PutF4BYT(&buf_p, radd_p->scan_mode_pram0);
    ValBuf_PutF4BYT(&buf_p, radd_p->scan_mode_pram1);
    ValBuf_PutI2BYT(&buf_p, radd_p->num_parms);
    ValBuf_PutI2BYT(&buf_p, radd_p->total_num_des);
    ValBuf_PutI2BYT(&buf_p, radd_p->data_compress);
    ValBuf_PutI2BYT(&buf_p, radd_p->data_reduction);
    ValBuf_PutF4BYT(&buf_p, radd_p->data_red_parm0);
    ValBuf_PutF4BYT(&buf_p, radd_p->data_red_parm1);
    ValBuf_PutF4BYT(&buf_p, radd_p->radar_longitude);
    ValBuf_PutF4BYT(&buf_p, radd_p->radar_latitude);
    ValBuf_PutF4BYT(&buf_p, radd_p->radar_altitude);
    ValBuf_PutF4BYT(&buf_p, radd_p->eff_unamb_vel);
    ValBuf_PutF4BYT(&buf_p, radd_p->eff_unamb_range);
    ValBuf_PutI2BYT(&buf_p, radd_p->num_freq_trans);
    ValBuf_PutI2BYT(&buf_p, radd_p->num_ipps_trans);
    ValBuf_PutF4BYT(&buf_p, radd_p->freq1);
    ValBuf_PutF4BYT(&buf_p, radd_p->freq2);
    ValBuf_PutF4BYT(&buf_p, radd_p->freq3);
    ValBuf_PutF4BYT(&buf_p, radd_p->freq4);
    ValBuf_PutF4BYT(&buf_p, radd_p->freq5);
    ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per1);
    ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per2);
    ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per3);
    ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per4);
    ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per5);
    ValBuf_PutI4BYT(&buf_p, radd_p->extension_num);
    ValBuf_PutBytes(&buf_p, radd_p->config_name, 8);
    ValBuf_PutI4BYT(&buf_p, radd_p->config_num);
    ValBuf_PutF4BYT(&buf_p, radd_p->aperture_size);
    ValBuf_PutF4BYT(&buf_p, radd_p->field_of_view);
    ValBuf_PutF4BYT(&buf_p, radd_p->aperture_eff);
    for (i = 0; i < 11; i++) {
	ValBuf_PutF4BYT(&buf_p, radd_p->freq[i]);
    }
    for (i = 0; i < 11; i++) {
	ValBuf_PutF4BYT(&buf_p, radd_p->interpulse_per[i]);
    }
    ValBuf_PutF4BYT(&buf_p, radd_p->pulse_width);
    ValBuf_PutF4BYT(&buf_p, radd_p->primary_cop_baseln);
    ValBuf_PutF4BYT(&buf_p, radd_p->secondary_cop_baseln);
    ValBuf_PutF4BYT(&buf_p, radd_p->pc_xmtr_bandwidth);
    ValBuf_PutI4BYT(&buf_p, radd_p->pc_waveform_type);
    ValBuf_PutBytes(&buf_p, radd_p->site_name, 20);
    if ( fwrite(buf, 1, 300, out) != 300 ) {
	fprintf(stderr, "Could not write RADD block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_RADD_Print(struct Dorade_RADD *radd_p, FILE *out)
{
    int i;

    fprintf(out, "RADD:radar_name = %s\n", radd_p->radar_name);
    fprintf(out, "RADD:radar_const = %lf\n", radd_p->radar_const);
    fprintf(out, "RADD:peak_power = %lf\n", radd_p->peak_power);
    fprintf(out, "RADD:noise_power = %lf\n", radd_p->noise_power);
    fprintf(out, "RADD:receiver_gain = %lf\n", radd_p->receiver_gain);
    fprintf(out, "RADD:antenna_gain = %lf\n", radd_p->antenna_gain);
    fprintf(out, "RADD:system_gain = %lf\n", radd_p->system_gain);
    fprintf(out, "RADD:horz_beam_width = %lf\n", radd_p->horz_beam_width);
    fprintf(out, "RADD:vert_beam_width = %lf\n", radd_p->vert_beam_width);
    fprintf(out, "RADD:radar_type = %d\n", radd_p->radar_type);
    fprintf(out, "RADD:scan_mode = %d ", radd_p->scan_mode);
    switch (radd_p->scan_mode) {
	case 0:
	    fprintf(out, "Calibration\n");
	    break;
	case 1:
	    fprintf(out, "PPI (constant elevation)\n");
	    break;
	case 2:
	    fprintf(out, "Coplane\n");
	    break;
	case 3:
	    fprintf(out, "RHI (Constant azimuth)\n");
	    break;
	case 4:
	    fprintf(out, "Vertical Pointing\n");
	    break;
	case 5:
	    fprintf(out, "Target (Stationary)\n");
	    break;
	case 6:
	    fprintf(out, "Manual\n");
	    break;
	case 7:
	    fprintf(out, "Idle (out of control)\n");
	    break;
	default:
	    fprintf(out, "Unknown\n");
	    break;
    }
    fprintf(out, "RADD:req_rotat_vel = %lf\n", radd_p->req_rotat_vel);
    fprintf(out, "RADD:scan_mode_pram0 = %lf\n", radd_p->scan_mode_pram0);
    fprintf(out, "RADD:scan_mode_pram1 = %lf\n", radd_p->scan_mode_pram1);
    fprintf(out, "RADD:num_parms = %d\n", radd_p->num_parms);
    fprintf(out, "RADD:total_num_des = %d\n", radd_p->total_num_des);
    fprintf(out, "RADD:data_compress = %d\n", radd_p->data_compress);
    fprintf(out, "RADD:data_reduction = %d\n", radd_p->data_reduction);
    fprintf(out, "RADD:data_red_parm0 = %lf\n", radd_p->data_red_parm0);
    fprintf(out, "RADD:data_red_parm1 = %lf\n", radd_p->data_red_parm1);
    fprintf(out, "RADD:radar_longitude = %lf\n", radd_p->radar_longitude);
    fprintf(out, "RADD:radar_latitude = %lf\n", radd_p->radar_latitude);
    fprintf(out, "RADD:radar_altitude = %lf\n", radd_p->radar_altitude);
    fprintf(out, "RADD:eff_unamb_vel = %lf\n", radd_p->eff_unamb_vel);
    fprintf(out, "RADD:eff_unamb_range = %lf\n", radd_p->eff_unamb_range);
    fprintf(out, "RADD:num_freq_trans = %d\n", radd_p->num_freq_trans);
    fprintf(out, "RADD:num_ipps_trans = %d\n", radd_p->num_ipps_trans);
    fprintf(out, "RADD:freq1 = %lf\n", radd_p->freq1);
    fprintf(out, "RADD:freq2 = %lf\n", radd_p->freq2);
    fprintf(out, "RADD:freq3 = %lf\n", radd_p->freq3);
    fprintf(out, "RADD:freq4 = %lf\n", radd_p->freq4);
    fprintf(out, "RADD:freq5 = %lf\n", radd_p->freq5);
    fprintf(out, "RADD:interpulse_per1 = %lf\n", radd_p->interpulse_per1);
    fprintf(out, "RADD:interpulse_per2 = %lf\n", radd_p->interpulse_per2);
    fprintf(out, "RADD:interpulse_per3 = %lf\n", radd_p->interpulse_per3);
    fprintf(out, "RADD:interpulse_per4 = %lf\n", radd_p->interpulse_per4);
    fprintf(out, "RADD:interpulse_per5 = %lf\n", radd_p->interpulse_per5);
    fprintf(out, "RADD:extension_num = %d\n", radd_p->extension_num);
    fprintf(out, "RADD:config_name = %s\n", radd_p->config_name);
    fprintf(out, "RADD:config_num = %d\n", radd_p->config_num);
    fprintf(out, "RADD:aperture_size = %lf\n", radd_p->aperture_size);
    fprintf(out, "RADD:field_of_view = %lf\n", radd_p->field_of_view);
    fprintf(out, "RADD:aperture_eff = %lf\n", radd_p->aperture_eff);
    fprintf(out, "RADD:freq =");
    for (i = 0; i < 11; i++) {
	fprintf(out, " %lf", radd_p->freq[i]);
    }
    fprintf(out, "\n");
    fprintf(out, "RADD:interpulse_per =");
    for (i = 0; i < 11; i++) {
	fprintf(out, " %lf", radd_p->interpulse_per[i]);
    }
    fprintf(out, "\n");
    fprintf(out, "RADD:pulse_width = %lf\n", radd_p->pulse_width);
    fprintf(out, "RADD:primary_cop_baseln = %lf\n", radd_p->primary_cop_baseln);
    fprintf(out, "RADD:secondary_cop_baseln = %lf\n",
	    radd_p->secondary_cop_baseln);
    fprintf(out, "RADD:pc_xmtr_bandwidth = %lf\n", radd_p->pc_xmtr_bandwidth);
    fprintf(out, "RADD:pc_waveform_type = %d\n", radd_p->pc_waveform_type);
    fprintf(out, "RADD:site_name = %s\n", radd_p->site_name);
}

void Dorade_CFAC_Init(struct Dorade_CFAC *cfac_p)
{
    cfac_p->azimuth_corr = DORADE_BAD_F;
    cfac_p->elevation_corr = DORADE_BAD_F;
    cfac_p->range_delay_corr = DORADE_BAD_F;
    cfac_p->longitude_corr = DORADE_BAD_F;
    cfac_p->latitude_corr = DORADE_BAD_F;
    cfac_p->pressure_alt_corr = DORADE_BAD_F;
    cfac_p->radar_alt_corr = DORADE_BAD_F;
    cfac_p->ew_gndspd_corr = DORADE_BAD_F;
    cfac_p->ns_gndspd_corr = DORADE_BAD_F;
    cfac_p->vert_vel_corr = DORADE_BAD_F;
    cfac_p->heading_corr = DORADE_BAD_F;
    cfac_p->roll_corr = DORADE_BAD_F;
    cfac_p->pitch_corr = DORADE_BAD_F;
    cfac_p->drift_corr = DORADE_BAD_F;
    cfac_p->rot_angle_corr = DORADE_BAD_F;
    cfac_p->tilt_corr = DORADE_BAD_F;
}

int Dorade_CFAC_Read(struct Dorade_CFAC *cfac_p, char *buf)
{
    cfac_p->azimuth_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->elevation_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->range_delay_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->longitude_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->latitude_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->pressure_alt_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->radar_alt_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->ew_gndspd_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->ns_gndspd_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->vert_vel_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->heading_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->roll_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->pitch_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->drift_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->rot_angle_corr = ValBuf_GetF4BYT(&buf);
    cfac_p->tilt_corr = ValBuf_GetF4BYT(&buf);
    return 1;
}

int Dorade_CFAC_Write(struct Dorade_CFAC *cfac_p, FILE *out)
{
    char *blk_id = "CFAC";
    unsigned blk_len = 72;
    char buf[72], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutF4BYT(&buf_p, cfac_p->azimuth_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->elevation_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->range_delay_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->longitude_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->latitude_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->pressure_alt_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->radar_alt_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->ew_gndspd_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->ns_gndspd_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->vert_vel_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->heading_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->roll_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->pitch_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->drift_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->rot_angle_corr);
    ValBuf_PutF4BYT(&buf_p, cfac_p->tilt_corr);
    if ( fwrite(buf, 1, 72, out) != 72 ) {
	fprintf(stderr, "Could not write CFAC block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_CFAC_Print(struct Dorade_CFAC *cfac_p, FILE *out)
{
    fprintf(out, "CFAC:azimuth_corr = %f\n", cfac_p->azimuth_corr);
    fprintf(out, "CFAC:elevation_corr = %f\n", cfac_p->elevation_corr);
    fprintf(out, "CFAC:range_delay_corr = %f\n", cfac_p->range_delay_corr);
    fprintf(out, "CFAC:longitude_corr = %f\n", cfac_p->longitude_corr);
    fprintf(out, "CFAC:latitude_corr = %f\n", cfac_p->latitude_corr);
    fprintf(out, "CFAC:pressure_alt_corr = %f\n", cfac_p->pressure_alt_corr);
    fprintf(out, "CFAC:radar_alt_corr = %f\n", cfac_p->radar_alt_corr);
    fprintf(out, "CFAC:ew_gndspd_corr = %f\n", cfac_p->ew_gndspd_corr);
    fprintf(out, "CFAC:ns_gndspd_corr = %f\n", cfac_p->ns_gndspd_corr);
    fprintf(out, "CFAC:vert_vel_corr = %f\n", cfac_p->vert_vel_corr);
    fprintf(out, "CFAC:heading_corr = %f\n", cfac_p->heading_corr);
    fprintf(out, "CFAC:roll_corr = %f\n", cfac_p->roll_corr);
    fprintf(out, "CFAC:pitch_corr = %f\n", cfac_p->pitch_corr);
    fprintf(out, "CFAC:drift_corr = %f\n", cfac_p->drift_corr);
    fprintf(out, "CFAC:rot_angle_corr = %f\n", cfac_p->rot_angle_corr);
    fprintf(out, "CFAC:tilt_corr = %f\n", cfac_p->tilt_corr);
}

void Dorade_PARM_Init(struct Dorade_PARM *parm_p)
{
    memset(parm_p->parm_nm, 0, sizeof(parm_p->parm_nm));
    memset(parm_p->parm_description, 0, sizeof(parm_p->parm_description));
    memset(parm_p->parm_units, 0, sizeof(parm_p->parm_units));
    parm_p->interpulse_time = DORADE_BAD_I2;
    parm_p->xmitted_freq = DORADE_BAD_I2;
    parm_p->recvr_bandwidth = DORADE_BAD_F;
    parm_p->pulse_width = DORADE_BAD_I2;
    parm_p->polarization = DORADE_BAD_I2;
    parm_p->num_samples = DORADE_BAD_I2;
    parm_p->binary_format = DORADE_BAD_I2;
    memset(parm_p->threshold_field, 0, sizeof(parm_p->threshold_field));
    parm_p->threshold_value = DORADE_BAD_F;
    parm_p->parameter_scale = DORADE_BAD_F;
    parm_p->parameter_bias = DORADE_BAD_F;
    parm_p->bad_data = DORADE_BAD_I2;
    parm_p->extension_num = DORADE_BAD_I4;
    memset(parm_p->config_name, 0, sizeof(parm_p->config_name));
    parm_p->config_num = DORADE_BAD_I4;
    parm_p->offset_to_data = DORADE_BAD_I4;
    parm_p->mks_conversion = DORADE_BAD_F;
    parm_p->num_qnames = DORADE_BAD_I4;
    memset(parm_p->qdata_names, 0, sizeof(parm_p->qdata_names));
    parm_p->num_criteria = DORADE_BAD_I4;
    memset(parm_p->criteria_names, 0, sizeof(parm_p->criteria_names));
    parm_p->num_cells = DORADE_BAD_I4;
    parm_p->meters_to_first_cell = DORADE_BAD_F;
    parm_p->meters_between_cells = DORADE_BAD_F;
    parm_p->eff_unamb_vel = DORADE_BAD_F;
    parm_p->next = NULL;
}

int Dorade_PARM_Read(struct Dorade_PARM *parm_p, char *buf)
{
    ValBuf_GetBytes(&buf, parm_p->parm_nm, 8);
    parm_p->parm_nm[8] = '\0';
    ValBuf_GetBytes(&buf, parm_p->parm_description, 40);
    parm_p->parm_description[40] = '\0';
    ValBuf_GetBytes(&buf, parm_p->parm_units, 8);
    parm_p->parm_units[8] = '\0';
    parm_p->interpulse_time = ValBuf_GetI2BYT(&buf);
    parm_p->xmitted_freq = ValBuf_GetI2BYT(&buf);
    parm_p->recvr_bandwidth = ValBuf_GetF4BYT(&buf);
    parm_p->pulse_width = ValBuf_GetI2BYT(&buf);
    parm_p->polarization = ValBuf_GetI2BYT(&buf);
    parm_p->num_samples = ValBuf_GetI2BYT(&buf);
    parm_p->binary_format = ValBuf_GetI2BYT(&buf);
    ValBuf_GetBytes(&buf, parm_p->threshold_field, 8);
    parm_p->threshold_value = ValBuf_GetF4BYT(&buf);
    parm_p->parameter_scale = ValBuf_GetF4BYT(&buf);
    parm_p->parameter_bias = ValBuf_GetF4BYT(&buf);
    parm_p->bad_data = ValBuf_GetI4BYT(&buf);
    parm_p->extension_num = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, parm_p->config_name, 8);
    parm_p->config_num = ValBuf_GetI4BYT(&buf);
    parm_p->offset_to_data = ValBuf_GetI4BYT(&buf);
    parm_p->mks_conversion = ValBuf_GetF4BYT(&buf);
    parm_p->num_qnames = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, parm_p->qdata_names, 32);
    parm_p->num_criteria = ValBuf_GetI4BYT(&buf);
    ValBuf_GetBytes(&buf, parm_p->criteria_names, 32);
    parm_p->num_cells = ValBuf_GetI4BYT(&buf);
    parm_p->meters_to_first_cell = ValBuf_GetF4BYT(&buf);
    parm_p->meters_between_cells = ValBuf_GetF4BYT(&buf);
    parm_p->eff_unamb_vel = ValBuf_GetF4BYT(&buf);
    return 1;
}

int Dorade_PARM_Write(struct Dorade_PARM *parm_p, FILE *out)
{
    char *blk_id = "PARM";
    unsigned blk_len = 216;
    char buf[216], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutBytes(&buf_p, parm_p->parm_nm, 8);
    ValBuf_PutBytes(&buf_p, parm_p->parm_description, 40);
    ValBuf_PutBytes(&buf_p, parm_p->parm_units, 8);
    ValBuf_PutI2BYT(&buf_p, parm_p->interpulse_time);
    ValBuf_PutI2BYT(&buf_p, parm_p->xmitted_freq);
    ValBuf_PutF4BYT(&buf_p, parm_p->recvr_bandwidth);
    ValBuf_PutI2BYT(&buf_p, parm_p->pulse_width);
    ValBuf_PutI2BYT(&buf_p, parm_p->polarization);
    ValBuf_PutI2BYT(&buf_p, parm_p->num_samples);
    ValBuf_PutI2BYT(&buf_p, parm_p->binary_format);
    ValBuf_PutBytes(&buf_p, parm_p->threshold_field, 8);
    ValBuf_PutF4BYT(&buf_p, parm_p->threshold_value);
    ValBuf_PutF4BYT(&buf_p, parm_p->parameter_scale);
    ValBuf_PutF4BYT(&buf_p, parm_p->parameter_bias);
    ValBuf_PutI4BYT(&buf_p, parm_p->bad_data);
    ValBuf_PutI4BYT(&buf_p, parm_p->extension_num);
    ValBuf_PutBytes(&buf_p, parm_p->config_name, 8);
    ValBuf_PutI4BYT(&buf_p, parm_p->config_num);
    ValBuf_PutI4BYT(&buf_p, parm_p->offset_to_data);
    ValBuf_PutF4BYT(&buf_p, parm_p->mks_conversion);
    ValBuf_PutI4BYT(&buf_p, parm_p->num_qnames);
    ValBuf_PutBytes(&buf_p, parm_p->qdata_names, 32);
    ValBuf_PutI4BYT(&buf_p, parm_p->num_criteria);
    ValBuf_PutBytes(&buf_p, parm_p->criteria_names, 32);
    ValBuf_PutI4BYT(&buf_p, parm_p->num_cells);
    ValBuf_PutF4BYT(&buf_p, parm_p->meters_to_first_cell);
    ValBuf_PutF4BYT(&buf_p, parm_p->meters_between_cells);
    ValBuf_PutF4BYT(&buf_p, parm_p->eff_unamb_vel);
    if ( fwrite(buf, 1, 216, out) != 216 ) {
	fprintf(stderr, "Could not write PARM block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_PARM_Print(struct Dorade_PARM *parm_p, int p, FILE *out)
{
    p = p + 1;
    fprintf(out, "PARM(%d):parameter_name = %s\n", 
	    p, parm_p->parm_nm);
    fprintf(out, "PARM(%d):parm_description = %s\n", 
	    p, parm_p->parm_description);
    fprintf(out, "PARM(%d):parm_units = %s\n", 
	    p, parm_p->parm_units);
    fprintf(out, "PARM(%d):interpulse_time = %d\n", 
	    p, parm_p->interpulse_time);
    fprintf(out, "PARM(%d):xmitted_freq = %d\n", 
	    p, parm_p->xmitted_freq);
    fprintf(out, "PARM(%d):recvr_bandwidth = %lf\n", 
	    p, parm_p->recvr_bandwidth);
    fprintf(out, "PARM(%d):pulse_width = %d\n", 
	    p, parm_p->pulse_width);
    fprintf(out, "PARM(%d):polarization = %d\n", 
	    p, parm_p->polarization);
    fprintf(out, "PARM(%d):num_samples = %d\n", 
	    p, parm_p->num_samples);
    fprintf(out, "PARM(%d):binary_format = %d\n", 
	    p, parm_p->binary_format);
    fprintf(out, "PARM(%d):threshold_field = %s\n", 
	    p, parm_p->threshold_field);
    fprintf(out, "PARM(%d):threshold_value = %lf\n", 
	    p, parm_p->threshold_value);
    fprintf(out, "PARM(%d):parameter_scale = %lf\n", 
	    p, parm_p->parameter_scale);
    fprintf(out, "PARM(%d):parameter_bias = %lf\n", 
	    p, parm_p->parameter_bias);
    fprintf(out, "PARM(%d):bad_data = %d\n", 
	    p, parm_p->bad_data);
    fprintf(out, "PARM(%d):extension_num = %d\n", 
	    p, parm_p->extension_num);
    fprintf(out, "PARM(%d):config_name = %s\n", 
	    p, parm_p->config_name);
    fprintf(out, "PARM(%d):config_num = %d\n", 
	    p, parm_p->config_num);
    fprintf(out, "PARM(%d):offset_to_data = %d\n", 
	    p, parm_p->offset_to_data);
    fprintf(out, "PARM(%d):mks_conversion = %lf\n", 
	    p, parm_p->mks_conversion);
    fprintf(out, "PARM(%d):num_qnames = %d\n", 
	    p, parm_p->num_qnames);
    fprintf(out, "PARM(%d):qdata_names = %s\n", 
	    p, parm_p->qdata_names);
    fprintf(out, "PARM(%d):num_criteria = %d\n", 
	    p, parm_p->num_criteria);
    fprintf(out, "PARM(%d):criteria_names = %s\n", 
	    p, parm_p->criteria_names);
    fprintf(out, "PARM(%d):num_cells = %d\n", 
	    p, parm_p->num_cells);
    fprintf(out, "PARM(%d):meters_to_first_cell = %lf\n", 
	    p, parm_p->meters_to_first_cell);
    fprintf(out, "PARM(%d):meters_between_cells = %lf\n", 
	    p, parm_p->meters_between_cells);
    fprintf(out, "PARM(%d):eff_unamb_vel = %lf\n", 
	    p, parm_p->eff_unamb_vel);
}

void Dorade_CELV_Init(struct Dorade_CELV *celv_p)
{
    celv_p->num_cells = DORADE_BAD_I4;
    celv_p->dist_cells = NULL;
}

int Dorade_CELV_Read(struct Dorade_CELV *celv_p, char *buf)
{
    int num_cells;
    float *dist_cells, *d;

    num_cells = ValBuf_GetI4BYT(&buf);
    if ( !(dist_cells = CALLOC(num_cells, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate memory for cell vector.\n");
	return 0;
    }
    for (d = dist_cells ; d < dist_cells + num_cells; d++) {
	*d = ValBuf_GetF4BYT(&buf);
    }
    celv_p->num_cells = num_cells;
    celv_p->dist_cells = dist_cells;
    return 1;
}

int Dorade_CELV_Write(struct Dorade_CELV *celv_p, FILE *out)
{
    char *blk_id = "CELV";
    unsigned blk_len;
    int i;
    char *buf, *buf_p;

    blk_len = 4 + 4 + 4 + 4 * celv_p->num_cells;
    if ( !(buf = CALLOC(blk_len, 1)) ) {
	fprintf(stderr, "Could not allocate memory for output buffer when"
		" writing cell vector (CELV)\n");
	return 0;
    }
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutI4BYT(&buf_p, celv_p->num_cells);
    for (i = 0; i < celv_p->num_cells; i++) {
	ValBuf_PutF4BYT(&buf_p, celv_p->dist_cells[i]);
    }
    if ( fwrite(buf, 1, blk_len, out) != blk_len ) {
	fprintf(stderr, "Could not write CELV block.\n%s\n", strerror(errno));
	return 0;
    }
    FREE(buf);
    return 1;
}

void Dorade_CELV_Print(struct Dorade_CELV *celv_p, FILE *out)
{
    int c;

    fprintf(out, "CELV:num_cells = %d\n", celv_p->num_cells);
    fprintf(out, "CELV:dist_cells = ");
    for (c = 0; c < celv_p->num_cells; c++) {
	fprintf(out, "%.2f ", celv_p->dist_cells[c]);
    }
    fprintf(out, "\n");
}

void Dorade_CSFD_Init(struct Dorade_CSFD *csfd_p)
{
    int i;

    csfd_p->num_segments = DORADE_BAD_I4;
    csfd_p->dist_to_first = DORADE_BAD_F;
    for (i = 0; i < 8; i++) {
	csfd_p->spacing[i] = DORADE_BAD_F;
	csfd_p->num_cells[i] = 0;
    }
}

int Dorade_CSFD_Read(struct Dorade_CSFD *csfd_p, char *buf)
{
    int i;

    csfd_p->num_segments = ValBuf_GetI4BYT(&buf);
    csfd_p->dist_to_first = ValBuf_GetF4BYT(&buf);
    for (i = 0; i < 8; i++) {
	csfd_p->spacing[i] = ValBuf_GetF4BYT(&buf);
    }
    for (i = 0; i < 8; i++) {
	csfd_p->num_cells[i] = ValBuf_GetI2BYT(&buf);
    }
    return 1;
}

int Dorade_CSFD_Write(struct Dorade_CSFD *csfd_p, FILE *out)
{
    char *blk_id = "CSFD";
    unsigned blk_len = 64;
    int i;
    char buf[64], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutI4BYT(&buf_p, csfd_p->num_segments);
    ValBuf_PutF4BYT(&buf_p, csfd_p->dist_to_first);
    for (i = 0; i < 8; i++) {
	ValBuf_PutF4BYT(&buf_p, csfd_p->spacing[i]);
    }
    for (i = 0; i < 8; i++) {
	ValBuf_PutI2BYT(&buf_p, csfd_p->num_cells[i]);
    }
    if ( fwrite(buf, 1, 64, out) != 64 ) {
	fprintf(stderr, "Could not write CSFD block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_CSFD_Print(struct Dorade_CSFD *csfd_p, FILE *out)
{
    int s, c;
    float d;

    fprintf(out, "CSFD:num_segments = %d\n", csfd_p->num_segments);
    for (d = csfd_p->dist_to_first, s = 0; s < csfd_p->num_segments; s++) {
	for (c = 0; c < csfd_p->num_cells[s]; c++) {
	    fprintf(out, "CSFD(%d,%d): %f ", s, c, d);
	    d += csfd_p->spacing[s];
	}
    }
    fprintf(out, "\n");
}

void Dorade_SWIB_Init(struct Dorade_SWIB *swib_p)
{
    memset(swib_p->radar_name, 0, sizeof(swib_p->radar_name));
    swib_p->sweep_num = DORADE_BAD_I4;
    swib_p->num_rays = DORADE_BAD_I4;
    swib_p->start_angle = DORADE_BAD_F;
    swib_p->stop_angle = DORADE_BAD_F;
    swib_p->fixed_angle = DORADE_BAD_F;
    swib_p->filter_flag = DORADE_BAD_I4;
}

int Dorade_SWIB_Read(struct Dorade_SWIB *swib_p, char *buf)
{
    ValBuf_GetBytes(&buf, swib_p->radar_name, 8);
    swib_p->sweep_num = ValBuf_GetI4BYT(&buf);
    swib_p->num_rays = ValBuf_GetI4BYT(&buf);
    swib_p->start_angle = ValBuf_GetF4BYT(&buf);
    swib_p->stop_angle = ValBuf_GetF4BYT(&buf);
    swib_p->fixed_angle = ValBuf_GetF4BYT(&buf);
    swib_p->filter_flag = ValBuf_GetI4BYT(&buf);
    return 1;
}

int Dorade_SWIB_Write(struct Dorade_SWIB *swib_p, FILE *out)
{
    char *blk_id = "SWIB";
    unsigned blk_len = 40;
    char buf[40], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutBytes(&buf_p, swib_p->radar_name, 8);
    ValBuf_PutI4BYT(&buf_p, swib_p->sweep_num);
    ValBuf_PutI4BYT(&buf_p, swib_p->num_rays);
    ValBuf_PutF4BYT(&buf_p, swib_p->start_angle);
    ValBuf_PutF4BYT(&buf_p, swib_p->stop_angle);
    ValBuf_PutF4BYT(&buf_p, swib_p->fixed_angle);
    ValBuf_PutI4BYT(&buf_p, swib_p->filter_flag);
    if ( fwrite(buf, 1, 40, out) != 40 ) {
	fprintf(stderr, "Could not write SSIB block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_SWIB_Print(struct Dorade_SWIB *swib_p, FILE *out)
{
    fprintf(out, "SWIB:radar_name = %s\n", swib_p->radar_name);
    fprintf(out, "SWIB:sweep_num = %d\n", swib_p->sweep_num);
    fprintf(out, "SWIB:num_rays = %d\n", swib_p->num_rays);
    fprintf(out, "SWIB:start_angle = %lf\n", swib_p->start_angle);
    fprintf(out, "SWIB:stop_angle = %lf\n", swib_p->stop_angle);
    fprintf(out, "SWIB:fixed_angle = %lf\n", swib_p->fixed_angle);
    fprintf(out, "SWIB:filter_flag = %d\n", swib_p->filter_flag);
}

void Dorade_ASIB_Init(struct Dorade_ASIB *asib_p)
{
    asib_p->longitude = DORADE_BAD_F;
    asib_p->latitude = DORADE_BAD_F;
    asib_p->altitude_msl = DORADE_BAD_F;
    asib_p->altitude_agl = DORADE_BAD_F;
    asib_p->ew_velocity = DORADE_BAD_F;
    asib_p->ns_velocity = DORADE_BAD_F;
    asib_p->vert_velocity = DORADE_BAD_F;
    asib_p->heading = DORADE_BAD_F;
    asib_p->roll = DORADE_BAD_F;
    asib_p->pitch = DORADE_BAD_F;
    asib_p->drift_angle = DORADE_BAD_F;
    asib_p->rotation_angle = DORADE_BAD_F;
    asib_p->tilt = DORADE_BAD_F;
    asib_p->ew_horiz_wind = DORADE_BAD_F;
    asib_p->ns_horiz_wind = DORADE_BAD_F;
    asib_p->vert_wind = DORADE_BAD_F;
    asib_p->heading_change = DORADE_BAD_F;
    asib_p->pitch_change = DORADE_BAD_F;
}

int Dorade_ASIB_Read(struct Dorade_ASIB *asib_p, char *buf)
{
    asib_p->longitude = ValBuf_GetF4BYT(&buf);
    asib_p->latitude = ValBuf_GetF4BYT(&buf);
    asib_p->altitude_msl = ValBuf_GetF4BYT(&buf);
    asib_p->altitude_agl = ValBuf_GetF4BYT(&buf);
    asib_p->ew_velocity = ValBuf_GetF4BYT(&buf);
    asib_p->ns_velocity = ValBuf_GetF4BYT(&buf);
    asib_p->vert_velocity = ValBuf_GetF4BYT(&buf);
    asib_p->heading = ValBuf_GetF4BYT(&buf);
    asib_p->roll = ValBuf_GetF4BYT(&buf);
    asib_p->pitch = ValBuf_GetF4BYT(&buf);
    asib_p->drift_angle = ValBuf_GetF4BYT(&buf);
    asib_p->rotation_angle = ValBuf_GetF4BYT(&buf);
    asib_p->tilt = ValBuf_GetF4BYT(&buf);
    asib_p->ew_horiz_wind = ValBuf_GetF4BYT(&buf);
    asib_p->ns_horiz_wind = ValBuf_GetF4BYT(&buf);
    asib_p->vert_wind = ValBuf_GetF4BYT(&buf);
    asib_p->heading_change = ValBuf_GetF4BYT(&buf);
    asib_p->pitch_change = ValBuf_GetF4BYT(&buf);
    return 1;
}

int Dorade_ASIB_Write(struct Dorade_ASIB *asib_p, FILE *out)
{
    char *blk_id = "ASIB";
    unsigned blk_len = 80;
    char buf[80], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutF4BYT(&buf_p, asib_p->longitude);
    ValBuf_PutF4BYT(&buf_p, asib_p->latitude);
    ValBuf_PutF4BYT(&buf_p, asib_p->altitude_msl);
    ValBuf_PutF4BYT(&buf_p, asib_p->altitude_agl);
    ValBuf_PutF4BYT(&buf_p, asib_p->ew_velocity);
    ValBuf_PutF4BYT(&buf_p, asib_p->ns_velocity);
    ValBuf_PutF4BYT(&buf_p, asib_p->vert_velocity);
    ValBuf_PutF4BYT(&buf_p, asib_p->heading);
    ValBuf_PutF4BYT(&buf_p, asib_p->roll);
    ValBuf_PutF4BYT(&buf_p, asib_p->pitch);
    ValBuf_PutF4BYT(&buf_p, asib_p->drift_angle);
    ValBuf_PutF4BYT(&buf_p, asib_p->rotation_angle);
    ValBuf_PutF4BYT(&buf_p, asib_p->tilt);
    ValBuf_PutF4BYT(&buf_p, asib_p->ew_horiz_wind);
    ValBuf_PutF4BYT(&buf_p, asib_p->ns_horiz_wind);
    ValBuf_PutF4BYT(&buf_p, asib_p->vert_wind);
    ValBuf_PutF4BYT(&buf_p, asib_p->heading_change);
    ValBuf_PutF4BYT(&buf_p, asib_p->pitch_change);
    if ( fwrite(buf, 1, 80, out) != 80 ) {
	fprintf(stderr, "Could not write ASIB block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_ASIB_Print(struct Dorade_ASIB *asib_p, int r, FILE *out)
{
    r = r + 1;
    fprintf(out, "ASIB(%d):longitude = %lf\n", 
	    r, asib_p->longitude);
    fprintf(out, "ASIB(%d):latitude = %lf\n", 
	    r, asib_p->latitude);
    fprintf(out, "ASIB(%d):altitude_msl = %lf\n", 
	    r, asib_p->altitude_msl);
    fprintf(out, "ASIB(%d):altitude_agl = %lf\n", 
	    r, asib_p->altitude_agl);
    fprintf(out, "ASIB(%d):ew_velocity = %lf\n", 
	    r, asib_p->ew_velocity);
    fprintf(out, "ASIB(%d):ns_velocity = %lf\n", 
	    r, asib_p->ns_velocity);
    fprintf(out, "ASIB(%d):vert_velocity = %lf\n", 
	    r, asib_p->vert_velocity);
    fprintf(out, "ASIB(%d):heading = %lf\n", 
	    r, asib_p->heading);
    fprintf(out, "ASIB(%d):roll = %lf\n", 
	    r, asib_p->roll);
    fprintf(out, "ASIB(%d):pitch = %lf\n", 
	    r, asib_p->pitch);
    fprintf(out, "ASIB(%d):drift_angle = %lf\n", 
	    r, asib_p->drift_angle);
    fprintf(out, "ASIB(%d):rotation_angle = %lf\n", 
	    r, asib_p->rotation_angle);
    fprintf(out, "ASIB(%d):tilt = %lf\n", 
	    r, asib_p->tilt);
    fprintf(out, "ASIB(%d):ew_horiz_wind = %lf\n", 
	    r, asib_p->ew_horiz_wind);
    fprintf(out, "ASIB(%d):ns_horiz_wind = %lf\n", 
	    r, asib_p->ns_horiz_wind);
    fprintf(out, "ASIB(%d):vert_wind = %lf\n", 
	    r, asib_p->vert_wind);
    fprintf(out, "ASIB(%d):heading_change = %lf\n", 
	    r, asib_p->heading_change);
    fprintf(out, "ASIB(%d):pitch_change = %lf\n", 
	    r, asib_p->pitch_change);
}

void Dorade_RYIB_Init(struct Dorade_RYIB *ryib_p)
{
    ryib_p->sweep_num = DORADE_BAD_I4;
    ryib_p->julian_day = DORADE_BAD_I4;
    ryib_p->hour = DORADE_BAD_I2;
    ryib_p->minute = DORADE_BAD_I2;
    ryib_p->second = DORADE_BAD_I2;
    ryib_p->millisecond = DORADE_BAD_I2;
    ryib_p->azimuth = DORADE_BAD_F;
    ryib_p->elevation = DORADE_BAD_F;
    ryib_p->peak_power = DORADE_BAD_F;
    ryib_p->true_scan_rate = DORADE_BAD_F;
    ryib_p->ray_status = DORADE_BAD_I4;
}

int Dorade_RYIB_Read(struct Dorade_RYIB *ryib_p, char *buf)
{
    ryib_p->sweep_num = ValBuf_GetI4BYT(&buf);
    ryib_p->julian_day = ValBuf_GetI4BYT(&buf);
    ryib_p->hour = ValBuf_GetI2BYT(&buf);
    ryib_p->minute = ValBuf_GetI2BYT(&buf);
    ryib_p->second = ValBuf_GetI2BYT(&buf);
    ryib_p->millisecond = ValBuf_GetI2BYT(&buf);
    ryib_p->azimuth = ValBuf_GetF4BYT(&buf);
    ryib_p->elevation = ValBuf_GetF4BYT(&buf);
    ryib_p->peak_power = ValBuf_GetF4BYT(&buf);
    ryib_p->true_scan_rate = ValBuf_GetF4BYT(&buf);
    ryib_p->ray_status = ValBuf_GetI4BYT(&buf);
    return 1;
}

int Dorade_RYIB_Write(struct Dorade_RYIB *ryib_p, FILE *out)
{
    char *blk_id = "RYIB";
    unsigned blk_len = 44;
    char buf[44], *buf_p;

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    ValBuf_PutBytes(&buf_p, blk_id, 4);
    ValBuf_PutI4BYT(&buf_p, blk_len);
    ValBuf_PutI4BYT(&buf_p, ryib_p->sweep_num);
    ValBuf_PutI4BYT(&buf_p, ryib_p->julian_day);
    ValBuf_PutI2BYT(&buf_p, ryib_p->hour);
    ValBuf_PutI2BYT(&buf_p, ryib_p->minute);
    ValBuf_PutI2BYT(&buf_p, ryib_p->second);
    ValBuf_PutI2BYT(&buf_p, ryib_p->millisecond);
    ValBuf_PutF4BYT(&buf_p, ryib_p->azimuth);
    ValBuf_PutF4BYT(&buf_p, ryib_p->elevation);
    ValBuf_PutF4BYT(&buf_p, ryib_p->peak_power);
    ValBuf_PutF4BYT(&buf_p, ryib_p->true_scan_rate);
    ValBuf_PutI4BYT(&buf_p, ryib_p->ray_status);
    if ( fwrite(buf, 1, 44, out) != 44 ) {
	fprintf(stderr, "Could not write RYIB block.\n%s\n", strerror(errno));
	return 0;
    }
    return 1;
}

void Dorade_RYIB_Print(struct Dorade_RYIB *ryib_p, int r, FILE *out)
{
    r = r + 1;
    fprintf(out, "RYIB(%d):sweep_num = %d\n", 
	    r, ryib_p->sweep_num);
    fprintf(out, "RYIB(%d):julian_day = %d\n", 
	    r, ryib_p->julian_day);
    fprintf(out, "RYIB(%d):hour = %d\n", 
	    r, ryib_p->hour);
    fprintf(out, "RYIB(%d):minute = %d\n", 
	    r, ryib_p->minute);
    fprintf(out, "RYIB(%d):second = %d\n", 
	    r, ryib_p->second);
    fprintf(out, "RYIB(%d):millisecond = %d\n", 
	    r, ryib_p->millisecond);
    fprintf(out, "RYIB(%d):azimuth = %lf\n", 
	    r, ryib_p->azimuth);
    fprintf(out, "RYIB(%d):elevation = %lf\n", 
	    r, ryib_p->elevation);
    fprintf(out, "RYIB(%d):peak_power = %lf\n", 
	    r, ryib_p->peak_power);
    fprintf(out, "RYIB(%d):true_scan_rate = %lf\n", 
	    r, ryib_p->true_scan_rate);
    fprintf(out, "RYIB(%d):ray_status = %d\n", 
	    r, ryib_p->ray_status);
}

void Dorade_Sensor_Init(struct Dorade_Sensor *sensor_p)
{
    int p;

    Dorade_RADD_Init(&sensor_p->radd);
    for (p = 0; p < DORADE_MAX_PARMS; p++) {
	Dorade_PARM_Init(sensor_p->parms + p);
    }
    sensor_p->parm0 = NULL;
    sensor_p->cell_geo_t = cell_geo;		/* Default, static variable */
    switch (cell_geo) {
	case CG_CELV:
	    Dorade_CELV_Init(&sensor_p->cell_geo.celv);
	    break;
	case CG_CSFD:
	    Dorade_CSFD_Init(&sensor_p->cell_geo.csfd);
	    break;
    }
    Dorade_CFAC_Init(&sensor_p->cfac);
}

/* 
   There is no Dorade_Sensor_Read function. Sensor is assembled as blocks for
   it are found in input.
 */

int Dorade_Sensor_Write(struct Dorade_Sweep *swp_p, FILE *out)
{
    struct Dorade_PARM *parm_p;
    struct Dorade_Sensor *sensor_p = &swp_p->sensor;

    if ( !Dorade_RADD_Write(&sensor_p->radd, out) ) {
	return 0;
    }
    for (parm_p = Dorade_NextParm(swp_p, NULL);
	    parm_p;
	    parm_p = Dorade_NextParm(swp_p, parm_p)) {
	if ( !Dorade_PARM_Write(parm_p, out) ) {
	    return 0;
	}
    }
    switch (sensor_p->cell_geo_t) {
	case CG_CELV:
	    if ( !Dorade_CELV_Write(&sensor_p->cell_geo.celv, out) ) {
		return 0;
	    }
	    break;
	case CG_CSFD:
	    if ( !Dorade_CSFD_Write(&sensor_p->cell_geo.csfd, out) ) {
		return 0;
	    }
	    break;
    }
    if ( !Dorade_CFAC_Write(&sensor_p->cfac, out) ) {
	return 0;
    }
    return 1;
}

void Dorade_Sensor_Print(struct Dorade_Sweep *swp_p, FILE *out)
{
    struct Dorade_Sensor *sensor_p = &swp_p->sensor;
    struct Dorade_PARM *parm_p;
    int p;

    Dorade_RADD_Print(&sensor_p->radd, out);
    for (parm_p = Dorade_NextParm(swp_p, NULL), p = 0;
	    parm_p;
	    parm_p = Dorade_NextParm(swp_p, parm_p), p++) {
	Dorade_PARM_Print(parm_p, p, out);
    }
    switch (sensor_p->cell_geo_t) {
	case CG_CELV:
	    Dorade_CELV_Print(&sensor_p->cell_geo.celv, out);
	    break;
	case CG_CSFD:
	    Dorade_CSFD_Print(&sensor_p->cell_geo.csfd, out);
	    break;
    }
    Dorade_CFAC_Print(&sensor_p->cfac, out);
}

void Dorade_Ray_Hdr_Init(struct Dorade_Ray_Hdr *ray_hdr_p)
{
    Dorade_RYIB_Init(&ray_hdr_p->ryib);
    Dorade_ASIB_Init(&ray_hdr_p->asib);
}

/*
   There is no Dorade_Ray_Hdr_Read function. Sensor is assembled as blocks for
   it are found in input.
 */

int Dorade_Ray_Hdr_Write(struct Dorade_Ray_Hdr *ray_hdr_p, FILE *out)
{
    if ( !(Dorade_RYIB_Write(&ray_hdr_p->ryib, out)
		&& Dorade_ASIB_Write(&ray_hdr_p->asib, out)) ) {
	return 0;
    }
    return 1;
}

void Dorade_Ray_Hdr_Print(struct Dorade_Ray_Hdr *ray_hdr_p, int r, FILE *out)
{
    Dorade_RYIB_Print(&ray_hdr_p->ryib, r, out);
    Dorade_ASIB_Print(&ray_hdr_p->asib, r, out);
}

void Dorade_Sweep_Init(struct Dorade_Sweep *swp_p)
{
    int p;

    swp_p->swp_fl_nm = NULL;
    Dorade_COMM_Init(&swp_p->comm);
    Dorade_SSWB_Init(&swp_p->sswb);
    Dorade_VOLD_Init(&swp_p->vold);
    Dorade_Sensor_Init(&swp_p->sensor);
    Dorade_SWIB_Init(&swp_p->swib);
    swp_p->ray_hdr = NULL;
    for (p = 0; p < DORADE_MAX_PARMS; p++) {
	swp_p->dat[p] = NULL;
    }
    swp_p->mod = 0;
}

int Dorade_Sweep_Read(struct Dorade_Sweep *swp_p, FILE *in)
{
    char blk_id[5] = {'\0'};
    int done_reading;			/* If true, done with file */
    int read;				/* If true, have read at least one
					   block from sweep file. */
    enum BLOCK_TYPE blk_typ;		/* Block type, see enumerator above */
    int blk_len;			/* Block length */
    int blk_len8;			/* Block length less storage for
					   blk_id and blk_len */
    char *buf = NULL;			/* Input buffer */
    int buf_sz = 0;			/* Allocation at buf */
    char *buf_p;			/* Point into buf */
    int p, r = -1, c;			/* Parameter, ray, cell index */
    struct Dorade_PARM *parm_p = NULL;	/* Current parameter being read in */
    struct Dorade_PARM parm;		/* Hold contents of parm_p */
    struct Dorade_PARM *prev_parm = NULL; /* Last parameter read in. This
					     becomes the "next" member of
					     the next parameter read in,
					     created a linked list
					     recording the order in which
					     parameters were read from the
					     sweep file. */
    int num_parms, num_rays, num_cells;	/* Dimensions */
    int num_rays_t;			/* Number of rays read in so far */
    double scale_inv, bias;		/* Constants to convert input to
					   measurements */
    char parm_nm[9];			/* Parameter name from RDAT */
    float *dat;				/* Data for one ray */
    float *dp;				/* Point into dat */
    I1BYT *cp;				/* 1 byte integer input */
    I2BYT s;				/* 2 byte integer input */
    I4BYT *ip;				/* 4 byte integer input */
    F4BYT *dp_in;			/* 4 byte floating point input */
    F4BYT *dp1;				/* Loop parameter */
    int cnt;				/* Number of values in a run of data */

    /* Initialize input buffer */
    buf_sz = comm_sz;
    buf_sz = (sswb_sz > buf_sz) ? sswb_sz : buf_sz;
    buf_sz = (vold_sz > buf_sz) ? vold_sz : buf_sz;
    buf_sz = (radd_sz > buf_sz) ? radd_sz : buf_sz;
    buf_sz = (cfac_sz > buf_sz) ? cfac_sz : buf_sz;
    buf_sz = (parm_sz > buf_sz) ? parm_sz : buf_sz;
    buf_sz = (celv_sz > buf_sz) ? celv_sz : buf_sz;
    buf_sz = (csfd_sz > buf_sz) ? csfd_sz : buf_sz;
    buf_sz = (swib_sz > buf_sz) ? swib_sz : buf_sz;
    buf_sz = (asib_sz > buf_sz) ? asib_sz : buf_sz;
    buf_sz = (ryib_sz > buf_sz) ? ryib_sz : buf_sz;
    buf_sz = (rdat_sz > buf_sz) ? rdat_sz : buf_sz;
    buf_sz = (rktb_sz > buf_sz) ? rktb_sz : buf_sz;
    buf_sz = (null_sz > buf_sz) ? null_sz : buf_sz;
    num_parms = num_rays = num_cells = -1;
    if ( !(buf = CALLOC(buf_sz, 1)) ) {
	fprintf(stderr, "Could not allocate 4 bytes for input buffer.\n");
	goto error;
    }

    /* Read block identifers until no more input or no more sweep */
    for (read = done_reading = num_rays_t = 0;
	    !done_reading && fread(blk_id, 4, 1, in) == 1; ) {
	blk_typ = Hash(blk_id, N_BUCKETS);
	if (strcmp(block_id[blk_typ], blk_id) != 0) {
	    fprintf(stderr, "Warning: Unknown block type: %4.4s\n", blk_id);
	}

	/* Read block size */
	if ( !lfread(buf, 4, 1, in, "integer") ) {
	    fprintf(stderr, "Could not get block size for %s header.\n", blk_id);
	    goto error;
	}
	buf_p = buf;
	blk_len = ValBuf_GetI4BYT(&buf_p);
	if ( blk_len < 0 && !read ) {
	    /*
	       Block size cannot be negative. See if adjusting byte
	       swapping produces something non-negative.
	     */

	    Toggle_Swap();
	    Swap_4Byt(&blk_len);
	    if ( blk_len < 0 ) {
		fprintf(stderr, "Negative size (%d) for %s block\n",
			blk_len, blk_id);
		goto error;
	    }
	}

	/* Adjust size of input buffer and read rest of block */
	blk_len8 = blk_len - 8;
	if ( blk_len8 > buf_sz ) {
	    char *t;

	    if ( !(t = REALLOC(buf, blk_len8)) ) {
		fprintf(stderr, "Could not allocate %d bytes "
			"for input buffer.\n", blk_len8);
		goto error;
	    }
	    buf = t;
	    buf_sz = blk_len8;
	}
	memset(buf, 0, buf_sz);
	if ( fread(buf, 1, blk_len8, in) != blk_len8 ) {
	    if ( !read ) {
		/*
		   Read failed, and this is the first block in the
		   file. Failure may be due to incorrect byte swapping.
		   Toggle byte swapping policy and try again.
		 */

		Toggle_Swap();
		Swap_4Byt(&blk_len);
		blk_len8 = blk_len - 8;
		if ( fseek(in, 8, SEEK_SET) != 0 ) {
		    fprintf(stderr, "Could not reposition in file after "
			    "resetting byte swapping.\n");
		    goto error;
		}
		if ( !lfread(buf, 1, blk_len8, in, "bytes") ) {
		    fprintf(stderr, "Could not read %s block. Attempted byte "
			    "swapping, still failed.\n", blk_id);
		    goto error;
		}
	    } else {
		/*
		   Read failed after first block. Assume byte swapping
		   does not change in the file, so something else is wrong.
		 */

		fprintf(stderr, "Could not read %s block.\n", blk_id);
		if ( feof(in) ) {
		    fprintf(stderr, "Unexpected end of file.\n");
		} else if ( ferror(in) ) {
		    perror(NULL);
		}
		goto error;
	    }
	}
	read = 1;

	/* Process block */
	switch (blk_typ) {
	    case BT_COMM:
		if ( !Dorade_COMM_Read(&swp_p->comm, buf) ) {
		    fprintf(stderr, "Could not read COMM block.\n%s\n",
			    strerror(errno));
		    goto error;
		}
		break;
	    case BT_SSWB:
		if ( !Dorade_SSWB_Read(&swp_p->sswb, buf) ) {
		    fprintf(stderr, "Could not read SSWB block.\n%s\n",
			    strerror(errno));
		    goto error;
		}
		break;
	    case BT_VOLD:
		if ( !Dorade_VOLD_Read(&swp_p->vold, buf) ) {
		    fprintf(stderr, "Could not read VOLD block.\n%s\n",
			    strerror(errno));
		    goto error;
		}
		break;
	    case BT_RADD:
		if ( !Dorade_RADD_Read(&swp_p->sensor.radd, buf) ) {
		    fprintf(stderr, "Could not read RADD block.\n%s\n",
			    strerror(errno));
		    goto error;
		}
		break;
	    case BT_CFAC:
		if ( !Dorade_CFAC_Read(&swp_p->sensor.cfac, buf) ) {
		    fprintf(stderr, "Could not read CFAC block.\n%s\n",
			    strerror(errno));
		    goto error;
		}
		break;
	    case BT_PARM:
		num_parms = swp_p->sensor.radd.num_parms;
		if ( num_parms == DORADE_BAD_I4 ) {
		    fprintf(stderr, "Parameter block found before number of "
			    "parameters known.\n");
		    goto error;
		}
		if ( num_parms + 1 > DORADE_MAX_PARMS ) {
		    fprintf(stderr, "Sweep cannot have more than %d "
			    "parameters.\n", DORADE_MAX_PARMS);
		    goto error;
		}
		Dorade_PARM_Init(&parm);
		if ( !Dorade_PARM_Read(&parm, buf) ) {
		    fprintf(stderr, "Failed to read PARM block.\n");
		    goto error;
		}
		p = Dorade_Parm_NewIdx(swp_p, parm.parm_nm);
		if ( p == -1 ) {
		    fprintf(stderr, "Could not find place for new "
			    "parameter.\n");
		    goto error;
		}
		swp_p->sensor.parms[p] = parm;
		parm_p = swp_p->sensor.parms + p;
		if ( !swp_p->sensor.parm0 ) {
		    swp_p->sensor.parm0 = parm_p;
		} else {
		    prev_parm->next = parm_p;
		}
		prev_parm = parm_p;
		break;
	    case BT_CELV:
		if ( !Dorade_CELV_Read(&swp_p->sensor.cell_geo.celv, buf) ) {
		    fprintf(stderr, "Failed to read CELV block.\n");
		    goto error;
		}
		swp_p->sensor.cell_geo_t = CG_CELV;
		break;
	    case BT_CSFD:
		if ( !Dorade_CSFD_Read(&swp_p->sensor.cell_geo.csfd, buf) ) {
		    fprintf(stderr, "Failed to read CSFD block.\n");
		    goto error;
		}
		swp_p->sensor.cell_geo_t = CG_CSFD;
		break;
	    case BT_SWIB:
		if ( !Dorade_SWIB_Read(&swp_p->swib, buf) ) {
		    fprintf(stderr, "Failed to read SWIB block.\n");
		    goto error;
		}
		num_rays = swp_p->swib.num_rays;
		if ( !swp_p->ray_hdr ) {
		    size_t sz = sizeof(struct Dorade_Ray_Hdr);

		    if ( !(swp_p->ray_hdr = CALLOC(num_rays, sz)) ) {
			fprintf(stderr, "Failed to allocate %d ray headers.\n",
				num_rays);
			goto error;
		    }
		    for (r = 0; r < num_rays; r++) {
			Dorade_RYIB_Init(&swp_p->ray_hdr[r].ryib);
			Dorade_ASIB_Init(&swp_p->ray_hdr[r].asib);
		    }
		}
		break;
	    case BT_RYIB:
		num_rays = swp_p->swib.num_rays;
		if ( num_rays == DORADE_BAD_I4 ) {
		    fprintf(stderr, "Ray data found before ray count known.\n");
		    goto error;
		}
		if ( num_rays_t + 1 > swp_p->swib.num_rays ) {
		    fprintf(stderr, "Sweep file has more rays than ray "
			    "count.\n");
		    goto error;
		}
		r = num_rays_t;
		num_parms = swp_p->sensor.radd.num_parms;
		if ( num_parms == DORADE_BAD_I4 ) {
		    fprintf(stderr, "Ray data found before parameter "
			    "count known.\n");
		    goto error;
		}
		if ( (num_cells = Dorade_NCells(swp_p)) == -1 ) {
		    fprintf(stderr, "Ray data found before cell "
			    "count known.\n");
		    goto error;
		}
		if ( !Dorade_RYIB_Read(&swp_p->ray_hdr[r].ryib, buf) ) {
		    fprintf(stderr, "Failed to read RYIB block.\n");
		    goto error;
		}
		parm_p = swp_p->sensor.parm0;
		break;
	    case BT_ASIB:
		if ( !Dorade_ASIB_Read(&swp_p->ray_hdr[r].asib, buf) ) {
		    fprintf(stderr, "Could not read ASIB block, ray %dk.\n", r);
		    goto error;
		}
		break;
	    case BT_RDAT:
		buf_p = buf;
		ValBuf_GetBytes(&buf_p, parm_nm, 8);
		parm_nm[8] = '\0';
		p = parm_p - swp_p->sensor.parms;
		if ( r == -1 ) {
		    fprintf(stderr, "RDAT (ray data) block found before "
			    "RYIB (ray info)\n");
		    goto error;
		}
		if ( !swp_p->dat[p] ) {
		    swp_p->dat[p] = Dorade_Alloc2F(num_rays, num_cells);
		    if ( !swp_p->dat[p] ) {
			fprintf(stderr, "Failed to allocate memory for "
				"data array with %d parameters, %d rays, "
				"and %d cells.\n",
				num_parms, num_rays, num_cells);
			goto error;
		    }
		}
		scale_inv = 1.0 / parm_p->parameter_scale;
		bias = parm_p->parameter_bias;
		dat = swp_p->dat[p][r];
		switch (parm_p->binary_format) {
		    case DD_8_BITS:
			cp = (char *)buf_p;
			for (c = 0; c <  num_cells; c++) {
			    if ( cp[c] == parm_p->bad_data ) {
				dat[c] = NAN;
			    } else {
				dat[c] = cp[c] * scale_inv - bias;
			    }
			}
			break;
		    case DD_16_BITS:
			s = ValBuf_GetI2BYT(&buf_p);
			dp  = swp_p->dat[p][r];
			if (swp_p->sensor.radd.data_compress) {
			    while (s != 1) {
				cnt = s & 0x7fff;
				if (dp + cnt
					> swp_p->dat[p][r] + num_cells) {
				    fprintf(stderr, "Pointer went "
					    "out of data array while "
					    "decompressing ray.\n");
				    goto error;
				}
				if (s & 0x8000) {
				    /*
				       Run of good data.  Transfer cnt
				       values from input buffer to
				       data array in sweep.
				     */

				    for (c = 0; c < cnt; c++) {
					s = ValBuf_GetI2BYT(&buf_p);
					if (s == parm_p->bad_data) {
					    *dp++ = NAN;
					} else {
					    *dp++ = s * scale_inv - bias;
					}
				    }
				    s = ValBuf_GetI2BYT(&buf_p);
				} else {
				    /*
				       Run of no data or bad data.  Put
				       cnt no-data values into data array
				       in sweep.  Ignore input buffer.
				     */

				    for (c = 0; c < cnt; c++) {
					*dp++ = NAN;
				    }
				    s = ValBuf_GetI2BYT(&buf_p);
				}
			    }
			    if (dp != swp_p->dat[p][r] + num_cells) {
				fprintf(stderr, "Decompression "
					"finished before end of ray.\n");
				goto error;
			    }
			} else {
			    if ( s == parm_p->bad_data ) {
				dp[0] = NAN;
			    } else {
				dp[0] = s * scale_inv - bias;
			    }
			    for (c = 1; c < num_cells; c++) {
				s = ValBuf_GetI2BYT(&buf_p);
				if ( s == parm_p->bad_data ) {
				    dp[c] = NAN;
				} else {
				    dp[c] = s * scale_inv - bias;
				}
			    }
			}
			break;
		    case DD_24_BITS:
			fprintf(stderr, "Cannot read 24 bit integers.\n");
			goto error;
			break;
		    case DD_32_BIT_FP:
			dp_in = (F4BYT *)buf_p;
			ip = (I4BYT *)dp_in;
			dp  = swp_p->dat[p][r];
			if (swp_p->sswb.compression_flag) {
			    while (*ip != 1) {
				cnt = *ip & 0x7fffffff;
				if (dp + cnt
					> swp_p->dat[p][r] + num_cells) {
				    fprintf(stderr, "Pointer went "
					    "out of data array while "
					    "decompressing ray.\n");
				    goto error;
				}
				if (*ip & 0x80000000) {
				    /*
				       Run of good data.  Transfer cnt
				       values from input buffer to
				       data array in sweep.
				     */

				    for (dp1 = ++dp_in + cnt;
					    dp_in < dp1;
					    dp_in++, dp++) {
					if (*dp_in == parm_p->bad_data) {
					    *dp = NAN;
					} else {
					    *dp = *dp_in;
					}
				    }
				} else {
				    /*
				       Run of no data or bad data.  Put
				       cnt no-data values into data array
				       in sweep.  Ignore input buffer.
				     */

				    for (c = 0; c < cnt; c++) {
					*dp++ = NAN;
				    }
				    ip++;
				}
			    }
			    if (dp != swp_p->dat[p][r] + num_cells) {
				fprintf(stderr, "Decompression "
					"finished before end of ray.\n");
				goto error;
			    }
			} else {
			    for (c = 0; c < num_cells; c++) {
				if ( dp_in[c] == parm_p->bad_data ) {
				    dp[c] = NAN;
				} else {
				    dp[c] = dp_in[c];
				}
			    }
			}
			break;
		    case DD_16_BIT_FP:
			fprintf(stderr, "Cannot read 16 bit floats.\n");
			goto error;
			break;
		}
		parm_p = parm_p->next;
		if ( !parm_p ) {
		    num_rays_t++;
		}
		break;
	    case BT_NULL:
		done_reading = 1;
		break;
	    case BT_RKTB:
	    case BT_SEDS:
	    case BT_NUL00:
	    case BT_NUL01:
	    case BT_NUL02:
	    case BT_NUL03:
	    case BT_NUL04:
	    case BT_NUL05:
	    case BT_NUL06:
	    case BT_NUL07:
	    case BT_NUL08:
	    case BT_NUL09:
	    case BT_NUL10:
	    case BT_NUL11:
	    case BT_NUL12:
	    case BT_NUL13:
	    case BT_NUL14:
	    case BT_NUL15:
	    case BT_NUL16:
	    case BT_NUL17:
	    case BT_NUL18:
	    case BT_NUL19:
	    case BT_NUL20:
	    case BT_NUL21:
	    case BT_NUL22:
	    case BT_NUL23:
	    case BT_NUL24:
	    case BT_NUL25:
	    case BT_NUL26:
	    case BT_NUL27:
	    case BT_NUL28:
	    case BT_NUL29:
	    case BT_NUL30:
	    case BT_NUL31:
	    case BT_NUL32:
	    case BT_NUL33:
	    case BT_NUL34:
	    case BT_NUL35:
	    case BT_NUL36:
	    case BT_NUL37:
	    case BT_NUL38:
	    case BT_NUL39:
	    case BT_NUL40:
	    case BT_NUL41:
	    case BT_NUL42:
	    case BT_NUL43:
	    case BT_NUL44:
	    case BT_NUL45:
	    case BT_NUL46:
	    case BT_NUL47:
		break;
	}
    }
    if ( ferror(in) ) {
	fprintf(stderr, "Input error.\n%s\n", strerror(errno));
	goto error;
    }
    if ( !read ) {
	fprintf(stderr, "File has no blocks.\n");
	goto error;
    }
    FREE(buf);
    return 1;

error:
    FREE(buf);
    Dorade_Sweep_Free(swp_p);
    return 0;
}

/*
   Create a sweep file. If swpFlNm is not NULL, use it for path, otherwise
   make a name.
 */

int Dorade_Sweep_Write(struct Dorade_Sweep *swp_p, char *swpFlNm)
{
    int r, p, c;		/* Loop parameters */
    struct tm tm;		/* For time calculations */
    char swpFlNm_d[LEN];	/* Default sweep file name. Use if no swpFlNm */
    FILE *out = NULL;		/* i_out as a FILE */
    char *ray_buf = NULL;	/* Output buffer for ray header and data */
    size_t ray_buf_max = 0;	/* Allocation size at ray_buf */
    size_t ray_buf_sz;		/* Size of RDAT block and data for one field */
    char buf[28];		/* Buffer for various output blocks */
    int num_parms, num_rays, num_cells;
    int num_cells_max;		/* num_cells + padding. Padding makes
				   block length divisible by 4. */
    char *buf_p;		/* Point into various output buffers */
    struct Dorade_PARM *parm_p;	/* Parameter */
    long fl_sz;			/* Sweep file size, for SSWB block */

    memset(buf, 0, sizeof(buf));
    buf_p = buf;
    num_parms = num_rays = num_cells = -1;
    num_rays = swp_p->swib.num_rays;
    num_parms = swp_p->sensor.radd.num_parms;
    if ( (num_cells = Dorade_NCells(swp_p)) == -1 ) {
	fprintf(stderr, "Ray data found before cell "
		"count known.\n");
	goto error;
    }
    num_cells_max = num_cells + num_cells % 2;
    ray_buf_sz = rdat_sz + 2 * num_cells_max;

    for (p = 0; p < num_parms; p++) {
	switch (swp_p->sensor.parms[p].binary_format) {
	    case DD_8_BITS:
		fprintf(stderr, "8 bit integers not supported.\n");
		return 0;
	    case DD_16_BITS:
		/* Default */
		break;
	    case DD_24_BITS:
		fprintf(stderr, "24 bit integers not supported.\n");
		return 0;
	    case DD_32_BIT_FP:
		fprintf(stderr, "32 bit float data not supported.\n");
		return 0;
	    case DD_16_BIT_FP:
		fprintf(stderr, "16 bit float data not supported.\n");
		return 0;
	}
    }

    /* Open output file */
    if ( swpFlNm ) {
	if ( !(out = fopen(swpFlNm, "w")) ) {
	    fprintf(stderr, "Could not open sweep file %s for writing\n%s\n",
		    swpFlNm, strerror(errno));
	    return 0;
	}
    } else {
	time_t start_time;

	swpFlNm_d[0] = '\0';
	start_time = swp_p->sswb.i_start_time;
	gmtime_r(&start_time, &tm);
	if ( snprintf(swpFlNm_d, LEN,
		    "swp.%0d%02d%02d%02d%02d%02d.%.8s.%d.%01.1f_%s_v1",
		    tm.tm_year, tm.tm_mon + 1, tm.tm_mday,
		    swp_p->ray_hdr[0].ryib.hour,
		    swp_p->ray_hdr[0].ryib.minute,
		    swp_p->ray_hdr[0].ryib.second,
		    swp_p->sensor.radd.radar_name,
		    swp_p->ray_hdr[0].ryib.millisecond,
		    swp_p->swib.fixed_angle,
		    ( swp_p->sensor.radd.scan_mode == 1 ) ? "PPI" :
		    ( swp_p->sensor.radd.scan_mode == 3 ) ? "RHI" :
		    "UNK" ) >= LEN) {
	    fprintf(stderr, "Could not create sweep file name.\n");
	    return 0;
	}
	if ( !(out = fopen(swpFlNm_d, "w")) ) {
	    fprintf(stderr, "Could not open sweep file %s for writing\n%s\n",
		    swpFlNm_d, strerror(errno));
	    return 0;
	}
    }

    /* Output here is always uncompressed. */
    swp_p->sswb.compression_flag = swp_p->sensor.radd.data_compress = 0;

    if ( !Dorade_COMM_Write(&swp_p->comm, out) ) {
	goto error;
    }
    if ( !Dorade_SSWB_Write(swp_p, out) ) {
	goto error;
    }
    if ( !Dorade_VOLD_Write(&swp_p->vold, out) ) {
	goto error;
    }
    if ( !Dorade_Sensor_Write(swp_p, out) ) {
	goto error;
    }
    if ( !Dorade_SWIB_Write(&swp_p->swib, out) ) {
	goto error;
    }
    for (r = 0; r < num_rays; r++) {
	if ( !Dorade_Ray_Hdr_Write(swp_p->ray_hdr + r, out) ) {
	    goto error;
	}
	for (parm_p = Dorade_NextParm(swp_p, NULL);
		parm_p;
		parm_p = Dorade_NextParm(swp_p, parm_p)) {
	    char *blk_id = "RDAT";
	    double scale, bias;
	    char *dat_buf;		/* Point into data part of ray_buf */
	    float d;

	    p = Dorade_Parm_Idx(swp_p, parm_p->parm_nm);
	    scale = parm_p->parameter_scale;
	    bias = parm_p->parameter_bias;
	    if ( ray_buf_sz > ray_buf_max ) {
		char *t;

		if ( !(t = (char *)REALLOC(ray_buf, ray_buf_sz)) ) {
		    fprintf(stderr, "Could not allocate ray buffer for "
			    "output.\n");
		    goto error;
		}
		ray_buf = t;
		ray_buf_max = ray_buf_sz;
	    }
	    buf_p = ray_buf;
	    ValBuf_PutBytes(&buf_p, blk_id, 4);
	    ValBuf_PutI4BYT(&buf_p, ray_buf_sz);
	    ValBuf_PutBytes(&buf_p, parm_p->parm_nm, 8);
	    for (dat_buf = ray_buf + rdat_sz ,c = 0; c < num_cells_max; c++) {
		ValBuf_PutI2BYT(&dat_buf, DORADE_BAD_I2);
	    }
	    for (dat_buf = ray_buf + rdat_sz, c = 0; c < num_cells; c++) {
		d = swp_p->dat[p][r][c];
		if ( isfinite(d) ) {
		    ValBuf_PutI2BYT(&dat_buf, round(scale * (d + bias)));
		} else {
		    ValBuf_PutI2BYT(&dat_buf, parm_p->bad_data);
		}
	    }
	    if ( fwrite(ray_buf, 1, ray_buf_sz, out) != ray_buf_sz ) {
		fprintf(stderr, "Could not write RDAT block.\n%s\n",
			strerror(errno));
		goto error;
	    }
	}
    }
    FREE(ray_buf);

    /* Put file size and offset to key table 0 into SSWB block */
    if ( (fl_sz = ftell(out)) == -1 ) {
	fprintf(stderr, "Could not determine size of sweep file %s\n%s\n",
		swpFlNm, strerror(errno));
	goto error;
    }
    Swap_4Byt(&fl_sz);
    if ( fseek(out, 508 + 20, SEEK_SET) != 0 ) {
	fprintf(stderr, "Could not set position in sweep file %s to add "
		"file size to SSWB block\n%s\n", swpFlNm, strerror(errno));
	goto error;
    }
    if ( fwrite(&fl_sz, 1, 4, out) != 4 ) {
	fprintf(stderr, "Could not write file size.\n%s\n", strerror(errno));
	goto error;
    }

    fclose(out);

    return 1;

error:
    FREE(ray_buf);
    if ( swpFlNm && strlen(swpFlNm) > 0 ) {
	fprintf(stderr, "Could not create sweep file %s\n", swpFlNm);
	unlink(swpFlNm);
    }
    if ( out ) {
	fclose(out);
    }
    return 0;
}

int Dorade_NCells(struct Dorade_Sweep *swp_p)
{
    int s, num_segments =0, num_cells = 0;
    struct Dorade_CSFD csfd;

    switch (swp_p->sensor.cell_geo_t) {
	case CG_CELV:
	    if ( num_cells == DORADE_BAD_I4 ) {
		return -1;
	    }
	    num_cells = swp_p->sensor.cell_geo.celv.num_cells;
	    break;
	case CG_CSFD:
	    csfd = swp_p->sensor.cell_geo.csfd;
	    num_segments = csfd.num_segments;
	    if ( num_segments == DORADE_BAD_I4 ) {
		return -1;
	    }
	    for (s = num_cells = 0; s < num_segments; s++) {
		num_cells += csfd.num_cells[s];
	    }
	    break;
    }
    return num_cells;
}

/*
   Put distances to cells into rng, which must have space for num_cells floats.
 */

void Dorade_CellRng(struct Dorade_Sweep *swp_p, float *rng)
{
    int s, c;
    float d;
    struct Dorade_CELV celv;
    struct Dorade_CSFD csfd;

    switch (swp_p->sensor.cell_geo_t) {
	case CG_CELV:
	    celv = swp_p->sensor.cell_geo.celv;
	    for (c = 0; c < celv.num_cells; c++) {
		rng[c] = celv.dist_cells[c];
	    }
	    break;
	case CG_CSFD:
	    csfd = swp_p->sensor.cell_geo.csfd;
	    for (d = csfd.dist_to_first, s = 0; s < csfd.num_segments; s++) {
		for (c = 0; c < csfd.num_cells[s]; c++) {
		    rng[c] = d;
		    d += csfd.spacing[s];
		}
	    }
	    break;
    }
}

/*
   If parm_nm is NULL, return first parameter, otherwise, return name of
   parameter after parm_nm in sequence read from sweep file.
 */

struct Dorade_PARM *Dorade_NextParm(struct Dorade_Sweep *swp_p,
	struct Dorade_PARM *parm_p)
{
    return parm_p ? parm_p->next : swp_p->sensor.parm0;
}

/*
   Copy arrays from swp_p->dat to dat, which must have storage for
   num_parms float** values.
   If p0 is the first parameter read from the sweep file, then
   swp_p->dat[p0] is assigned to dat[0].
   If p1 is the second parameter read from the sweep file, then
   swp_p->dat[p1] is assigned to dat[1].
   If p2 is the third parameter read from the sweep file, then
   swp_p->dat[p2] is assigned to dat[2].
   And so on.

   Upon return, dat[0], dat[1], dat[2], ... should not be modified by caller.
 */

void Dorade_Data(struct Dorade_Sweep *swp_p, float ***dat)
{
    int p;
    struct Dorade_PARM *parm_p;

    for (p = 0, parm_p = Dorade_NextParm(swp_p, NULL);
	    parm_p;
	    p++, parm_p = Dorade_NextParm(swp_p, parm_p)) {
	dat[p] = swp_p->dat[Dorade_Parm_Idx(swp_p, parm_p->parm_nm)];
    }
}

float **Dorade_ParmData(struct Dorade_Sweep *swp_p, char *parm_nm)
{
    int p;

    if ( (p = Dorade_Parm_Idx(swp_p, parm_nm)) == -1 ) {
	fprintf(stderr, "No parameter named %s\n", parm_nm);
	return NULL;
    }
    return swp_p->dat[p];
}

/*
   Add d_az to all azimuths in the sweep at swp_p.
 */

void Dorade_ShiftAz(struct Dorade_Sweep *swp_p, double d_az)
{
    int r;

    swp_p->swib.start_angle += d_az;
    swp_p->swib.stop_angle += d_az;
    for (r = 0; r < swp_p->swib.num_rays; r++) {
	swp_p->ray_hdr[r].ryib.azimuth += d_az;
    }
    if ( swp_p->sensor.radd.scan_mode == 3 ) {	/* 3 => RHI */
	swp_p->swib.fixed_angle += d_az;
    }
}

/*
   Add d_el to all azimuths in the sweep at swp_p.
 */

void Dorade_ShiftEl(struct Dorade_Sweep *swp_p, double d_el)
{
    int r;

    swp_p->swib.start_angle += d_el;
    swp_p->swib.stop_angle += d_el;
    for (r = 0; r < swp_p->swib.num_rays; r++) {
	swp_p->ray_hdr[r].ryib.elevation += d_el;
    }
    if ( swp_p->sensor.radd.scan_mode == 1 ) {	/* 1 => PPI */
	swp_p->swib.fixed_angle += d_el;
    }
}

/*
   Add dt seconds to the values of all time members in the sweep at swp_p.
 */

int Dorade_IncrTime(struct Dorade_Sweep *swp_p, double dt)
{
    int r;
    double jday;
    double j0;				/* Julian day at start of year */
    int yr, mon, day, hr, min;
    double sec;

    swp_p->sswb.i_start_time += dt;
    swp_p->sswb.i_stop_time += dt;
    swp_p->sswb.volume_time_stamp += dt;
    swp_p->sswb.start_time += dt;
    swp_p->sswb.stop_time += dt;
    yr = swp_p->vold.year;
    mon = swp_p->vold.month;
    day = swp_p->vold.day;
    hr = swp_p->vold.data_set_hour;
    min = swp_p->vold.data_set_minute;
    sec = swp_p->vold.data_set_second;
    jday = Tm_CalToJul(yr, mon, day, hr, min, sec);
    j0 = Tm_CalToJul(yr, 1, 1, 0, 0, 0.0);
    jday += dt / 86400.0;
    if ( !Tm_JulToCal(jday, &yr, &mon, &day, &hr, &min, &sec) ) {
	fprintf(stderr, "Failed to convert time in volume descriptor.\n");
	return 0;
    }
    swp_p->vold.year = yr;
    swp_p->vold.month = mon;
    swp_p->vold.day = day;
    swp_p->vold.data_set_hour = hr;
    swp_p->vold.data_set_minute = min;
    swp_p->vold.data_set_second = sec;
    for (r = 0; r < swp_p->swib.num_rays; r++) {
	struct Dorade_RYIB *ryib_p = &swp_p->ray_hdr[r].ryib;

	hr = ryib_p->hour;
	min = ryib_p->minute;
	jday = j0 + ryib_p->julian_day + hr / 24.0 + min / 1440.0
	    + ryib_p->second / 86400.0 + ryib_p->millisecond / 86400000.0;
	jday += dt / 86400.0;
	ryib_p->julian_day = floor(jday - j0 + 1);
	if ( !Tm_JulToCal(jday, &yr, &mon, &day, &hr, &min, &sec) ) {
	    fprintf(stderr, "Failed to convert time for ray %d.\n", r);
	    return 0;
	}
	ryib_p->hour = hr;
	ryib_p->minute = min;
	ryib_p->millisecond = 1000 * modf(sec, &sec);
	ryib_p->second = sec;
    }
    return 1;
}

/*
   . Smooth data along rays for parameter p.
   . For dat = swp_p->dat, value in ray index r at cell index c will be replaced
   . with:
   .	1 / n * (dat[p][r][c-(n-1)/2] + ... + dat[p][r][c+n/2])
   . If n is odd, this is a center average. If n is even, it is forward.
 */

int Dorade_Smooth(struct Dorade_Sweep *swp_p, int p, int n)
{
    int r, c;				/* Ray, cell index */
    int num_rays, num_cells;		/* Sweep geometry */
    float *dat_r = NULL;		/* Hold data for one ray */

    if ( !swp_p ) {
	fprintf(stderr, "Smooth function called on bogus sweep.\n");
	goto error;
    }
    if ( !swp_p || !swp_p->dat[p] ) {
	fprintf(stderr, "Attempting to smooth nonexistent parameter.\n");
	goto error;
    }
    if ( (num_rays = swp_p->swib.num_rays) == DORADE_BAD_I4 ) {
	fprintf(stderr, "Number of rays not known.");
	goto error;
    }
    if ( (num_cells = Dorade_NCells(swp_p)) == -1 ) {
	fprintf(stderr, "Number of cells not known.");
	goto error;
    }
    if ( !(dat_r = CALLOC(num_cells, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate memory for temporary ray "
		"of %d cells.\n", num_cells);
	goto error;
    }
    for (r = 0; r < num_rays; r++) {
	for (c = 0; c < num_cells; c++) {
	    int c_;			/* Cell index in averaging interval */
	    int c0, c1;			/* Limits for c_ */
	    int c_used;			/* Number of values to use in
					   averaging, less than n, because
					   NAN cell values are skipped. */
	    float dat_sum;		/* Data total in smoothing interval */

	    c0 = c - (n - 1) / 2;
	    if ( c0 < 0 ) {
		c0 = 0;
	    }
	    c1 = c + n / 2;
	    if ( c1 >= num_cells ) {
		c1 = num_cells - 1;
	    }
	    for (dat_sum = 0.0, c_used = 0, c_ = c0; c_ <= c1; c_++) {
		if ( isfinite(swp_p->dat[p][r][c_]) ) {
		    dat_sum += swp_p->dat[p][r][c_];
		    c_used++;
		}
	    }
	    dat_r[c] = (c_used > 0) ? dat_sum / c_used : NAN;
	}
	memcpy(swp_p->dat[p][r], dat_r, num_cells * sizeof(float));
    }
    FREE(dat_r);
    return 1;

error:
    FREE(dat_r);
    return 0;
}

void Dorade_Sweep_Free(struct Dorade_Sweep *swp_p)
{
    int p;

    if ( swp_p->sensor.cell_geo_t == CG_CELV ) {
	FREE(swp_p->sensor.cell_geo.celv.dist_cells);
    }
    FREE(swp_p->ray_hdr);
    for (p = 0; p < DORADE_MAX_PARMS; p++) {
	if ( swp_p->dat[p] ) {
	    Dorade_Free2F(swp_p->dat[p]);
	}
    }
    Dorade_Sweep_Init(swp_p);
}

/*
   Allocate an of floats dimensioned [j][i].
 */

float ** Dorade_Alloc2F(int j, int i)
{
    float **dat = NULL, *d;
    long n;
    size_t jj, ii;		/* Addends for pointer arithmetic */
    size_t ji;

    /* Make sure casting to size_t does not overflow anything.  */
    if (j <= 0 || i <= 0) {
	fprintf(stderr, "Array dimensions must be positive.\n");
	return NULL;
    }
    jj = (size_t)j;
    ii = (size_t)i;
    ji = jj * ii;
    if (ji / jj != ii) {
	fprintf(stderr, "Dimensions %d by %d too big for pointer arithmetic.\n",
		j, i);
	return NULL;
    }

    dat = (float **)CALLOC(jj + 2, sizeof(float *));
    if ( !dat ) {
	fprintf(stderr, "Could not allocate memory with %d rows for 1st "
		"dimension of two dimensional array.\n", j);
	return NULL;
    }
    dat[0] = (float *)CALLOC(ji, sizeof(float));
    if ( !dat[0] ) {
	FREE(dat);
	fprintf(stderr, "Could not allocate memory for values of two "
		"dimensional array with %d by %d elements.\n", j, i);
	return NULL;
    }
    for (d = dat[0]; d < dat[0] + ji; d++) {
	*d = NAN;
    }
    for (n = 1; n <= j; n++) {
	dat[n] = dat[n - 1] + i;
    }
    return dat;
}

void Dorade_Free2F(float **dat)
{
    if (dat && dat[0]) {
	FREE(dat[0]);
    }
    FREE(dat);
}

/*
   Call fread. Print an error message if it fails. item_nm provides string
   description of what is being read, e.g. "bytes", "float values". Return
   1/0 on success/failure.
 */

static int lfread(char *buf, size_t sz, size_t n, FILE *in, char *item)
{
    size_t n1;				/* Number of items read */

    n1 = fread(buf, sz, n, in);
    if ( n1 == n ) {
	return 1;
    } else {
	fprintf(stderr, "Read fail. Attempted to read %u %s, got %u.\n",
		(unsigned)n, item, (unsigned)n1);
	if ( feof(in) ) {
	    fprintf(stderr, "Unexpected end of file.\n");
	} else if ( ferror(in) ) {
	    perror(NULL);
	}
	return 0;
    }
}
