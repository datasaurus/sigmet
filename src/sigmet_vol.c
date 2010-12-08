/*
   -	sigmet_vol.c--
   -		Definitions of functions that store and access
   -		information from Sigmet raw product volumes.
   -
   .	Global functions (Sigmet_*) are documented sigmet (3).
   .
   .	Copyright (c) 2004 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.96 $ $Date: 2010/12/08 20:41:26 $
   .
   .	Reference: IRIS Programmers Manual
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include "alloc.h"
#include "tm_calc_lib.h"
#include "err_msg.h"
#include "swap.h"
#include "type_nbit.h"
#include "geog_lib.h"
#include "data_types.h"
#include "sigmet.h"

/* Length of a record in a Sigmet raw file */
#define REC_LEN 6144

/* Header sizes in data record */
#define SZ_RAW_PROD_BHDR 12
#define SZ_INGEST_DATA_HDR 76
#define SZ_RAY_HDR 12

/* Field separator in printf output */
#define FS "|"

/* Sigmet scan modes */
enum SCAN_MODE {ppi_sec = 1, rhi, man, ppi_cont, file};

static char *trimRight(char *, int);
static int get_sint16(void *);
static unsigned get_uint16(void *);
static int get_sint32(void *);
static unsigned get_uint32(void *);
static void swap_arr16(void *, int);
static int type_tbl_set(struct Sigmet_Vol *);

/* Default length for character strings */
#define STR_LEN 512

/*
   These functions read and print structures that make up a Sigmet raw product
   volume. The structures are declared in sigmet.h. See the Sigmet programmer's
   guide for more information.
 */
static struct Sigmet_YMDS_Time get_ymds_time(char *);
static void print_ymds_time(FILE *, struct Sigmet_YMDS_Time,
	char *, char *, char *);
static struct Sigmet_Structure_Header get_structure_header(char *);
static void print_structure_header(FILE *, char *,
	struct Sigmet_Structure_Header);
static struct Sigmet_Product_Specific_Info get_product_specific_info(char *);
static void print_product_specific_info(FILE *, char *,
	struct Sigmet_Product_Specific_Info);
static struct Sigmet_Color_Scale_Def get_color_scale_def(char *);
static void print_color_scale_def(FILE *, char *,
	struct Sigmet_Color_Scale_Def);
static struct Sigmet_Product_Configuration get_product_configuration(char *);
static void print_product_configuration(FILE *, char *,
	struct Sigmet_Product_Configuration);
static struct Sigmet_Product_End get_product_end(char *);
static void print_product_end(FILE *, char *, struct Sigmet_Product_End);
static struct Sigmet_Product_Hdr get_product_hdr(char *);
static void print_product_hdr(FILE *, char *, struct Sigmet_Product_Hdr);
static struct Sigmet_Ingest_Configuration get_ingest_configuration(char *);
static void print_ingest_configuration(FILE *, char *,
	struct Sigmet_Ingest_Configuration);
static struct Sigmet_Task_Sched_Info get_task_sched_info(char *);
static void print_task_sched_info(FILE *, char *,
	struct Sigmet_Task_Sched_Info);
static struct Sigmet_DSP_Data_Mask get_dsp_data_mask(char *);
static void print_dsp_data_mask(FILE *, char *, struct Sigmet_DSP_Data_Mask,
	char *);
static struct Sigmet_Task_DSP_Mode_Batch get_task_dsp_mode_batch(char *);
static void print_task_dsp_mode_batch(FILE *, char *,
	struct Sigmet_Task_DSP_Mode_Batch);
static struct Sigmet_Task_DSP_Info get_task_dsp_info(char *);
static void print_task_dsp_info(FILE *, char *, struct Sigmet_Task_DSP_Info);
static struct Sigmet_Task_Calib_Info get_task_calib_info(char *);
static void print_task_calib_info(FILE *, char *,
	struct Sigmet_Task_Calib_Info);
static struct Sigmet_Task_Range_Info get_task_range_info(char *);
static void print_task_range_info(FILE *, char *,
	struct Sigmet_Task_Range_Info);
static struct Sigmet_Task_RHI_Scan_Info get_task_rhi_scan_info(char *);
static void print_task_rhi_scan_info(FILE *, char *,
	struct Sigmet_Task_RHI_Scan_Info);
static struct Sigmet_Task_PPI_Scan_Info get_task_ppi_scan_info(char *);
static void print_task_ppi_scan_info(FILE *, char *,
	struct Sigmet_Task_PPI_Scan_Info);
static struct Sigmet_Task_File_Scan_Info get_task_file_scan_info(char *);
static void print_task_file_scan_info(FILE *, char *,
	struct Sigmet_Task_File_Scan_Info);
static struct Sigmet_Task_Manual_Scan_Info get_task_manual_scan_info(char *);
static void print_task_manual_scan_info(FILE *, char *,
	struct Sigmet_Task_Manual_Scan_Info);
static struct Sigmet_Task_Scan_Info get_task_scan_info(char *);
static void print_task_scan_info(FILE *, char *, struct Sigmet_Task_Scan_Info);
static struct Sigmet_Task_Misc_Info get_task_misc_info(char *);
static void print_task_misc_info(FILE *, char *, struct Sigmet_Task_Misc_Info);
static struct Sigmet_Task_End_Info get_task_end_info(char *);
static void print_task_end_info(FILE *, char *, struct Sigmet_Task_End_Info);
static struct Sigmet_Task_Configuration get_task_configuration(char *);
static void print_task_configuration(FILE *, char *,
	struct Sigmet_Task_Configuration);
static struct Sigmet_Ingest_Header get_ingest_header(char *);
static void print_ingest_header(FILE *, char *, struct Sigmet_Ingest_Header);

/* Output functions */
static void print_u(FILE *, unsigned , char *, char *, char *);
static void print_x(FILE *, unsigned , char *, char *, char *);
static void print_i(FILE *, int , char *, char *, char *);
static void print_s(FILE *, char *, char *, char *, char *);

/* Allocators */
static double ** calloc2d(long, long);
static void free2d(double **);
static int ** calloc2i(long, long);
static void free2i(int **);
static U1BYT *** calloc3_u1(long, long, long);
static void free3_u1(U1BYT ***);
static U2BYT *** calloc3_u2(long, long, long);
static void free3_u2(U2BYT ***);
static float *** calloc3_flt(long, long, long);
static void free3_flt(float ***);

void Sigmet_Vol_Init(struct Sigmet_Vol *vol_p)
{
    int n;

    if (!vol_p) {
	return;
    }
    memset(vol_p, 0, sizeof(struct Sigmet_Vol));
    vol_p->num_types = 0;
    Hash_Init(&vol_p->types_tbl, 0);
    for (n = 0; n < SIGMET_NTYPES; n++) {
	vol_p->types_fl[n] = DB_XHDR;		/* Force error if used */
    }
    vol_p->sweep_ok = NULL;
    vol_p->sweep_time = NULL;
    vol_p->sweep_angle = NULL;
    vol_p->ray_tilt0 = vol_p->ray_tilt1
	= vol_p->ray_az0 = vol_p->ray_az1 = NULL;
    vol_p->dat = NULL;
    vol_p->truncated = 1;
    vol_p->size = sizeof(struct Sigmet_Vol);
    return;
}

void Sigmet_Vol_Free(struct Sigmet_Vol *vol_p)
{
    int y;

    if (!vol_p) {
	return;
    }
    FREE(vol_p->sweep_ok);
    FREE(vol_p->sweep_time);
    FREE(vol_p->sweep_angle);
    free2i(vol_p->ray_ok);
    free2d(vol_p->ray_time);
    free2i(vol_p->ray_num_bins);
    free2d(vol_p->ray_tilt0);
    free2d(vol_p->ray_tilt1);
    free2d(vol_p->ray_az0);
    free2d(vol_p->ray_az1);
    if ( vol_p->dat ) {
	for (y = 0; y < vol_p->num_types; y++) {
	    switch (vol_p->dat[y].data_type->stor_fmt) {
		case DATA_TYPE_U1:
		    free3_u1(vol_p->dat[y].arr.d1);
		    break;
		case DATA_TYPE_U2:
		    free3_u2(vol_p->dat[y].arr.d2);
		case DATA_TYPE_FLT:
		    free3_flt(vol_p->dat[y].arr.flt);
		    break;
		case DATA_TYPE_DBL:
		case DATA_TYPE_MT:
		    break;
	    }
	}
	FREE(vol_p->dat);
    }
    Hash_Clear(&vol_p->types_tbl);
    Sigmet_Vol_Init(vol_p);
}

/*
   Set values in volume types table
 */

static int type_tbl_set(struct Sigmet_Vol *vol_p)
{
    struct Sigmet_DatArr *dat_p;

    for (dat_p = vol_p->dat; dat_p < vol_p->dat + vol_p->num_types; dat_p++) {
	if ( !Hash_Set(&vol_p->types_tbl, dat_p->data_type->abbrv, dat_p) ) {
	    return 0;
	}
    }
    return 1;
}

int Sigmet_Vol_ReadHdr(FILE *f, struct Sigmet_Vol *vol_p)
{
    char rec[REC_LEN];			/* Input record from file */
    int status;

    /*
       yf will increment as bits are found in the volume type mask.
       It will end up with a count of the Sigmet data types in the
       file, including DB_XHDR.

       y will receive the number of types in the volume, excluding the
       extended header type.
       vol_p->types array will store type identifiers that end up in vol_p.

       If the volume uses extended headers, yf = y + 1,
       and types_fl will have one more element than vol_p->types.
       The extra type is the "extended header type".
     */

    int y, yf;
    enum Sigmet_DataTypeN sig_type;
    struct Sigmet_DatArr *dat_p;
    char *abbrv;
    struct DataType *data_type;

    /*
       These masks are placed against the data type mask in the task dsp info
       structure to determine what data types are in the volume.
     */
    static unsigned type_mask_bit[SIGMET_NTYPES] = {
	(1 <<  0), (1 <<  1), (1 <<  2), (1 <<  3), (1 <<  4),
	(1 <<  5), (1 <<  7), (1 <<  8), (1 <<  9), (1 << 10),
	(1 << 11), (1 << 12), (1 << 13), (1 << 14), (1 << 15),
	(1 << 16), (1 << 17), (1 << 18), (1 << 19), (1 << 20),
	(1 << 21), (1 << 22), (1 << 23), (1 << 24), (1 << 25),
	(1 << 26), (1 << 27), (1 << 28)
    };
    unsigned vol_type_mask;
    size_t sz;

    if ( !f ) {
	Err_Append("Read header function called with bogus input stream. ");
	status = SIGMET_BAD_ARG;
	goto error;
    }
    if ( !vol_p ) {
	Err_Append("Read header function called with bogus volume. ");
	status = SIGMET_BAD_ARG;
	goto error;
    }

    Sigmet_Vol_Free(vol_p);

    /*
       Initialize dat array with SIGMET_NTYPES members.
       Do not allocate arrays within dat.
     */

    vol_p->dat = CALLOC(SIGMET_NTYPES, sizeof(struct Sigmet_DatArr));
    if ( !vol_p->dat ) {
	Err_Append("Could not allocate data array (data type dimension). ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    for (dat_p = vol_p->dat; dat_p < vol_p->dat + SIGMET_NTYPES; dat_p++) {
	dat_p->data_type = NULL;
	dat_p->arr.flt = NULL;
    }
    if ( !Hash_Init(&vol_p->types_tbl, SIGMET_NTYPES * 2) ) {
	Err_Append("Could not allocate space for volume types table. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }

    /*
       record 1, <product_header>
     */

    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	Err_Append("Could not read record 1 of Sigmet volume.  ");
	status = SIGMET_IO_FAIL;
	goto error;
    }

    /*
       If first 16 bits of product header != 27, turn on byte swapping
       and check again. If still not 27, give up.
     */
    if (get_sint16(rec) != 27) {
	Toggle_Swap();
	if (get_sint16(rec) != 27) {
	    Err_Append( "Bad magic number (should be 27).  ");
	    status = SIGMET_BAD_FILE;
	    goto error;
	}
    }

    vol_p->ph = get_product_hdr(rec);

    /* record 2, <ingest_header> */
    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	Err_Append("Could not read record 2 of Sigmet volume.  ");
	status = SIGMET_IO_FAIL;
	goto error;
    }
    vol_p->ih = get_ingest_header(rec);

    /*
       Loop through the bits in the data type mask. 
       If bit is set, add the corresponding type to types array.
     */

    vol_type_mask = vol_p->ih.tc.tdi.curr_data_mask.mask_word_0;
    if (vol_type_mask & type_mask_bit[DB_XHDR]) {
	vol_p->xhdr = 1;
    }
    for (sig_type = 0, yf = y = 0; sig_type < SIGMET_NTYPES; sig_type++) {
	if (vol_type_mask & type_mask_bit[sig_type]) {
	    if ( !(abbrv = Sigmet_DataType_Abbrv(sig_type))
		    || !(data_type = DataType_Get(abbrv)) ) {
		Err_Append("Unknown data type in volume file. ");
		status = SIGMET_BAD_FILE;
		goto error;
	    }
	    vol_p->dat[y].data_type = data_type;
	    switch (data_type->stor_fmt) {
		case DATA_TYPE_MT:
		    break;
		case DATA_TYPE_U1:
		    vol_p->dat[y].arr.d1 = NULL;
		    y++;
		    break;
		case DATA_TYPE_U2:
		    vol_p->dat[y].arr.d2 = NULL;
		    y++;
		    break;
		case DATA_TYPE_FLT:
		case DATA_TYPE_DBL:
		    Err_Append("Volume in memory is corrupt. Unknown "
			    "data type in data array.");
		    status = SIGMET_BAD_FILE;
		    goto error;
		    break;
	    }
	    if ( !Hash_Add(&vol_p->types_tbl, abbrv, dat_p) ) {
		Err_Append("Could not initialize volume types table. ");
		status = SIGMET_ALLOC_FAIL;
		goto error;
	    }
	    vol_p->types_fl[yf] = sig_type;
	    yf++;
	}
    }
    vol_p->num_types = y;
    sz = vol_p->num_types * sizeof(struct Sigmet_DatArr);
    dat_p = REALLOC(vol_p->dat, sz);
    if ( !dat_p ) {
	Err_Append("Could not reallocate space for volume types array. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->dat = dat_p;
    if ( !type_tbl_set(vol_p) ) {
	Err_Append("Allocation failed while reseting volume types table. ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += vol_p->num_types + sizeof(struct Sigmet_DatArr);
    vol_p->has_headers = 1;
    return SIGMET_OK;

error:
    Sigmet_Vol_Free(vol_p);
    return status;
}

void Sigmet_Vol_PrintHdr(FILE *out, struct Sigmet_Vol *vol_p)
{
    int y;
    char elem_nm[STR_LEN];

    if ( !vol_p->has_headers ) {
	fprintf(out, "volume has no headers\n");
    }
    print_product_hdr(out, "<product_hdr>.", vol_p->ph);
    print_ingest_header(out, "<ingest_header>.", vol_p->ih);
    fprintf(out, "%d " FS " %s " FS " %s\n",
	    vol_p->xhdr, "xhdr", "If true, volume uses extended headers");
    fprintf(out, "%d " FS " %s " FS " %s\n",
	    vol_p->num_types, "num_types", "Number of Sigmet data types");
    for (y = 0; y < vol_p->num_types; y++) {
	struct DataType *data_type = vol_p->dat[y].data_type;

	snprintf(elem_nm, STR_LEN, "%s%d%s", "types[", y, "]");
	fprintf(out, "%s " FS " %s " FS " %s\n",
		data_type->abbrv, elem_nm, data_type->descr);
    }
    fprintf(out, "%d " FS " %s " FS " %s\n",
	    vol_p->truncated, "truncated", "If true, volume is truncated");
}

int Sigmet_Vol_Good(FILE *f)
{
    struct Sigmet_Vol vol;
    char rec[REC_LEN];			/* Input record from file */
    int num_types_fl;
    int num_types;
    static unsigned type_mask_bit[SIGMET_NTYPES] = {
	(1 <<  0), (1 <<  1), (1 <<  2), (1 <<  3), (1 <<  4),
	(1 <<  5), (1 <<  7), (1 <<  8), (1 <<  9), (1 << 10),
	(1 << 11), (1 << 12), (1 << 13), (1 << 14), (1 << 15),
	(1 << 16), (1 << 17), (1 << 18), (1 << 19), (1 << 20),
	(1 << 21), (1 << 22), (1 << 23), (1 << 24), (1 << 25),
	(1 << 26), (1 << 27), (1 << 28)
    };
    unsigned vol_type_mask;
    U16BIT *recP;			/* Pointer into rec */
    U16BIT *recN;			/* Stopping point in rec */
    U16BIT *recEnd = (U16BIT *)(rec + REC_LEN); /* End rec */
    int rec_idx;			/* Current record index (0 is first) */
    int sweep_num;			/* Current sweep number (1 is first) */

    int num_sweeps;			/* Number of sweeps in volume */
    unsigned num_rays;			/* Number of rays per sweep */
    int num_bins;			/* Number of output bins */

    int year, month, day, sec;
    unsigned msec;

    unsigned numWds;			/* Number of words in a run of data */
    enum Sigmet_DataTypeN type;
    int s, r;				/* Sweep, ray indeces */
    int i, n;				/* Temporary values */

    s = type = r = 0;
    Sigmet_Vol_Init(&vol);

    /* record 1, <product_header> */
    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	return SIGMET_IO_FAIL;
    }

    /*
       If first 16 bits of product header != 27, turn on byte swapping
       and check again. If still not 27, give up.
     */
    if (get_sint16(rec) != 27) {
	Toggle_Swap();
	if (get_sint16(rec) != 27) {
	    return SIGMET_BAD_FILE;
	}
    }

    vol.ph = get_product_hdr(rec);

    /* record 2, <ingest_header> */
    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	return SIGMET_IO_FAIL;
    }
    vol.ih = get_ingest_header(rec);

    /*
       Obtain number of data types in volume from data type mask. 
     */

    vol_type_mask = vol.ih.tc.tdi.curr_data_mask.mask_word_0;
    for (type = 0, num_types_fl = 0, num_types = 0; type < SIGMET_NTYPES; type++) {
	if (vol_type_mask & type_mask_bit[type]) {
	    if (type == DB_XHDR) {
		vol.xhdr = 1;
	    } else {
		num_types++;
	    }
	    vol.types_fl[num_types_fl] = type;
	    num_types_fl++;
	}
    }
    vol.num_types = num_types;

    num_types = vol.num_types;
    num_types_fl = num_types + vol.xhdr;
    num_sweeps = vol.ih.tc.tni.num_sweeps;
    num_rays = vol.ih.ic.num_rays;
    num_bins = vol.ih.tc.tri.num_bins_out;

    /* Process data records */
    rec_idx = 1;
    sweep_num = 0;
    while (fread(rec, 1, REC_LEN, f) == REC_LEN) {

	/* Get record number and sweep number from <raw_prod_bhdr>. */
	i = get_sint16(rec);
	n = get_sint16(rec + 2);

	if (i != rec_idx + 1) {
	    return SIGMET_BAD_FILE;
	}
	rec_idx = i;

	if (n != sweep_num) {
	    /* Record is start of new sweep. */
	    sweep_num = n;
	    if (sweep_num > num_sweeps) {
		return SIGMET_BAD_FILE;
	    }
	    s = sweep_num - 1;
	    r = 0;

	    /*
	       If sweep number from <ingest_data_header> has gone back to 0,
	       there are no more sweeps in volume.
	     */
	    n = get_sint16(rec + 36);
	    if (n == 0) {
		vol.ih.tc.tni.num_sweeps = sweep_num - 1;
		break;
	    }

	    /* Store sweep time and angle (from first <ingest_data_header>). */
	    sec = get_sint32(rec + 24);
	    msec = get_uint16(rec + 28);
	    year = get_sint16(rec + 30);
	    month = get_sint16(rec + 32);
	    day = get_sint16(rec + 34);
	    if (year == 0 || month == 0 || day == 0) {
		return SIGMET_BAD_FILE;
	    }

	    /* Set sweep in volume. */

	    /* Byte swap data segment in record, if necessary. */
	    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR
		    + num_types_fl * SZ_INGEST_DATA_HDR);
	    swap_arr16(recP, recEnd - recP);
	    type = 0;

	} else {

	    /*
	       Record continues a sweep started in an earlier record Byte
	       swap data segment in record, if necessary.
	     */
	    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR);
	    swap_arr16(recP, recEnd - recP);

	}

	/*
	   Decompress and store ray data.
	   See IRIS/Open Programmers Manual, April 2000, p. 3-38 to 3-40.
	 */
	while (recP < recEnd) {

	    if ((0x8000 & *recP) == 0x8000) {
		/* Run of data words */
		numWds = 0x7FFF & *recP;
		if (numWds > recEnd - recP - 1) {
		    /*
		       Data run crosses record boundary. Store number of
		       words in second part of data run. We will need this
		       when we get to the next record.
		     */
		    numWds = numWds - (recEnd - recP - 1);

		    /* Skip rest of current record. */
		    for (recP++; recP < recEnd; recP++) {
		    }

		    /*
		       Get next record. Record number from <raw_prod_bhdr>
		       should agree with counter.
		     */
		    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
			return SIGMET_IO_FAIL;
		    }
		    i = get_sint16(rec);
		    if (i != rec_idx + 1) {
			return SIGMET_BAD_FILE;
		    }
		    rec_idx = i;

		    /*
		       Position record pointer at start of data segment and
		       byte swap data segment in record, if necessary.
		     */
		    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR);
		    swap_arr16(recP, recEnd - recP);

		    /* Skip second part of data run from the new record. */
		    for (recN = recP + numWds; recP < recN; recP++) {
		    }

		} else {
		    /* Current record contains entire data run. Skip it. */
		    for (recP++, recN = recP + numWds; recP < recN; recP++) {
		    }
		}
	    } else if (*recP == 1) {
		/* End of ray */
		if (s > num_sweeps) {
		    return SIGMET_BAD_FILE;
		}
		if (r > num_rays) {
		    return SIGMET_BAD_FILE;
		}

		/* Skip ray data. */

		/* Reset for next ray. */
		if (++type == num_types_fl) {
		    r++;
		    type = 0;
		}
		recP++;
	    } else {
		/* Run of zeros */
		numWds = 0x7FFF & *recP;
		recP++;
	    }
	}
    }
    if (s + 1 != num_sweeps) {
	return SIGMET_BAD_FILE;
    }
    if ( !feof(f) ) {
	return SIGMET_BAD_FILE;
    }
    return SIGMET_OK;
}

int Sigmet_Vol_Read(FILE *f, struct Sigmet_Vol *vol_p)
{
    char rec[REC_LEN];			/* Input record from file */
    int status;


    U16BIT *recP;			/* Pointer into rec */
    U16BIT *recN;			/* Stopping point in rec */
    U16BIT *recEnd = (U16BIT *)(rec + REC_LEN); /* End rec */
    int rec_idx;			/* Current record index (0 is first) */
    int sweep_num;			/* Current sweep number (1 is first) */

    int num_sweeps;			/* Number of sweeps in vol_p */
    int num_types_fl;			/* Number of types in the file will be
					 * one more than num_types if the volume
					 * contains extended headers. */
    int num_types;			/* Number of types in the volume */
    unsigned num_rays;			/* Number of rays per sweep */
    int num_bins;			/* Number of output bins */

    int year, month, day, sec;
    unsigned msec;
    double swpTm = 0.0;
    double angle;			/* Sweep angle */

    U16BIT *ray = NULL;			/* Buffer, receives data from rec */
    U16BIT *rayP = NULL;		/* Point into ray while looping */
    U16BIT *rayEnd = NULL;		/* End of allocation at ray */

    size_t raySz;			/* Allocation size for a ray */
    unsigned char *rayd;		/* Pointer to start of data in ray */
    unsigned numWds;			/* Number of words in a run of data */
    int s, r, b;			/* Sweep, ray, bin indeces */
    int yf, y;				/* Indeces for type in file, type in dat */
    int i, n;				/* Temporary values */
    int nbins;				/* vol_p->ray_num_bins */
    int tm_incr;			/* Ray time adjustment */

    if ( !f ) {
	Err_Append("Read header function called with bogus input stream. ");
	status = SIGMET_BAD_ARG;
	goto error;
    }
    if ( !vol_p ) {
	Err_Append("Read header function called with bogus volume. ");
	status = SIGMET_BAD_ARG;
	goto error;
    }

    s = r = b = 0;
    yf = y = 0;

    /* Read headers. As a side effect, this initializes vol_p. */
    if ( (status = Sigmet_Vol_ReadHdr(f, vol_p)) != SIGMET_OK ) {
	Err_Append("Could not read volume headers.\n");
	Sigmet_Vol_Free(vol_p);
	return status;
    }

    /* Allocate arrays in vol_p. */
    num_types = vol_p->num_types;
    num_types_fl = num_types + vol_p->xhdr;
    num_sweeps = vol_p->ih.tc.tni.num_sweeps;
    num_rays = vol_p->ih.ic.num_rays;
    num_bins = vol_p->ih.tc.tri.num_bins_out;

    vol_p->sweep_ok = (int *)CALLOC(num_sweeps, sizeof(int));
    if ( !vol_p->sweep_ok ) {
	Err_Append("Could not allocate sweep status array.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * sizeof(int);

    vol_p->sweep_time = (double *)CALLOC(num_sweeps, sizeof(double));
    if ( !vol_p->sweep_time ) {
	Err_Append("Could not allocate sweep time array.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * sizeof(double);

    vol_p->sweep_angle = (double *)CALLOC(num_sweeps, sizeof(double));
    if ( !vol_p->sweep_angle ) {
	Err_Append("Could not allocate sweep angle array.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * sizeof(double);

    vol_p->ray_ok = calloc2i(num_sweeps, num_rays);
    if ( !vol_p->ray_ok ) {
	Err_Append("Could not allocate array of ray status array.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(int);

    vol_p->ray_time = calloc2d(num_sweeps, num_rays);
    if ( !vol_p->ray_time ) {
	Err_Append("Could not allocate ray time array.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(double);

    vol_p->ray_num_bins = calloc2i(num_sweeps, num_rays);
    if ( !vol_p->ray_num_bins ) {
	Err_Append("Could not allocate array of ray bin counts.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(int);

    vol_p->ray_tilt0 = calloc2d(num_sweeps, num_rays);
    if ( !vol_p->ray_tilt0 ) {
	Err_Append("Could not allocate array of ray initial tilts.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(double);

    vol_p->ray_tilt1 = calloc2d(num_sweeps, num_rays);
    if ( !vol_p->ray_tilt1 ) {
	Err_Append("Could not allocate array of ray final tilts.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(double);

    vol_p->ray_az0 = calloc2d(num_sweeps, num_rays);
    if ( !vol_p->ray_az0 ) {
	Err_Append("Could not allocate array of ray initial azimuths.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(double);

    vol_p->ray_az1 = calloc2d(num_sweeps, num_rays);
    if ( !vol_p->ray_az1 ) {
	Err_Append("Could not allocate array of ray final azimuths.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    vol_p->size += num_sweeps * num_rays * sizeof(double);

    for (y = 0; y < vol_p->num_types; y++) {
	switch (vol_p->dat[y].data_type->stor_fmt) {
	    case DATA_TYPE_U1:
		vol_p->dat[y].arr.d1 = calloc3_u1(num_sweeps, num_rays, num_bins);
		if ( !vol_p->dat[y].arr.d1 ) {
		    status = SIGMET_ALLOC_FAIL;
		    goto error;
		}
		vol_p->size += num_sweeps * num_rays * num_bins;
		break;
	    case DATA_TYPE_U2:
		vol_p->dat[y].arr.d2 = calloc3_u2(num_sweeps, num_rays, num_bins);
		if ( !vol_p->dat[y].arr.d2 ) {
		    status = SIGMET_ALLOC_FAIL;
		    goto error;
		}
		vol_p->size += num_sweeps * num_rays * num_bins * 2;
		break;
	    default:
		Err_Append("Volume in memory is corrupt. Unknown data type "
			"in data array.");
		status = SIGMET_BAD_VOL;
		goto error;
		break;
	}
    }

    /*
       Allocate and partition largest possible ray buffer.
       Data will be decompressed from rec and copied into ray.
     */
    raySz = SZ_RAY_HDR + vol_p->ih.ic.extended_ray_headers_sz
	+ num_bins * sizeof(U16BIT);
    ray = (U16BIT *)MALLOC(raySz);
    if ( !ray ) {
	Err_Append("Could not allocate input ray buffer.  ");
	status = SIGMET_ALLOC_FAIL;
	goto error;
    }
    rayd = (unsigned char *)ray + SZ_RAY_HDR;
    rayEnd = (U16BIT *)((unsigned char *)ray + raySz);

    /* Process data records. */
    rec_idx = 1;
    sweep_num = 0;
    while (fread(rec, 1, REC_LEN, f) == REC_LEN) {

	/* Get record number and sweep number from <raw_prod_bhdr>. */
	i = get_sint16(rec);
	n = get_sint16(rec + 2);

	if (i != rec_idx + 1) {
	    Err_Append(
		    "Sigmet raw product file records out of sequence.  ");
	    status = SIGMET_BAD_FILE;
	    goto error;
	}
	rec_idx = i;

	if (n != sweep_num) {
	    /* Record is start of new sweep. */
	    vol_p->sweep_ok[sweep_num] = 1;
	    sweep_num = n;
	    if (sweep_num > num_sweeps) {
		Err_Append("Volume has excess sweeps.  ");
		status = SIGMET_BAD_FILE;
		goto error;
	    }
	    s = sweep_num - 1;
	    r = 0;

	    /*
	       If sweep number from <ingest_data_header> has gone back to 0,
	       there are no more sweeps in volume.
	     */
	    n = get_sint16(rec + 36);
	    if (n == 0) {
		vol_p->ih.tc.tni.num_sweeps = sweep_num - 1;
		break;
	    }

	    /* Store sweep time and angle (from first <ingest_data_header>). */
	    sec = get_sint32(rec + 24);
	    msec = get_uint16(rec + 28);
	    year = get_sint16(rec + 30);
	    month = get_sint16(rec + 32);
	    day = get_sint16(rec + 34);
	    if (year == 0 || month == 0 || day == 0) {
		Err_Append("Sweep date is 0000/00/00.  ");
		status = SIGMET_BAD_FILE;
		goto error;
	    }

	    /* Set sweep in volume. */
	    swpTm = Tm_CalToJul(year, month, day, 0, 0, sec + 0.001 * msec);
	    angle = Sigmet_Bin2Rad(get_uint16(rec + 46));
	    vol_p->sweep_time[s] = swpTm;
	    vol_p->sweep_angle[s] = angle;

	    /* Byte swap data segment in record, if necessary. */
	    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR
		    + num_types_fl * SZ_INGEST_DATA_HDR);
	    swap_arr16(recP, recEnd - recP);

	    /* Initialize ray. */
	    memset(ray, 0, raySz);
	    rayP = ray;
	    yf = 0;

	} else {

	    /*
	       Record continues a sweep started in an earlier record Byte
	       swap data segment in record, if necessary.
	     */
	    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR);
	    swap_arr16(recP, recEnd - recP);

	}

	/* Decompress and store ray data.  See IRIS/Open Programmers Manual. */
	while (recP < recEnd) {

	    if ((0x8000 & *recP) == 0x8000) {
		/* Run of data words */
		numWds = 0x7FFF & *recP;
		if (numWds > recEnd - recP - 1) {
		    /*
		       Data run crosses record boundary. Store number of
		       words in second part of data run. We will need this
		       when we get to the next record.
		     */
		    numWds = numWds - (recEnd - recP - 1);

		    /* Read rest of current record. */
		    for (recP++; recP < recEnd; recP++, rayP++) {
			*rayP = *recP;
		    }

		    /* Get next record. */
		    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
			vol_p->truncated = 1;
			break;
		    }
		    i = get_sint16(rec);
		    if (i != rec_idx + 1) {
			Err_Append("Sigmet raw product file records "
				" out of sequence.  ");
			status = SIGMET_BAD_FILE;
			goto error;
		    }
		    rec_idx = i;

		    /*
		       Position record pointer at start of data segment and
		       byte swap data segment in record, if necessary.
		     */
		    recP = (U16BIT *)(rec + SZ_RAW_PROD_BHDR);
		    swap_arr16(recP, recEnd - recP);

		    /* Get second part of data run from the new record. */
		    for (recN = recP + numWds; recP < recN; recP++, rayP++) {
			*rayP = *recP;
		    }

		} else {
		    /* Current record contains entire data run. */
		    for (recP++, recN = recP + numWds; recP < recN;
			    recP++, rayP++) {
			*rayP = *recP;
		    }
		}
	    } else if (*recP == 1) {
		/* End of ray */
		if (s > num_sweeps) {
		    Err_Append("Volume has more sweeps than reported in header. ");
		    status = SIGMET_BAD_FILE;
		    goto error;
		}
		if (r > num_rays) {
		    Err_Append("Volume has more rays than reported in header. ");
		    status = SIGMET_BAD_FILE;
		    goto error;
		}
		vol_p->ray_ok[s][r] = 1;
		vol_p->ray_az0[s][r] = Sigmet_Bin2Rad(ray[0]);
		vol_p->ray_tilt0[s][r] = Sigmet_Bin2Rad(ray[1]);
		vol_p->ray_az1[s][r] = Sigmet_Bin2Rad(ray[2]);
		vol_p->ray_tilt1[s][r] = Sigmet_Bin2Rad(ray[3]);
		nbins = vol_p->ray_num_bins[s][r] = ray[4];
		if ( !vol_p->xhdr ) {
		    vol_p->ray_time[s][r] = swpTm + ray[5];
		}

		/* Store ray data. */
		y = yf - vol_p->xhdr;
		if ( vol_p->types_fl[yf] == DB_XHDR ) {
			/*
			   For extended header, undo the call to swap_arr16
			   above, then apply byte swapping to the raw input.
			 */
			swap_arr16(ray + SZ_RAY_HDR, 2);
			tm_incr = get_sint32(ray + SZ_RAY_HDR);
			vol_p->ray_time[s][r] = swpTm + tm_incr * 0.001 / 86400.0;
		} else {
		    switch (Sigmet_DataType_StorFmt(vol_p->types_fl[yf])) {
			case DATA_TYPE_U1:
			    for (b = 0; b < nbins; b++)  {
				vol_p->dat[y].arr.d1[s][r][b] = rayd[b];
			    }
			    break;
			case DATA_TYPE_U2:
			    for (b = 0; b < nbins; b++)  {
				vol_p->dat[y].arr.d2[s][r][b] = ((U2BYT *)rayd)[b];
			    }
			    break;
			default:
			    Err_Append("Volume has unknown data type.  ");
			    status = SIGMET_BAD_FILE;
			    goto error;
			    break;
		    }
		}

		/* Reset for next ray. */
		memset(ray, 0, raySz);
		rayP = ray;
		if (++yf == num_types_fl) {
		    r++;
		    yf = 0;
		}
		recP++;
	    } else {
		/* Run of zeros */
		numWds = 0x7FFF & *recP;
		if ( numWds > rayEnd - rayP ) {
		    Err_Append("Corrupt volume.  "
			    "Run of zeros tried to go past end of ray.  ");
		    status = SIGMET_BAD_FILE;
		    goto error;
		}
		rayP += numWds;
		recP++;
	    }
	}
    }
    for (s = 0; s < vol_p->ih.tc.tni.num_sweeps && vol_p->sweep_ok[s]; s++) {
	continue;
    }
    vol_p->truncated = (r + 1 < num_rays || s + 1 < num_sweeps) ? 1 : 0;
    if ( vol_p->truncated && feof(f) ) {
	status = SIGMET_BAD_FILE;
    }
    FREE(ray);
    vol_p->num_sweeps_ax = s;

    return SIGMET_OK;

error:
    FREE(ray);
    Sigmet_Vol_Free(vol_p);
    return status;
}

int Sigmet_Vol_BadRay(struct Sigmet_Vol *vol_p, int s, int r)
{
    return vol_p->ray_az0[s][r] == vol_p->ray_az1[s][r];
}

/*
   Add a new field to a volume.  This also allocates space for data in the
   dat array. It does not initialize the array.
 */

int Sigmet_Vol_NewField(struct Sigmet_Vol *vol_p, char *abbrv)
{
    struct DataType *data_type;
    size_t sz;
    struct Sigmet_DatArr *dat_p;
    float ***flt_p;
    int num_sweeps, num_rays, num_bins;

    if ( !abbrv ) {
	Err_Append("Attempted to add bogus data type to a volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !vol_p ) {
	Err_Append("Attempted to add data type to a bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(data_type = DataType_Get(abbrv)) ) {
	Err_Append("No data type named %s. ");
	Err_Append(abbrv);
	Err_Append( " Please add with the new_data_type command. ");
	return SIGMET_BAD_ARG;
    }
    if ( Hash_Get(&vol_p->types_tbl, abbrv) ) {
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" already exists in volume. ");
	return SIGMET_BAD_ARG;
    }
    sz = (vol_p->num_types + 1) * sizeof(struct Sigmet_DatArr);
    if ( !(dat_p = REALLOC(vol_p->dat, sz)) ) {
	Err_Append("Allocation failed while enlarging volume data "
		"array. ");
	return SIGMET_ALLOC_FAIL;
    }
    vol_p->dat = dat_p;
    if ( !type_tbl_set(vol_p) ) {
	Err_Append("Allocation failed while reseting volume types table. ");
	return SIGMET_ALLOC_FAIL;
    }
    dat_p = vol_p->dat + vol_p->num_types;
    dat_p->data_type = data_type;
    dat_p->arr.flt = NULL;
    if ( !Hash_Add(&vol_p->types_tbl, abbrv, dat_p) ) {
	Err_Append("Could not add ");
	Err_Append(abbrv);
	Err_Append(" to volume types table. ");
	return SIGMET_ALLOC_FAIL;
    }
    num_sweeps = vol_p->ih.tc.tni.num_sweeps;
    num_rays = vol_p->ih.ic.num_rays;
    num_bins = vol_p->ih.tc.tri.num_bins_out;
    flt_p = calloc3_flt(num_sweeps, num_rays, num_bins);
    if ( !flt_p ) {
	dat_p->data_type = NULL;
	Err_Append("Could not allocate new field ");
	return SIGMET_ALLOC_FAIL;
    }
    dat_p->arr.flt = flt_p;
    vol_p->num_types++;
    return SIGMET_OK;
}

int Sigmet_Vol_DelField(struct Sigmet_Vol *vol_p, char *abbrv)
{
    struct DataType *data_type;
    size_t sz;
    struct Sigmet_DatArr *d_p, *d1_p;

    if ( !abbrv ) {
	Err_Append("Attempted to remove a bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( !vol_p ) {
	Err_Append("Attempted to remove a field from a bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(data_type = DataType_Get(abbrv)) ) {
	Err_Append("No data type named %s. ");
	Err_Append(abbrv);
	Err_Append( ".  ");
	return SIGMET_BAD_ARG;
    }
    if ( !(d_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume. ");
	return SIGMET_BAD_ARG;
    }
    free3_flt(d_p->arr.flt);
    Hash_Rm(&vol_p->types_tbl, abbrv);
    for (d1_p = d_p + 1; d1_p < vol_p->dat + vol_p->num_types; d_p++, d1_p++) {
	*d_p = *d1_p;
    }
    sz = (vol_p->num_types - 1) * sizeof(struct Sigmet_DatArr);
    if ( !(d_p = REALLOC(vol_p->dat, sz)) ) {
	Err_Append("Allocation failed while reallocating volume data array.");
	return SIGMET_ALLOC_FAIL;
    }
    vol_p->dat = d_p;
    vol_p->num_types--;
    if ( !type_tbl_set(vol_p) ) {
	Err_Append("Allocation failed while reseting volume types table. ");
	return SIGMET_ALLOC_FAIL;
    }
    return SIGMET_OK;
}

/*
   Initialize data array to a given value.
 */

int Sigmet_Vol_Fld_SetFlt(struct Sigmet_Vol *vol_p, char *abbrv, float v)
{
    struct Sigmet_DatArr *dat_p;
    int s, r;
    float *dp, *dp1;

    if ( !vol_p || !abbrv ) {
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    dat_p->data_type->stor_fmt = DATA_TYPE_FLT;
    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( vol_p->ray_ok[s][r] ) {
		    dp = dat_p->arr.flt[s][r];
		    dp1 = dp + vol_p->ray_num_bins[s][r];
		    for ( ; dp < dp1; dp++) {
			*dp = v;
		    }
		}
	    }
	}
    }
    return SIGMET_OK;
}

/*
   Initialize data array to distance along beam.
 */

int Sigmet_Vol_Fld_SetRBeam(struct Sigmet_Vol *vol_p, char *abbrv)
{
    struct Sigmet_DatArr *dat_p;
    float ***arr;
    int s, r, b;
    double bin0, bin_step;

    if ( !vol_p || !abbrv ) {
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    dat_p->data_type->stor_fmt = DATA_TYPE_FLT;
    arr = dat_p->arr.flt;
    bin_step = 0.01 * vol_p->ih.tc.tri.step_out;	/* cm -> meter */
    bin0 = 0.01 * vol_p->ih.tc.tri.rng_1st_bin + 0.5 * bin_step;
    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( vol_p->ray_ok[s][r] ) {
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
			 arr[s][r][b] = bin0 + b * bin_step;
		    }
		}
	    }
	}
    }
    return SIGMET_OK;
}

/*
   Copy field abbrv2 to abbrv1. Previous contents of abbrv1 are obliterated.
 */

int Sigmet_Vol_Fld_Copy(struct Sigmet_Vol *vol_p, char *abbrv1, char *abbrv2)
{
    enum Sigmet_DataTypeN sig_type1;
    struct Sigmet_DatArr *dat_p1, *dat_p2;
    struct DataType *data_type1, *data_type2;
    int s, r, b;
    int num_sweeps, num_rays, num_bins;
    size_t sz;
    float f2;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv1 || !abbrv2 ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv1, &sig_type1) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p1 = Hash_Get(&vol_p->types_tbl, abbrv1)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv1);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type1 = dat_p1->data_type;
    if ( data_type1->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    if ( !(dat_p2 = Hash_Get(&vol_p->types_tbl, abbrv2)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv2);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type2 = dat_p2->data_type;
    switch (data_type2->stor_fmt) {
	case DATA_TYPE_U1:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f2 = dat_p2->arr.d1[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				dat_p1->arr.flt[s][r][b] = f2;
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_U2:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f2 = dat_p2->arr.d2[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				dat_p1->arr.flt[s][r][b] = f2;
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_FLT:
	    num_sweeps = vol_p->ih.tc.tni.num_sweeps;
	    num_rays = vol_p->ih.ic.num_rays;
	    num_bins = vol_p->ih.tc.tri.num_bins_out;
	    sz = num_sweeps * num_rays * num_bins;
	    memcpy(dat_p1->arr.flt, dat_p2->arr.flt, sz);
	    break;
	case DATA_TYPE_DBL:
	case DATA_TYPE_MT:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
			    }
			}
		    }
		}
	    }
	    break;
    }
    return SIGMET_OK;
}

/*
   Add scalar a to field abbrv.
 */

int Sigmet_Vol_Fld_AddFlt(struct Sigmet_Vol *vol_p, char *abbrv, float a)
{
    enum Sigmet_DataTypeN sig_type;
    struct Sigmet_DatArr *dat_p;
    struct DataType *data_type;
    int s, r, b;
    float f;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv, &sig_type) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type = dat_p->data_type;
    if ( data_type->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( vol_p->ray_ok[s][r] ) {
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
			f = dat_p->arr.flt[s][r][b];
			if ( Sigmet_IsData(f) ) {
			    dat_p->arr.flt[s][r][b] = f + a;
			}
		    }
		}
	    }
	}
    }
    return SIGMET_OK;
}

/*
   Add field abbrv2 to abbrv1.
 */

int Sigmet_Vol_Fld_AddFld(struct Sigmet_Vol *vol_p, char *abbrv1, char *abbrv2)
{
    enum Sigmet_DataTypeN sig_type1;
    struct Sigmet_DatArr *dat_p1, *dat_p2;
    struct DataType *data_type1, *data_type2;
    int sgn = 1;		/* -1 if *abbrv2 == '-' (negate the field) */
    int s, r, b;
    float f1, f2;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv1 || !abbrv2 ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv1, &sig_type1) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p1 = Hash_Get(&vol_p->types_tbl, abbrv1)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv1);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type1 = dat_p1->data_type;
    if ( data_type1->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    if ( *abbrv2 == '-' ) {
	sgn = -1;
	abbrv2++;
    }
    if ( !(dat_p2 = Hash_Get(&vol_p->types_tbl, abbrv2)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv2);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type2 = dat_p2->data_type;
    switch (dat_p2->data_type->stor_fmt) {
	case DATA_TYPE_U1:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.d1[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 + sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_U2:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.d2[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 + sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_FLT:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.flt[s][r][b];
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 + sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_DBL:
	case DATA_TYPE_MT:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
			    }
			}
		    }
		}
	    }
	    break;
    }
    return SIGMET_OK;
}

/*
   Multiply field abbrv by scalar a.
 */

int Sigmet_Vol_Fld_MulFlt(struct Sigmet_Vol *vol_p, char *abbrv, float a)
{
    enum Sigmet_DataTypeN sig_type;
    struct Sigmet_DatArr *dat_p;
    struct DataType *data_type;
    int s, r, b;
    float f;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv, &sig_type) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type = dat_p->data_type;
    if ( data_type->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( vol_p->ray_ok[s][r] ) {
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
			f = dat_p->arr.flt[s][r][b];
			if ( Sigmet_IsData(f) ) {
			    dat_p->arr.flt[s][r][b] = f * a;
			}
		    }
		}
	    }
	}
    }
    return SIGMET_OK;
}

/*
   Replace field abbrv1 with abbrv1 * abbrv2.
 */

int Sigmet_Vol_Fld_MulFld(struct Sigmet_Vol *vol_p, char *abbrv1, char *abbrv2)
{
    enum Sigmet_DataTypeN sig_type1;
    struct Sigmet_DatArr *dat_p1, *dat_p2;
    struct DataType *data_type1, *data_type2;
    int sgn = 1;		/* -1 if *abbrv2 == '-' (negate the field) */
    int s, r, b;
    float f1, f2;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv1 || !abbrv2 ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv1, &sig_type1) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p1 = Hash_Get(&vol_p->types_tbl, abbrv1)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv1);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type1 = dat_p1->data_type;
    if ( data_type1->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    if ( *abbrv2 == '-' ) {
	sgn = -1;
	abbrv2++;
    }
    if ( !(dat_p2 = Hash_Get(&vol_p->types_tbl, abbrv2)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv2);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type2 = dat_p2->data_type;
    switch (dat_p2->data_type->stor_fmt) {
	case DATA_TYPE_U1:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.d1[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 * sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_U2:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.d2[s][r][b];
				f2 = DataType_StorToComp(data_type2, f2, vol_p);
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 * sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_FLT:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				f1 = dat_p1->arr.flt[s][r][b];
				f2 = dat_p2->arr.flt[s][r][b];
				if ( Sigmet_IsData(f1) && Sigmet_IsData(f2) ) {
				    dat_p1->arr.flt[s][r][b] = f1 * sgn * f2;
				} else {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
				}
			    }
			}
		    }
		}
	    }
	    break;
	case DATA_TYPE_DBL:
	case DATA_TYPE_MT:
	    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
		if ( vol_p->sweep_ok[s] ) {
		    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
			if ( vol_p->ray_ok[s][r] ) {
			    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
				    dat_p1->arr.flt[s][r][b] = Sigmet_NoData();
			    }
			}
		    }
		}
	    }
	    break;
    }
    return SIGMET_OK;
}

/*
   Replace field abbrv with its log10.
 */

int Sigmet_Vol_Fld_Log10(struct Sigmet_Vol *vol_p, char *abbrv)
{
    enum Sigmet_DataTypeN sig_type;
    struct Sigmet_DatArr *dat_p;
    struct DataType *data_type;
    int s, r, b;
    float f;

    if ( !vol_p ) {
	Err_Append("Attempted to add field to bogus volume. ");
	return SIGMET_BAD_ARG;
    }
    if ( !abbrv ) {
	Err_Append("Attempted to add bogus field. ");
	return SIGMET_BAD_ARG;
    }
    if ( Sigmet_DataType_GetN(abbrv, &sig_type) ) {
	Err_Append("Sigmet raw data cannot be modified. "
		"Please copy the field and modify the copy. ");
	return SIGMET_BAD_ARG;
    }
    if ( !(dat_p = Hash_Get(&vol_p->types_tbl, abbrv)) ) {
	Err_Append("No field of ");
	Err_Append(abbrv);
	Err_Append(" in volume. ");
	return SIGMET_BAD_ARG;
    }
    data_type = dat_p->data_type;
    if ( data_type->stor_fmt != DATA_TYPE_FLT ) {
	Err_Append("Editable field in volume not in correct format. ");
	return SIGMET_BAD_VOL;
    }
    for (s = 0; s < vol_p->ih.ic.num_sweeps; s++) {
	if ( vol_p->sweep_ok[s] ) {
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( vol_p->ray_ok[s][r] ) {
		    for (b = 0; b < vol_p->ray_num_bins[s][r]; b++)  {
			f = dat_p->arr.flt[s][r][b];
			if ( Sigmet_IsData(f) ) {
			    f = log10(f);
			    dat_p->arr.flt[s][r][b]
				= isfinite(f) ? f : Sigmet_NoData();
			}
		    }
		}
	    }
	}
    }
    return SIGMET_OK;
}

/* Nyquist velocity */
double Sigmet_Vol_VNyquist(struct Sigmet_Vol *vol_p)
{
    double wav_len, prf;

    prf = vol_p->ih.tc.tdi.prf;
    wav_len = 0.01 * 0.01 * vol_p->ih.tc.tmi.wave_len;
    switch (vol_p->ih.tc.tdi.m_prf_mode) {
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

/* Fetch a value from a Sigmet volume */
float Sigmet_Vol_GetDat(struct Sigmet_Vol *vol_p, int y, int s, int r, int b)
{
    float v;

    if ( !vol_p ) {
	return Sigmet_NoData();
    }
    if ( y < 0 || y >= vol_p->num_types
	    || s < 0 || s >= vol_p->num_sweeps_ax
	    || r < 0 || r >= vol_p->ih.ic.num_rays
	    || !vol_p->ray_num_bins
	    || b < 0 || b >= vol_p->ray_num_bins[s][r] ) {
	return Sigmet_NoData();
    }
    switch (vol_p->dat[y].data_type->stor_fmt) {
	case DATA_TYPE_U1:
	    v = vol_p->dat[y].arr.d1[s][r][b];
	    break;
	case DATA_TYPE_U2:
	    v = vol_p->dat[y].arr.d2[s][r][b];
	    break;
	case DATA_TYPE_FLT:
	    v = vol_p->dat[y].arr.flt[s][r][b];
	    break;
	case DATA_TYPE_DBL:
	case DATA_TYPE_MT:
	    return Sigmet_NoData();
    }
    return DataType_StorToComp(vol_p->dat[y].data_type, v, vol_p);
}

/* Return lon-lat's at corners of a bin */
int Sigmet_Vol_BinOutl(struct Sigmet_Vol *vol_p, int s, int r, int b,
	double *ll)
{
    double re;			/* Earth radius */
    double lon_r, lat_r;	/* Radar longitude latitude */
    double az0, az1;		/* Azimuth limits of bin */
    double r00, dr;		/* Range to first bin, bin length, in meters */
    double r0, r1;		/* Range to start and end of bin */
    double tilt;		/* Mean tilt */

    /* Distances in meters */

    if (s >= vol_p->ih.ic.num_sweeps) {
	Err_Append("Sweep index out of bounds.  ");
	return SIGMET_RNG_ERR;
    }
    if (r >= vol_p->ih.ic.num_rays) {
	Err_Append("Ray index out of bounds.  ");
	return SIGMET_RNG_ERR;
    }
    if (b >= vol_p->ray_num_bins[s][r]) {
	Err_Append("Bin index out of bounds.  ");
	return SIGMET_RNG_ERR;
    }

    re = GeogREarth(NULL);
    lon_r = Sigmet_Bin4Rad(vol_p->ih.ic.longitude);
    lat_r = Sigmet_Bin4Rad(vol_p->ih.ic.latitude);
    r00 = vol_p->ih.tc.tri.rng_1st_bin;
    dr = vol_p->ih.tc.tri.step_out;
    r0 = 0.01 * (r00 + b * dr);
    r1 = 0.01 * (r00 + (b + 1) * dr);
    az0 = vol_p->ray_az0[s][r];
    az1 = vol_p->ray_az1[s][r];
    if (az1 < az0) {
	double t = az1;
	az1 = az0;
	az0 = t;
    }
    tilt = 0.5 * (vol_p->ray_tilt0[s][r] + vol_p->ray_tilt1[s][r]);

    /* Distance along ground to point under the bin */
    r0 = atan(r0 * cos(tilt) / (re + r0 * sin(tilt)));
    r1 = atan(r1 * cos(tilt) / (re + r1 * sin(tilt)));

    GeogStep(lon_r, lat_r, az0, r0, ll, ll + 1);
    GeogStep(lon_r, lat_r, az0, r1, ll + 2, ll + 3);
    GeogStep(lon_r, lat_r, az1, r1, ll + 4, ll + 5);
    GeogStep(lon_r, lat_r, az1, r0, ll + 6, ll + 7);

    return SIGMET_OK;
}

/* get / print product_hdr (a.k.a. raw volume record 1). */

struct Sigmet_Product_Hdr get_product_hdr(char *rec)
{
    struct Sigmet_Product_Hdr ph;

    ph.sh = get_structure_header(rec);
    ph.pc = get_product_configuration(rec);
    rec += 12;
    ph.pe = get_product_end(rec);
    rec += 320;
    return ph;
}

void print_product_hdr(FILE *out, char *prefix, struct Sigmet_Product_Hdr ph)
{
    print_structure_header(out, prefix, ph.sh);
    print_product_configuration(out, prefix,  ph.pc);
    print_product_end(out, prefix, ph.pe);
}

/* get / print product_configuration. */

struct Sigmet_Product_Configuration get_product_configuration(char *rec)
{
    struct Sigmet_Product_Configuration pc;

    pc.sh = get_structure_header(rec);
    pc.type = get_uint16(rec + 12);
    pc.schedule = get_uint16(rec + 14);
    pc.skip = get_sint32(rec + 16);
    pc.gen_tm = get_ymds_time(rec + 20);
    pc.ingest_sweep_tm = get_ymds_time(rec + 32);
    pc.ingest_file_tm = get_ymds_time(rec + 44);
    memcpy(pc.config_file, rec + 62, 12);
    trimRight(pc.config_file, 12);
    memcpy(pc.task_name, rec + 74, 12);
    trimRight(pc.task_name, 12);
    pc.flag = get_uint16(rec + 86);
    pc.x_scale = get_sint32(rec + 88);
    pc.y_scale = get_sint32(rec + 92);
    pc.z_scale = get_sint32(rec + 96);
    pc.x_size = get_sint32(rec + 100);
    pc.y_size = get_sint32(rec + 104);
    pc.z_size = get_sint32(rec + 108);
    pc.x_loc = get_sint32(rec + 112);
    pc.y_loc = get_sint32(rec + 116);
    pc.z_loc = get_sint32(rec + 120);
    pc.max_rng = get_sint32(rec + 124);
    pc.data_type = get_uint16(rec + 130);
    memcpy(pc.proj, rec + 132, 12);
    trimRight(pc.proj, 12);
    pc.inp_data_type = get_uint16(rec + 144);
    pc.proj_type = *(unsigned char *)(rec + 146);
    pc.rad_smoother = get_sint16(rec + 148);
    pc.num_runs = get_sint16(rec + 150);
    pc.zr_const = get_sint32(rec + 152);
    pc.zr_exp = get_sint32(rec + 156);
    pc.x_smooth = get_sint16(rec + 160);
    pc.y_smooth = get_sint16(rec + 162);
    pc.psi = get_product_specific_info(rec + 80);
    memcpy(pc.suffixes, rec + 244, 16);
    trimRight(pc.suffixes, 16);
    pc.csd = get_color_scale_def(rec + 272);
    return pc;
}

void print_product_configuration(FILE *out, char *pfx,
	struct Sigmet_Product_Configuration pc)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_configuration>.");
    print_structure_header(out, prefix, pc.sh);
    print_u(out, pc.type, prefix, "type",
	    "Product type code: 1:PPI 2:RHI 3:CAPPI 4:CROSS 5:TOPS 6:TRACK"
	    " 7:RAIN1 8:RAINN 9:VVP 10:VIL 11:SHEAR 12:WARN 13:CATCH 14:RTI"
	    " 15:RAW 16:MAX 17:USER 18:USERV 19:OTHER 20:STATUS 21:SLINE"
	    " 22:WIND 23:BEAM 24:TEXT 25:FCAST 26:NDOP 27:IMAGE 28:COMP"
	    " 29:TDWR 30:GAGE 31:DWELL 32:SRI 33:BASE 34:HMAX");
    print_u(out, pc.schedule, prefix, "schedule",
	    "Scheduling code: 0:hold; 1:next; 2:all");
    print_i(out, pc.skip, prefix, "skip",
	    "Number of seconds to skip between runs");
    print_ymds_time(out, pc.gen_tm, prefix, "gen_tm",
	    "Time product was generated (UTC)");
    print_ymds_time(out, pc.ingest_sweep_tm, prefix,
	    "ingest_sweep_tm","Time of input ingest sweep (TZ flex)");
    print_ymds_time(out, pc.ingest_file_tm, prefix,
	    "ingest_file_tm","Time of input ingest file (TZ flexible)");
    print_s(out, pc.config_file, prefix, "config_file",
	    "Name of the product configuration file");
    print_s(out, pc.task_name, prefix, "task_name",
	    "Name of the task used to generate the data");
    print_x(out, pc.flag, prefix, "flag",
	    "Flag word: (Bits 0,2,3,4,8,9,10 used internally)."
	    " Bit1: TDWR style messages. Bit5: Keep this file. Bit6: This is a"
	    " clutter map. Bit7: Speak warning messages. Bit11: This product"
	    " has been composited. Bit12: This product has been dwelled."
	    " Bit13: Z/R source0, 0:Type­in; 1:Setup; 2:Disdrometer."
	    " Bit14: Z/R source1");
    print_i(out, pc.x_scale, prefix, "x_scale",
	    "X scale in cm/pixel");
    print_i(out, pc.y_scale, prefix, "y_scale",
	    "Y scale in cm/pixel");
    print_i(out, pc.z_scale, prefix, "z_scale",
	    "Z scale in cm/pixel");
    print_i(out, pc.x_size, prefix, "x_size",
	    "X direction size of data array");
    print_i(out, pc.y_size, prefix, "y_size",
	    "Y direction size of data array");
    print_i(out, pc.z_size, prefix, "z_size",
	    "Z direction size of data array");
    print_i(out, pc.x_loc, prefix, "x_loc",
	    "X location of radar in data array (signed 1/1000 of pixels)");
    print_i(out, pc.y_loc, prefix, "y_loc",
	    "Y location of radar in data array (signed 1/1000 of pixels)");
    print_i(out, pc.z_loc, prefix, "z_loc",
	    "Z location of radar in data array (signed 1/1000 of pixels)");
    print_i(out, pc.max_rng, prefix, "max_rng",
	    "Maximum range in cm (used only in version 2.0, raw products)");
    print_u(out, pc.data_type, prefix, "data_type",
	    "Data type generated (See Section 3.8 for values)");
    print_s(out, pc.proj, prefix, "proj",
	    "Name of projection used");
    print_u(out, pc.inp_data_type, prefix, "inp_data_type",
	    "Data type used as input (See Section 3.8 for values)");
    print_u(out, pc.proj_type, prefix, "proj_type",
	    "Projection type: 0=Centered Azimuthal, 1=Mercator");
    print_i(out, pc.rad_smoother, prefix, "rad_smoother",
	    "Radial smoother in 1/100 of km");
    print_i(out, pc.num_runs, prefix, "num_runs",
	    "Number of times this product configuration has run");
    print_i(out, pc.zr_const, prefix, "zr_const",
	    "Z/R relationship constant in 1/1000");
    print_i(out, pc.zr_exp, prefix, "zr_exp",
	    "Z/R relationship exponent in 1/1000");
    print_i(out, pc.x_smooth, prefix, "x_smooth",
	    "X-direction smoother in 1/100 of km");
    print_i(out, pc.y_smooth, prefix, "y_smooth",
	    "Y-direction smoother in 1/100 of km");
    print_product_specific_info(out, prefix, pc.psi);
    print_s(out, pc.suffixes, prefix, "suffixes",
	    "List of minor task suffixes, null terminated");
    print_color_scale_def(out, prefix, pc.csd);
}

/* get / print product_specific_info. */

struct Sigmet_Product_Specific_Info get_product_specific_info(char *rec)
{
    struct Sigmet_Product_Specific_Info psi;

    psi.data_type_mask = get_uint32(rec + 0);
    psi.rng_last_bin = get_sint32(rec + 4);
    psi.format_conv_flag = get_uint32(rec + 8);
    psi.flag = get_uint32(rec + 12);
    psi.sweep_num = get_sint32(rec + 16);
    psi.xhdr_type = get_uint32(rec + 20);
    psi.data_type_mask1 = get_uint32(rec + 24);
    psi.data_type_mask2 = get_uint32(rec + 28);
    psi.data_type_mask3 = get_uint32(rec + 32);
    psi.data_type_mask4 = get_uint32(rec + 36);
    psi.playback_vsn = get_uint32(rec + 40);
    return psi;
}

void print_product_specific_info(FILE *out, char *pfx,
	struct Sigmet_Product_Specific_Info psi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_specific_info>.");
    print_x(out, psi.data_type_mask, prefix, "data_type_mask",
	    "Data type mask word 0");
    print_i(out, psi.rng_last_bin, prefix, "rng_last_bin",
	    "Range of last bin in cm");
    print_u(out, psi.format_conv_flag, prefix, "format_conv_flag",
	    "Format conversion flag: 0=Preserve all ingest data"
	    " 1=Convert 8-bit data to 16-bit data"
	    " 2=Convert 16-bit data to 8-bit data");
    print_x(out, psi.flag, prefix, "flag",
	    "Flag word: Bit 0=Separate product files by sweep Bit 1=Mask data"
	    " by supplied mask");
    print_i(out, psi.sweep_num, prefix, "sweep_num",
	    "Sweep number if separate files, origin 1");
    print_u(out, psi.xhdr_type, prefix, "xhdr_type",
	    "Xhdr type (unused)");
    print_x(out, psi.data_type_mask1, prefix, "data_type_mask1",
	    "Data type mask 1");
    print_x(out, psi.data_type_mask2, prefix, "data_type_mask2",
	    "Data type mask 2");
    print_x(out, psi.data_type_mask3, prefix, "data_type_mask3",
	    "Data type mask 3");
    print_x(out, psi.data_type_mask4, prefix, "data_type_mask4",
	    "Data type mask 4");
    print_u(out, psi.playback_vsn, prefix, "playback_vsn",
	    "Playback version (low 16-bits)");
}

/* get / print color_scale_def. */

struct Sigmet_Color_Scale_Def get_color_scale_def(char *rec)
{
    struct Sigmet_Color_Scale_Def csd;
    char *p, *p1;
    unsigned *q;

    csd.flags = get_uint32(rec + 0);
    csd.istart = get_sint32(rec + 4);
    csd.istep = get_sint32(rec + 8);
    csd.icolcnt = get_sint16(rec + 12);
    csd.iset_and_scale = get_uint16(rec + 14);
    q = csd.ilevel_seams;
    p = rec + 16;
    p1 = p + sizeof(*csd.ilevel_seams) * 16;
    for ( ; p < p1; p += sizeof(*csd.ilevel_seams), q++) {
	*q = get_uint16(p);
    }
    return csd;
}

void print_color_scale_def(FILE *out, char *pfx,
	struct Sigmet_Color_Scale_Def csd)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<color_scale_def>.");
    print_x(out, csd.flags, prefix, "flags",
	    "iflags: Bit 8=COLOR_SCALE_VARIABLE Bit 10=COLOR_SCALE_TOP_SAT Bit"
	    " 11=COLOR_SCALE_BOT_SAT");
    print_i(out, csd.istart, prefix, "istart",
	    "istart: Starting level");
    print_i(out, csd.istep, prefix, "istep",
	    "istep: Level step");
    print_i(out, csd.icolcnt, prefix, "icolcnt",
	    "icolcnt: Number of colors in scale");
    print_u(out, csd.iset_and_scale, prefix, "iset_and_scale",
	    "iset_and_scale: Color set number in low byte, color scale number"
	    " in high byte.");
    for (n = 0; n < 16; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix,
		"ilevel_seams[", n, "]");
	fprintf(out, "%u " FS " %s " FS " %s\n", csd.ilevel_seams[n], struct_path,
		"ilevel_seams: Variable level starting values");
    }
}

/* get / print product_end. */

struct Sigmet_Product_End get_product_end(char *rec)
{
    struct Sigmet_Product_End pe;

    memcpy(pe.site_name_prod, rec + 0, 16);
    trimRight(pe.site_name_prod, 16);
    memcpy(pe.iris_prod_vsn, rec + 16, 8);
    trimRight(pe.iris_prod_vsn, 8);
    memcpy(pe.iris_ing_vsn, rec + 24, 8);
    trimRight(pe.iris_ing_vsn, 8);
    pe.local_wgmt = get_sint16(rec + 72);
    memcpy(pe.hw_name, rec + 74, 16);
    trimRight(pe.hw_name, 16);
    memcpy(pe.site_name_ing, rec + 90, 16);
    trimRight(pe.site_name_ing, 16);
    pe.rec_wgmt = get_sint16(rec + 106);
    pe.center_latitude = get_uint32(rec + 108);
    pe.center_longitude = get_uint32(rec + 112);
    pe.ground_elev = get_sint16(rec + 116);
    pe.radar_ht = get_sint16(rec + 118);
    pe.prf = get_sint32(rec + 120);
    pe.pulse_w = get_sint32(rec + 124);
    pe.proc_type = get_uint16(rec + 128);
    pe.trigger_rate_scheme = get_uint16(rec + 130);
    pe.num_samples = get_sint16(rec + 132);
    memcpy(pe.clutter_filter, rec + 134, 12);
    trimRight(pe.clutter_filter, 12);
    pe.lin_filter = get_uint16(rec + 146);
    pe.wave_len = get_sint32(rec + 148);
    pe.trunc_ht = get_sint32(rec + 152);
    pe.rng_bin0 = get_sint32(rec + 156);
    pe.rng_last_bin = get_sint32(rec + 160);
    pe.num_bins_out = get_sint32(rec + 164);
    pe.flag = get_uint16(rec + 168);
    pe.polarization = get_uint16(rec + 172);
    pe.hpol_io_cal = get_sint16(rec + 174);
    pe.hpol_cal_noise = get_sint16(rec + 176);
    pe.hpol_radar_const = get_sint16(rec + 178);
    pe.recv_bandw = get_uint16(rec + 180);
    pe.hpol_noise = get_sint16(rec + 182);
    pe.vpol_noise = get_sint16(rec + 184);
    pe.ldr_offset = get_sint16(rec + 186);
    pe.zdr_offset = get_sint16(rec + 188);
    pe.tcf_cal_flags = get_uint16(rec + 190);
    pe.tcf_cal_flags2 = get_uint16(rec + 192);
    pe.std_parallel1 = get_uint32(rec + 212);
    pe.std_parallel2 = get_uint32(rec + 216);
    pe.rearth = get_uint32(rec + 220);
    pe.flatten = get_uint32(rec + 224);
    pe.fault = get_uint32(rec + 228);
    pe.insites_mask = get_uint32(rec + 232);
    pe.logfilter_num = get_uint16(rec + 236);
    pe.cluttermap_used = get_uint16(rec + 238);
    pe.proj_lat = get_uint32(rec + 240);
    pe.proj_lon = get_uint32(rec + 244);
    pe.i_prod = get_sint16(rec + 248);
    pe.melt_level = get_sint16(rec + 282);
    pe.radar_ht_ref = get_sint16(rec + 284);
    pe.num_elem = get_sint16(rec + 286);
    pe.wind_spd = *(unsigned char *)(rec + 288);
    pe.wind_dir = *(unsigned char *)(rec + 289);
    memcpy(pe.tz, rec + 292, 8);
    trimRight(pe.tz, 8);
    return pe;
}

void print_product_end(FILE *out, char *pfx, struct Sigmet_Product_End pe)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_end>.");
    print_s(out, pe.site_name_prod, prefix, "site_name_prod",
	    "Site name -- where product was made (space padded)");
    print_s(out, pe.iris_prod_vsn, prefix, "iris_prod_vsn",
	    "IRIS version where product was made (null terminated)");
    print_s(out, pe.iris_ing_vsn, prefix, "iris_ing_vsn",
	    "IRIS version where ingest data came from");
    print_i(out, pe.local_wgmt, prefix, "local_wgmt",
	    "Number of minutes local standard time is west of GMT");
    print_s(out, pe.hw_name, prefix, "hw_name",
	    "Hardware name where ingest data came from (space padded)");
    print_s(out, pe.site_name_ing, prefix, "site_name_ing",
	    "Site name where ingest data came from (space padded)");
    print_i(out, pe.rec_wgmt, prefix, "rec_wgmt",
	    "Number of minutes recorded standard time is west of GMT");
    print_u(out, pe.center_latitude, prefix, "center_latitude",
	    "Latitude of center (binary angle) *");
    print_u(out, pe.center_longitude, prefix, "center_longitude",
	    "Longitude of center (binary angle) *");
    print_i(out, pe.ground_elev, prefix, "ground_elev",
	    "Signed ground height in meters relative to sea level");
    print_i(out, pe.radar_ht, prefix, "radar_ht",
	    "Height of radar above the ground in meters");
    print_i(out, pe.prf, prefix, "prf",
	    "PRF in hertz");
    print_i(out, pe.pulse_w, prefix, "pulse_w",
	    "Pulse width in 1/100 of microseconds");
    print_u(out, pe.proc_type, prefix, "proc_type",
	    "Type of signal processor used");
    print_u(out, pe.trigger_rate_scheme, prefix, "trigger_rate_scheme",
	    "Trigger rate scheme");
    print_i(out, pe.num_samples, prefix, "num_samples",
	    "Number of samples used");
    print_s(out, pe.clutter_filter, prefix, "clutter_filter",
	    "Clutter filter file name");
    print_u(out, pe.lin_filter, prefix, "lin_filter",
	    "Number of linear based filter for the first bin");
    print_i(out, pe.wave_len, prefix, "wave_len",
	    "Wavelength in 1/100 of centimeters");
    print_i(out, pe.trunc_ht, prefix, "trunc_ht",
	    "Truncation height (cm above the radar)");
    print_i(out, pe.rng_bin0, prefix, "rng_bin0",
	    "Range of the first bin in cm");
    print_i(out, pe.rng_last_bin, prefix, "rng_last_bin",
	    "Range of the last bin in cm");
    print_i(out, pe.num_bins_out, prefix, "num_bins_out",
	    "Number of output bins");
    print_x(out, pe.flag, prefix, "flag",
	    "Flag word Bit0:Disdrometer failed, we used setup for Z/R source"
	    " instead");
    print_u(out, pe.polarization, prefix, "polarization",
	    "Type of polarization used");
    print_i(out, pe.hpol_io_cal, prefix, "hpol_io_cal",
	    "I0 cal value, horizontal pol, in 1/100 dBm");
    print_i(out, pe.hpol_cal_noise, prefix, "hpol_cal_noise",
	    "Noise at calibration, horizontal pol, in 1/100 dBm");
    print_i(out, pe.hpol_radar_const, prefix, "hpol_radar_const",
	    "Radar constant, horizontal pol, in 1/100 dB");
    print_u(out, pe.recv_bandw, prefix, "recv_bandw",
	    "Receiver bandwidth in kHz");
    print_i(out, pe.hpol_noise, prefix, "hpol_noise",
	    "Current noise level, horizontal pol, in 1/100 dBm");
    print_i(out, pe.vpol_noise, prefix, "vpol_noise",
	    "Current noise level, vertical pol, in 1/100 dBm");
    print_i(out, pe.ldr_offset, prefix, "ldr_offset",
	    "LDR offset, in 1/100 dB");
    print_i(out, pe.zdr_offset, prefix, "zdr_offset",
	    "ZDR offset, in 1/100 dB");
    print_u(out, pe.tcf_cal_flags, prefix, "tcf_cal_flags",
	    "TCF Cal flags, see struct task_calib_info (added in 8.12.3)");
    print_u(out, pe.tcf_cal_flags2, prefix, "tcf_cal_flags2",
	    "TCF Cal flags2, see struct task_calib_info (added in 8.12.3)");
    print_u(out, pe.std_parallel1, prefix, "std_parallel1",
	    "More projection info these 4 words: Standard parallel #1");
    print_u(out, pe.std_parallel2, prefix, "std_parallel2",
	    "Standard parallel #2");
    print_u(out, pe.rearth, prefix, "rearth",
	    "Equatorial radius of the earth, cm (zero = 6371km sphere)");
    print_u(out, pe.flatten, prefix, "flatten",
	    "1/Flattening in 1/1000000 (zero = sphere)");
    print_u(out, pe.fault, prefix, "fault",
	    "Fault status of task, see ingest_configuration 3.2.14 "
	    " for details");
    print_u(out, pe.insites_mask, prefix, "insites_mask",
	    "Mask of input sites used in a composite");
    print_u(out, pe.logfilter_num, prefix, "logfilter_num",
	    "Number of log based filter for the first bin");
    print_u(out, pe.cluttermap_used, prefix, "cluttermap_used",
	    "Nonzero if cluttermap applied to the ingest data");
    print_u(out, pe.proj_lat, prefix, "proj_lat",
	    "Latitude of projection reference *");
    print_u(out, pe.proj_lon, prefix, "proj_lon",
	    "Longitude of projection reference *");
    print_i(out, pe.i_prod, prefix, "i_prod",
	    "Product sequence number");
    print_i(out, pe.melt_level, prefix, "melt_level",
	    "Melting level in meters, msb complemented (0=unknown)");
    print_i(out, pe.radar_ht_ref, prefix, "radar_ht_ref",
	    "Height of radar above reference height in meters");
    print_i(out, pe.num_elem, prefix, "num_elem",
	    "Number of elements in product results array");
    print_u(out, pe.wind_spd, prefix, "wind_spd",
	    "Mean wind speed");
    print_u(out, pe.wind_dir, prefix, "wind_dir",
	    "Mean wind direction (unknown if speed and direction 0)");
    print_s(out, pe.tz, prefix, "tz",
	    "TZ Name of recorded data");
}

/* get / print ingest header (a.k.a. raw volume record 2). */

struct Sigmet_Ingest_Header get_ingest_header(char *rec)
{
    struct Sigmet_Ingest_Header ih;

    ih.sh = get_structure_header(rec);
    rec += 12;
    ih.ic = get_ingest_configuration(rec);
    rec += 480;
    ih.tc = get_task_configuration(rec);
    return ih;
}

void print_ingest_header(FILE *out, char *prefix,
	struct Sigmet_Ingest_Header ih)
{
    print_structure_header(out, prefix, ih.sh);
    print_ingest_configuration(out, prefix, ih.ic);
    print_task_configuration(out, prefix, ih.tc);
}

/* get / print ingest_configuration. */

struct Sigmet_Ingest_Configuration get_ingest_configuration(char *rec)
{
    struct Sigmet_Ingest_Configuration ic;
    char *p, *p1;
    int *q;

    memcpy(ic.file_name, rec + 0, 80);
    trimRight(ic.file_name, 80);
    ic.num_assoc_files = get_sint16(rec + 80);
    ic.num_sweeps = get_sint16(rec + 82);
    ic.size_files = get_sint32(rec + 84);
    ic.vol_start_time = get_ymds_time(rec + 88);
    ic.ray_headers_sz = get_sint16(rec + 112);
    ic.extended_ray_headers_sz = get_sint16(rec + 114);
    ic.task_config_table_num = get_sint16(rec + 116);
    ic.playback_vsn = get_sint16(rec + 118);
    memcpy(ic.IRIS_vsn, rec + 124, 8);
    trimRight(ic.IRIS_vsn, 8);
    memcpy(ic.hw_site_name, rec + 132, 16);
    trimRight(ic.hw_site_name, 16);
    ic.local_wgmt = get_sint16(rec + 148);
    memcpy(ic.su_site_name, rec + 150, 16);
    trimRight(ic.su_site_name, 16);
    ic.rec_wgmt = get_sint16(rec + 166);
    ic.latitude = get_uint32(rec + 168);
    ic.longitude = get_uint32(rec + 172);
    ic.ground_elev = get_sint16(rec + 176);
    ic.radar_ht = get_sint16(rec + 178);
    ic.resolution = get_uint16(rec + 180);
    ic.index_first_ray = get_uint16(rec + 182);
    ic.num_rays = get_uint16(rec + 184);
    ic.num_bytes_gparam = get_sint16(rec + 186);
    ic.altitude = get_sint32(rec + 188);
    q = ic.velocity;
    p = rec + 192;
    p1 = p + sizeof(*ic.velocity) * 3;
    for ( ; p < p1; p += sizeof(*ic.velocity), q++) {
	*q = get_sint32(p);
    }
    q = ic.offset_inu;
    p = rec + 204;
    p1 = p + sizeof(*ic.velocity) * 3;
    for ( ; p < p1; p += sizeof(*ic.offset_inu), q++) {
	*q = get_sint32(p);
    }
    ic.fault = get_uint32(rec + 216);
    ic.melt_level = get_sint16(rec + 220);
    memcpy(ic.tz, rec + 224, 8);
    trimRight(ic.tz, 8);
    ic.flags = get_uint32(rec + 232);
    memcpy(ic.config_name, rec + 236, 16);
    trimRight(ic.config_name, 16);
    return ic;
}

void print_ingest_configuration(FILE *out, char *pfx,
	struct Sigmet_Ingest_Configuration ic)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<ingest_configuration>.");
    print_s(out, ic.file_name, prefix, "file_name",
	    "Name of file on disk");
    print_i(out, ic.num_assoc_files, prefix, "num_assoc_files",
	    "Number of associated data files extant");
    print_i(out, ic.num_sweeps, prefix, "num_sweeps",
	    "Number of sweeps completed so far");
    print_i(out, ic.size_files, prefix, "size_files",
	    "Total size of all files in bytes");
    print_ymds_time(out, ic.vol_start_time, prefix, "vol_start_time",
	    "Time that volume scan was started, TZ spec in bytes 166 & 224");
    print_i(out, ic.ray_headers_sz, prefix, "ray_headers_sz",
	    "Number of bytes in the ray headers");
    print_i(out, ic.extended_ray_headers_sz, prefix, "extended_ray_headers_sz",
	    "Number of bytes in extended ray headers"
	    " (includes normal ray header)");
    print_i(out, ic.task_config_table_num, prefix, "task_config_table_num",
	    "Number of task configuration table");
    print_i(out, ic.playback_vsn, prefix, "playback_vsn",
	    "Playback version number");
    print_s(out, ic.IRIS_vsn, prefix, "IRIS_vsn",
	    "IRIS version, null terminated");
    print_s(out, ic.hw_site_name, prefix, "hw_site_name",
	    "Hardware name of site");
    print_i(out, ic.local_wgmt, prefix, "local_wgmt",
	    "Time zone of local standard time, minutes west of GMT");
    print_s(out, ic.su_site_name, prefix, "su_site_name",
	    "Name of site, from setup utility");
    print_i(out, ic.rec_wgmt, prefix, "rec_wgmt",
	    "Time zone of recorded standard time, minutes west of GMT");
    print_u(out, ic.latitude, prefix, "latitude",
	    "Latitude of radar (binary angle: 20000000 hex is 45_ North)");
    print_u(out, ic.longitude, prefix, "longitude",
	    "Longitude of radar (binary angle: 20000000 hex is 45_ East)");
    print_i(out, ic.ground_elev, prefix, "ground_elev",
	    "Height of ground at site (meters above sea level)");
    print_i(out, ic.radar_ht, prefix, "radar_ht",
	    "Height of radar above ground (meters)");
    print_u(out, ic.resolution, prefix, "resolution",
	    "Resolution specified in number of rays in a 360_ sweep");
    print_u(out, ic.index_first_ray, prefix, "index_first_ray",
	    "Index of first ray from above set of rays");
    print_u(out, ic.num_rays, prefix, "num_rays",
	    "Number of rays in a sweep");
    print_i(out, ic.num_bytes_gparam, prefix, "num_bytes_gparam",
	    "Number of bytes in each gparam");
    print_i(out, ic.altitude, prefix, "altitude",
	    "Altitude of radar (cm above sea level)");
    print_i(out, ic.velocity[0], prefix, "velocity east",
	    "Velocity of radar platform (cm/sec) east");
    print_i(out, ic.velocity[1], prefix, "velocity north",
	    "Velocity of radar platform (cm/sec) north");
    print_i(out, ic.velocity[2], prefix, "velocity up",
	    "Velocity of radar platform (cm/sec) up");
    print_i(out, ic.offset_inu[0], prefix, "offset_inu starboard",
	    "Antenna offset from INU (cm) starboard");
    print_i(out, ic.offset_inu[1], prefix, "offset_inu bow",
	    "Antenna offset from INU (cm) bow");
    print_i(out, ic.offset_inu[2], prefix, "offset_inu up",
	    "Antenna offset from INU (cm) up");
    print_u(out, ic.fault, prefix, "fault",
	    "Fault status at the time the task was started, bits:"
	    " 0:Normal BITE 1:Critical BITE 2:Normal RCP 3:Critical RCP"
	    " 4:Critical system 5:Product gen. 6:Output 7:Normal system ");
    print_i(out, ic.melt_level, prefix, "melt_level",
	    "Height of melting layer (meters above sea level) MSB is"
	    " complemented, zero=Unknown");
    print_s(out, ic.tz, prefix, "tz",
	    "Local timezone string, null terminated");
    print_x(out, ic.flags, prefix, "flags",
	    "Flags, Bit 0=First ray not centered on zero degrees");
    print_s(out, ic.config_name, prefix, "config_name",
	    "Configuration name in the dpolapp.conf file, null terminated");
}

/* get / print task_configuration. */

struct Sigmet_Task_Configuration get_task_configuration(char *rec)
{
    struct Sigmet_Task_Configuration tc;

    tc.sh = get_structure_header(rec);
    rec += 12;
    tc.tsi = get_task_sched_info(rec);
    rec += 120;
    tc.tdi = get_task_dsp_info(rec);
    rec += 320;
    tc.tci = get_task_calib_info(rec);
    rec += 320;
    tc.tri = get_task_range_info(rec);
    rec += 160;
    tc.tni = get_task_scan_info(rec);
    rec += 320;
    tc.tmi = get_task_misc_info(rec);
    rec += 320;
    tc.tei = get_task_end_info(rec);
    return tc;
}

void print_task_configuration(FILE *out, char *pfx,
	struct Sigmet_Task_Configuration tc)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_configuration>.");
    print_structure_header(out, prefix, tc.sh);
    print_task_sched_info(out, prefix, tc.tsi);
    print_task_dsp_info(out, prefix, tc.tdi);
    print_task_calib_info(out, prefix, tc.tci);
    print_task_range_info(out, prefix, tc.tri);
    print_task_scan_info(out, prefix, tc.tni);
    print_task_misc_info(out, prefix, tc.tmi);
    print_task_end_info(out, prefix, tc.tei);
}

/* get / print task_sched_info. */

struct Sigmet_Task_Sched_Info get_task_sched_info(char *rec)
{
    struct Sigmet_Task_Sched_Info tsi;

    tsi.start_time = get_sint32(rec + 0);
    tsi.stop_time = get_sint32(rec + 4);
    tsi.skip = get_sint32(rec + 8);
    tsi.time_last_run = get_sint32(rec + 12);
    tsi.time_used_last_run = get_sint32(rec + 16);
    tsi.rel_day_last_run = get_sint32(rec + 20);
    tsi.flag = get_uint16(rec + 24);
    return tsi;
}

void print_task_sched_info(FILE *out, char *pfx,
	struct Sigmet_Task_Sched_Info tsi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_sched_info>.");
    print_i(out, tsi.start_time, prefix, "start_time",
	    "Start time (seconds within a day)");
    print_i(out, tsi.stop_time, prefix, "stop_time",
	    "Stop time (seconds within a day)");
    print_i(out, tsi.skip, prefix, "skip",
	    "Desired skip time (seconds)");
    print_i(out, tsi.time_last_run, prefix, "time_last_run",
	    "Time last run (seconds within a day)(0 for passive ingest)");
    print_i(out, tsi.time_used_last_run, prefix, "time_used_last_run",
	    "Time used on last run (seconds) (in file time to writeout)");
    print_i(out, tsi.rel_day_last_run, prefix, "rel_day_last_run",
	    "Relative day of last run (zero for passive ingest)");
    print_x(out, tsi.flag, prefix, "flag",
	    "Flag: Bit 0 = ASAP Bit 1 = Mandatory Bit 2 = Late skip"
	    " Bit 3 = Time used has been measured Bit 4 = Stop after running");
}

/* get / print task_dsp_mode_batch. */

struct Sigmet_Task_DSP_Mode_Batch get_task_dsp_mode_batch(char *rec)
{
    struct Sigmet_Task_DSP_Mode_Batch tdmb;

    tdmb.lo_prf = get_uint16(rec + 0);
    tdmb.lo_prf_frac = get_uint16(rec + 2);
    tdmb.lo_prf_sampl = get_sint16(rec + 4);
    tdmb.lo_prf_avg = get_sint16(rec + 6);
    tdmb.dz_unfold_thresh = get_sint16(rec + 8);
    tdmb.vr_unfold_thresh = get_sint16(rec + 10);
    tdmb.sw_unfold_thresh = get_sint16(rec + 12);
    return tdmb;
}

void print_task_dsp_mode_batch(FILE *out, char *pfx,
	struct Sigmet_Task_DSP_Mode_Batch tdmb)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_dsp_mode_batch>.");
    print_u(out, tdmb.lo_prf, prefix, "lo_prf",
	    "Low PRF in Hz");
    print_u(out, tdmb.lo_prf_frac, prefix, "lo_prf_frac",
	    "Low PRF fraction part, scaled by 2**­16");
    print_i(out, tdmb.lo_prf_sampl, prefix, "lo_prf_sampl",
	    "Low PRF sample size");
    print_i(out, tdmb.lo_prf_avg, prefix, "lo_prf_avg",
	    "Low PRF range averaging in bins");
    print_i(out, tdmb.dz_unfold_thresh, prefix, "dz_unfold_thresh",
	    "Theshold for reflectivity unfolding in 1/100 of dB");
    print_i(out, tdmb.vr_unfold_thresh, prefix, "vr_unfold_thresh",
	    "Threshold for velocity unfolding in 1/100 of dB");
    print_i(out, tdmb.sw_unfold_thresh, prefix, "sw_unfold_thresh",
	    "Threshold for width unfolding in 1/100 of dB");
}

/* get / print task_dsp_info. */

struct Sigmet_Task_DSP_Info get_task_dsp_info(char *rec)
{
    struct Sigmet_Task_DSP_Info tdi;

    tdi.major_mode = get_uint16(rec + 0);
    tdi.dsp_type = get_uint16(rec + 2);
    tdi.curr_data_mask = get_dsp_data_mask(rec + 4);
    tdi.orig_data_mask = get_dsp_data_mask(rec + 28);
    tdi.mb = get_task_dsp_mode_batch(rec + 52);
    tdi.prf = get_sint32(rec + 136);
    tdi.pulse_w = get_sint32(rec + 140);
    tdi.m_prf_mode = (enum Sigmet_Multi_PRF)get_uint16(rec + 144);
    tdi.dual_prf = get_sint16(rec + 146);
    tdi.agc_feebk = get_uint16(rec + 148);
    tdi.sampl_sz = get_sint16(rec + 150);
    tdi.gain_flag = get_uint16(rec + 152);
    memcpy(tdi.clutter_file, rec + 154, 12);
    trimRight(tdi.clutter_file, 12);
    tdi.lin_filter_num = *(unsigned char *)(rec + 166);
    tdi.log_filter_num = *(unsigned char *)(rec + 167);
    tdi.attenuation = get_sint16(rec + 168);
    tdi.gas_attenuation = get_uint16(rec + 170);
    tdi.clutter_flag = get_uint16(rec + 172);
    tdi.xmt_phase = get_uint16(rec + 174);
    tdi.ray_hdr_mask = get_uint32(rec + 176);
    tdi.time_series_flag = get_uint16(rec + 180);
    memcpy(tdi.custom_ray_hdr, rec + 184, 16);
    trimRight(tdi.custom_ray_hdr, 16);
    return tdi;
}

void print_task_dsp_info(FILE *out, char *pfx,
	struct Sigmet_Task_DSP_Info tdi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_dsp_info>.");
    print_u(out, tdi.major_mode, prefix, "major_mode",
	    "Major mode");
    print_u(out, tdi.dsp_type, prefix, "dsp_type",
	    "DSP type");
    print_dsp_data_mask(out, prefix, tdi.curr_data_mask,
	    "Current Data type mask");
    print_dsp_data_mask(out, prefix, tdi.orig_data_mask,
	    "Original Data type mask");
    print_task_dsp_mode_batch(out, prefix, tdi.mb);
    print_i(out, tdi.prf, prefix, "prf",
	    "PRF in Hertz");
    print_i(out, tdi.pulse_w, prefix, "pulse_w",
	    "Pulse width in 1/100 of microseconds");
    print_i(out, tdi.m_prf_mode, prefix, "m_prf_mode",
	    "Multi PRF mode flag: 0=1:1, 1=2:3, 2=3:4, 3=4:5");
    print_i(out, tdi.dual_prf, prefix, "dual_prf",
	    "Dual PRF delay");
    print_u(out, tdi.agc_feebk, prefix, "agc_feebk",
	    "AGC feedback code");
    print_i(out, tdi.sampl_sz, prefix, "sampl_sz",
	    "Sample size");
    print_u(out, tdi.gain_flag, prefix, "gain_flag",
	    "Gain Control flag (0=fixed, 1=STC, 2=AGC)");
    print_s(out, tdi.clutter_file, prefix, "clutter_file",
	    "Name of file used for clutter filter");
    print_u(out, tdi.lin_filter_num, prefix, "lin_filter_num",
	    "Linear based filter number for first bin");
    print_u(out, tdi.log_filter_num, prefix, "log_filter_num",
	    "Log based filter number for first bin");
    print_i(out, tdi.attenuation, prefix, "attenuation",
	    "Attenuation in 1/10 dB applied in fixed gain mode");
    print_u(out, tdi.gas_attenuation, prefix, "gas_attenuation",
	    "Gas attenuation in 1/100000 dB/km for first 10000, then"
	    " stepping in 1/10000 dB/km");
    print_u(out, tdi.clutter_flag, prefix, "clutter_flag",
	    "Flag nonzero means cluttermap used");
    print_u(out, tdi.xmt_phase, prefix, "xmt_phase",
	    "XMT phase sequence: 0:Fixed, 1:Random, 3:SZ8/64");
    print_x(out, tdi.ray_hdr_mask, prefix, "ray_hdr_mask",
	    "Mask used for to configure the ray header.");
    print_u(out, tdi.time_series_flag, prefix, "time_series_flag",
	    "Time series playback flags, see OPTS_* in dsp.h");
    print_s(out, tdi.custom_ray_hdr, prefix, "custom_ray_hdr",
	    "Name of custom ray header");
}

/* get / print task_calib_info. */

struct Sigmet_Task_Calib_Info get_task_calib_info(char *rec)
{
    struct Sigmet_Task_Calib_Info tci;

    tci.dbz_slope = get_sint16(rec + 0);
    tci.dbz_noise_thresh = get_sint16(rec + 2);
    tci.clutter_corr_thesh = get_sint16(rec + 4);
    tci.sqi_thresh = get_sint16(rec + 6);
    tci.pwr_thresh = get_sint16(rec + 8);
    tci.cal_dbz = get_sint16(rec + 18);
    tci.dbt_flags = get_uint16(rec + 20);
    tci.dbz_flags = get_uint16(rec + 22);
    tci.vel_flags = get_uint16(rec + 24);
    tci.sw_flags = get_uint16(rec + 26);
    tci.zdr_flags = get_uint16(rec + 28);
    tci.flags = get_uint16(rec + 36);
    tci.ldr_bias = get_sint16(rec + 40);
    tci.zdr_bias = get_sint16(rec + 42);
    tci.nx_clutter_thresh = get_sint16(rec + 44);
    tci.nx_clutter_skip = get_uint16(rec + 46);
    tci.hpol_io_cal = get_sint16(rec + 48);
    tci.vpol_io_cal = get_sint16(rec + 50);
    tci.hpol_noise = get_sint16(rec + 52);
    tci.vpol_noise = get_sint16(rec + 54);
    tci.hpol_radar_const = get_sint16(rec + 56);
    tci.vpol_radar_const = get_sint16(rec + 58);
    tci.bandwidth = get_uint16(rec + 60);
    tci.flags2 = get_uint16(rec + 62);
    return tci;
}

void print_task_calib_info(FILE *out, char *pfx,
	struct Sigmet_Task_Calib_Info tci)
{
    char prefix[STR_LEN];
    char *desc;

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_calib_info>.");
    print_i(out, tci.dbz_slope, prefix, "dbz_slope",
	    "Reflectivity slope (4096*dB/ A/D count)");
    print_i(out, tci.dbz_noise_thresh, prefix, "dbz_noise_thresh",
	    "Reflectivity noise threshold (1/16 dB above Noise)");
    print_i(out, tci.clutter_corr_thesh, prefix, "clutter_corr_thesh",
	    "Clutter Correction threshold (1/16 dB)");
    print_i(out, tci.sqi_thresh, prefix, "sqi_thresh",
	    "SQI threshold (0­1)*256");
    print_i(out, tci.pwr_thresh, prefix, "pwr_thresh",
	    "Power threshold (1/16 dBZ)");
    print_i(out, tci.cal_dbz, prefix, "cal_dbz",
	    "Calibration Reflectivity (1/16 dBZ at 1 km)");
    print_u(out, tci.dbt_flags, prefix, "dbt_flags",
	    "Threshold flags for uncorrected reflectivity");
    print_u(out, tci.dbz_flags, prefix, "dbz_flags",
	    "Threshold flags for corrected reflectivity");
    print_u(out, tci.vel_flags, prefix, "vel_flags",
	    "Threshold flags for velocity");
    print_u(out, tci.sw_flags, prefix, "sw_flags",
	    "Threshold flags for width");
    print_u(out, tci.zdr_flags, prefix, "zdr_flags",
	    "Threshold flags for ZDR");
    desc = "Flags. Bits: 0: Speckle remover for log channel 3: Speckle"
	" remover for linear channel 4:  data is range normalized 5:  pulse"
	" at beginning of ray 6:  pulse at end of ray 7: Vary number of"
	" pulses in dual PRF 8: Use 3 lag processing in PP02 9: Apply vel"
	" correction for ship motion 10: Vc is unfolded 11: Vc has fallspeed"
	" correction 12: Zc has beam blockage correction 13: Zc has Z-based"
	" attenuation correction 14: Zc has target detection 15: Vc has storm"
	" relative vel correction";
    print_u(out, tci.flags, prefix, "flags", desc);
    print_i(out, tci.ldr_bias, prefix, "ldr_bias",
	    "LDR bias in signed 1/100 dB");
    print_i(out, tci.zdr_bias, prefix, "zdr_bias",
	    "ZDR bias in signed 1/16 dB");
    print_i(out, tci.nx_clutter_thresh, prefix, "nx_clutter_thresh",
	    "NEXRAD point clutter threshold in 1/100 of dB");
    print_u(out, tci.nx_clutter_skip, prefix, "nx_clutter_skip",
	    "NEXRAD point clutter bin skip in low 4 bits");
    print_i(out, tci.hpol_io_cal, prefix, "hpol_io_cal",
	    "I0 cal value, horizontal pol, in 1/100 dBm");
    print_i(out, tci.vpol_io_cal, prefix, "vpol_io_cal",
	    "I0 cal value, vertical pol, in 1/100 dBm");
    print_i(out, tci.hpol_noise, prefix, "hpol_noise",
	    "Noise at calibration, horizontal pol, in 1/100 dBm");
    print_i(out, tci.vpol_noise, prefix, "vpol_noise",
	    "Noise at calibration, vertical pol, in 1/100 dBm");
    print_i(out, tci.hpol_radar_const, prefix, "hpol_radar_const",
	    "Radar constant, horizontal pol, in 1/100 dB");
    print_i(out, tci.vpol_radar_const, prefix, "vpol_radar_const",
	    "Radar constant, vertical pol, in 1/100 dB");
    print_u(out, tci.bandwidth, prefix, "bandwidth",
	    "Receiver bandwidth in kHz");
    print_x(out, tci.flags2, prefix, "flags2",
	    "Flags2: Bit 0: Zc and ZDRc has DP attenuation correction"
	    " Bit 1: Z and ZDR has DP attenuation correction");
}

/* get / print task_range_info. */

struct Sigmet_Task_Range_Info get_task_range_info(char *rec)
{
    struct Sigmet_Task_Range_Info tri;

    tri.rng_1st_bin = get_sint32(rec + 0);
    tri.rng_last_bin = get_sint32(rec + 4);
    tri.num_bins_in = get_sint16(rec + 8);
    tri.num_bins_out = get_sint16(rec + 10);
    tri.step_in = get_sint32(rec + 12);
    tri.step_out = get_sint32(rec + 16);
    tri.flag = get_uint16(rec + 20);
    tri.rng_avg_flag = get_sint16(rec + 22);
    return tri;
}

void print_task_range_info(FILE *out, char *pfx,
	struct Sigmet_Task_Range_Info tri)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_range_info>.");
    print_i(out, tri.rng_1st_bin, prefix, "rng_1st_bin",
	    "Range of first bin in centimeters");
    print_i(out, tri.rng_last_bin, prefix, "rng_last_bin",
	    "Range of last bin in centimeters");
    print_i(out, tri.num_bins_in, prefix, "num_bins_in",
	    "Number of input bins");
    print_i(out, tri.num_bins_out, prefix, "num_bins_out",
	    "Number of output range bins");
    print_i(out, tri.step_in, prefix, "step_in",
	    "Step between input bins");
    print_i(out, tri.step_out, prefix, "step_out",
	    "Step between output bins (in centimeters)");
    print_u(out, tri.flag, prefix, "flag",
	    "Flag for variable range bin spacing (1=var, 0=fixed)");
    print_i(out, tri.rng_avg_flag, prefix, "rng_avg_flag",
	    "Range bin averaging flag");
}

/* get / print task_scan_info. */

struct Sigmet_Task_Scan_Info get_task_scan_info(char *rec)
{
    struct Sigmet_Task_Scan_Info tsi;

    switch (get_uint16(rec + 0)) {
	case 1:
	    tsi.scan_mode = PPI_S;
	    break;
	case 2:
	    tsi.scan_mode = RHI;
	    break;
	case 3:
	    tsi.scan_mode = MAN_SCAN;
	    break;
	case 4:
	    tsi.scan_mode = PPI_C;
	    break;
	case 5:
	    tsi.scan_mode = FILE_SCAN;
	    break;
    }
    tsi.resoln = get_sint16(rec + 2);
    tsi.num_sweeps = get_sint16(rec + 6);
    switch (tsi.scan_mode) {
	case RHI:
	    tsi.scan_info.rhi_info = get_task_rhi_scan_info(rec + 8);
	    break;
	case PPI_S:
	case PPI_C:
	    tsi.scan_info.ppi_info = get_task_ppi_scan_info(rec + 8);
	    break;
	case FILE_SCAN:
	    tsi.scan_info.file_info = get_task_file_scan_info(rec + 8);
	    break;
	case MAN_SCAN:
	    tsi.scan_info.man_info = get_task_manual_scan_info(rec + 8);
	    break;
    }
    return tsi;
}

void print_task_scan_info(FILE *out, char *pfx,
	struct Sigmet_Task_Scan_Info tsi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_scan_info>.");
    print_u(out, tsi.scan_mode, prefix, "scan_mode",
	    "Antenna scan mode 1:PPI sector, 2:RHI, 3:Manual,"
	    " 4:PPI cont, 5:file");
    print_i(out, tsi.resoln, prefix, "resoln",
	    "Desired angular resolution in 1/1000 of degrees");
    print_i(out, tsi.num_sweeps, prefix, "num_sweeps",
	    "Number of sweeps to perform");
    switch (tsi.scan_mode) {
	case RHI:
	    print_task_rhi_scan_info(out, prefix, tsi.scan_info.rhi_info);
	    break;
	case PPI_S:
	case PPI_C:
	    print_task_ppi_scan_info(out, prefix, tsi.scan_info.ppi_info);
	    break;
	case FILE_SCAN:
	    print_task_file_scan_info(out, prefix, tsi.scan_info.file_info);
	    break;
	case MAN_SCAN:
	    print_task_manual_scan_info(out, prefix, tsi.scan_info.man_info);
	    break;
    }
}

/* get / print task_rhi_scan_info. */

struct Sigmet_Task_RHI_Scan_Info get_task_rhi_scan_info(char *rec)
{
    struct Sigmet_Task_RHI_Scan_Info trsi;
    char *p, *p1;
    unsigned *q;

    trsi.lo_elev = get_uint16(rec + 0);
    trsi.hi_elev = get_uint16(rec + 2);
    q = trsi.az;
    p = rec + 4;
    p1 = p + sizeof(U16BIT) * 40;
    for ( ; p < p1; p += sizeof(U16BIT), q++) {
	*q = get_uint16(p);
    }
    trsi.start = *(unsigned char *)(rec + 199);
    return trsi;
}

void print_task_rhi_scan_info(FILE *out, char *pfx,
	struct Sigmet_Task_RHI_Scan_Info trsi)
{
    int n;
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<rhi_scan_info>.");
    print_u(out, trsi.lo_elev, prefix, "lo_elev",
	    "Lower elevation limit (binary angle, only for sector)");
    print_u(out, trsi.hi_elev, prefix, "hi_elev",
	    "Upper elevation limit (binary angle, only for sector)");
    for (n = 0; n < 40; n++) {
	fprintf(out, "%u " FS " %s%s%d%s " FS " %s\n", trsi.az[n], prefix,
		"az[", n, "]", "List of azimuths (binary angles) to scan at");
    }
    print_u(out, trsi.start, prefix, "start",
	    "Start of first sector sweep: 0=Nearest, 1=Lower,"
	    " 2=Upper Sector sweeps alternate in direction.");
}

/* get / print task_ppi_scan_info. */

struct Sigmet_Task_PPI_Scan_Info get_task_ppi_scan_info(char *rec)
{
    struct Sigmet_Task_PPI_Scan_Info tpsi;
    char *p, *p1;
    unsigned *q;

    tpsi.left_az = get_uint16(rec + 0);
    tpsi.right_az = get_uint16(rec + 2);
    q = tpsi.elevs;
    p = rec + 4;
    p1 = p + sizeof(U16BIT) * 40;
    for ( ; p < p1; p += sizeof(U16BIT), q++) {
	*q = get_uint16(p);
    }
    tpsi.start = *(unsigned char *)(rec + 199);
    return tpsi;
}

void print_task_ppi_scan_info(FILE *out, char *pfx,
	struct Sigmet_Task_PPI_Scan_Info tpsi)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_ppi_scan_info>.");
    print_u(out, tpsi.left_az, prefix, "left_az",
	    "Left azimuth limit (binary angle, only for sector)");
    print_u(out, tpsi.right_az, prefix, "right_az",
	    "Right azimuth limit (binary angle, only for sector)");
    for (n = 0; n < 40; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix, "elevs[", n,
		"]");
	fprintf(out, "%u " FS " %s " FS " %s\n", tpsi.elevs[n], struct_path,
		"List of elevations (binary angles) to scan at");
    }
    print_u(out, tpsi.start, prefix, "start",
	    "Start of first sector sweep: 0=Nearest, 1=Left, 2=Right Sector"
	    " sweeps alternate in direction.");
}

/* get / print task_file_scan_info. */

struct Sigmet_Task_File_Scan_Info get_task_file_scan_info(char *rec)
{
    struct Sigmet_Task_File_Scan_Info tfsi;

    tfsi.az0 = get_uint16(rec + 0);
    tfsi.elev0 = get_uint16(rec + 2);
    memcpy(tfsi.ant_ctrl, rec + 4, 12);
    trimRight(tfsi.ant_ctrl, 12);
    return tfsi;
}

void print_task_file_scan_info(FILE *out, char *pfx,
	struct Sigmet_Task_File_Scan_Info tfsi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_file_scan_info>.");
    print_u(out, tfsi.az0, prefix, "az0",
	    "First azimuth angle (binary angle)");
    print_u(out, tfsi.elev0, prefix, "elev0",
	    "First elevation angle (binary angle)");
    print_s(out, tfsi.ant_ctrl, prefix, "ant_ctrl",
	    "Filename for antenna control");
}

/* get / print task_manual_scan_info. */

struct Sigmet_Task_Manual_Scan_Info get_task_manual_scan_info(char *rec)
{
    struct Sigmet_Task_Manual_Scan_Info tmsi;

    tmsi.flags = get_uint16(rec + 0);
    return tmsi;
}

void print_task_manual_scan_info(FILE *out, char *pfx,
	struct Sigmet_Task_Manual_Scan_Info tmsi)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_manual_scan_info>.");
    print_x(out, tmsi.flags, prefix, "flags",
	    "Flags: bit 0=Continuous recording");
}

/* get / print task_misc_info. */

struct Sigmet_Task_Misc_Info get_task_misc_info(char *rec)
{
    struct Sigmet_Task_Misc_Info tmi;
    char *p, *p1;
    unsigned *q;

    tmi.wave_len = get_sint32(rec + 0);
    memcpy(tmi.tr_ser, rec + 4, 16);
    trimRight(tmi.tr_ser, 16);
    tmi.power = get_sint32(rec + 20);
    tmi.flags = get_uint16(rec + 24);
    tmi.polarization = get_uint16(rec + 26);
    tmi.trunc_ht = get_sint32(rec + 28);
    tmi.comment_sz = get_sint16(rec + 62);
    tmi.horiz_beam_width = get_uint32(rec + 64);
    tmi.vert_beam_width = get_uint32(rec + 68);
    q = tmi.custom;
    p = rec + 72;
    p1 = p + sizeof(*tmi.custom) * 10;
    for ( ; p < p1; p += sizeof(*tmi.custom), q++) {
	*q = get_uint32(p);
    }
    return tmi;
}

void print_task_misc_info(FILE *out, char *pfx,
	struct Sigmet_Task_Misc_Info tmi)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_misc_info>.");
    print_i(out, tmi.wave_len, prefix, "wave_len",
	    "Wavelength in 1/100 of cm");
    print_s(out, tmi.tr_ser, prefix, "tr_ser",
	    "T/R Serial Number");
    print_i(out, tmi.power, prefix, "power",
	    "Transmit Power in watts");
    print_x(out, tmi.flags, prefix, "flags",
	    "Flags: Bit 0: Digital signal simulator in use"
	    " Bit 1: Polarization in use Bit 4: Keep bit");
    print_u(out, tmi.polarization, prefix, "polarization",
	    "Type of polarization");
    print_i(out, tmi.trunc_ht, prefix, "trunc_ht",
	    "Truncation height (centimeters above the radar)");
    print_i(out, tmi.comment_sz, prefix, "comment_sz",
	    "Number of bytes of comments entered");
    print_u(out, tmi.horiz_beam_width, prefix, "horiz_beam_width",
	    "Horizontal beamwidth (binary angle, starting in 7.18)");
    print_u(out, tmi.vert_beam_width, prefix, "vert_beam_width",
	    "Vertical beamwidth (binary angle, starting in 7.18)");
    for (n = 0; n < 10; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix, "custom[", n, "]");
	fprintf(out, "%u " FS " %s " FS " %s\n", tmi.custom[n], struct_path,
		"Customer defined storage (starting in 7.27)");
    }
}

struct Sigmet_Task_End_Info get_task_end_info(char *rec)
{
    struct Sigmet_Task_End_Info tei;

    tei.task_major = get_sint16(rec + 0);
    tei.task_minor = get_sint16(rec + 2);
    memcpy(tei.task_config, rec + 4, 12);
    trimRight(tei.task_config, 12);
    memcpy(tei.task_descr, rec + 16, 80);
    trimRight(tei.task_descr, 80);
    tei.hybrid_ntasks = get_sint32(rec + 96);
    tei.task_state = get_uint16(rec + 100);
    tei.data_time = get_ymds_time(rec + 104);
    return tei;
}

void print_task_end_info(FILE *out, char *pfx,
	struct Sigmet_Task_End_Info tei)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_end_info>.");
    print_i(out, tei.task_major, prefix, "task_major",
	    "Task major number");
    print_i(out, tei.task_minor, prefix, "task_minor",
	    "Task minor number");
    print_s(out, tei.task_config, prefix, "task_config",
	    "Name of task configuration file");
    print_s(out, tei.task_descr, prefix, "task_descr",
	    "Task description");
    print_i(out, tei.hybrid_ntasks, prefix, "hybrid_ntasks",
	    "Number of tasks in hybrid task");
    print_u(out, tei.task_state, prefix, "task_state",
	    "Task state: 0=no task; 1=task being modified; 2=inactive;"
	    " 3=scheduled, 4=running.");
    print_ymds_time(out, tei.data_time, prefix, "data_time",
	    "Data time of task (TZ flexible)");
}

/* get / print dsp_data_mask. */

struct Sigmet_DSP_Data_Mask get_dsp_data_mask(char *rec)
{
    struct Sigmet_DSP_Data_Mask ddm;

    ddm.mask_word_0 = get_uint32(rec + 0);
    ddm.ext_hdr_type = get_uint32(rec + 4);
    ddm.mask_word_1 = get_uint32(rec + 8);
    ddm.mask_word_2 = get_uint32(rec + 12);
    ddm.mask_word_3 = get_uint32(rec + 16);
    ddm.mask_word_4 = get_uint32(rec + 20);
    return ddm;
}

void print_dsp_data_mask(FILE *out, char *pfx, struct Sigmet_DSP_Data_Mask ddm,
	char *suffix)
{
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<dsp_data_mask>.");
    snprintf(struct_path, STR_LEN, "%s%s",
	    prefix, "mask_word_0");
    fprintf(out, "%#X " FS " %s " FS " %s.  %s\n",
	    ddm.mask_word_0, struct_path, "Mask word 0", suffix);
    snprintf(struct_path, STR_LEN, "%s%s",
	    prefix, "ext_hdr_type");
    fprintf(out, "%u " FS " %s " FS " %s.  %s\n",
	    ddm.ext_hdr_type, struct_path, "Extended header type", suffix);
    snprintf(struct_path, STR_LEN, "%s%s",
	    prefix, "mask_word_1");
    fprintf(out, "%#X " FS " %s " FS " %s.  %s\n", ddm.mask_word_1, struct_path,
	    "Mask word 1 Contains bits set for all data recorded.", suffix);
    snprintf(struct_path, STR_LEN, "%s%s",
	    prefix, "mask_word_2");
    fprintf(out, "%#X " FS " %s " FS " %s.  %s\n", ddm.mask_word_2, struct_path,
	    "Mask word 2 See parameter DB_* in Table 3­6 for", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix,
	    "mask_word_3");
    fprintf(out, "%#X " FS " %s " FS " %s.  %s\n", ddm.mask_word_3, struct_path,
	    "Mask word 3 bit specification.", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_4");
    fprintf(out, "%#X " FS " %s " FS " %s.  %s\n", ddm.mask_word_4, struct_path,
	    "Mask word 4", suffix);
}

/* get / print structure_header. */

struct Sigmet_Structure_Header get_structure_header(char *rec)
{
    struct Sigmet_Structure_Header sh;

    sh.id = get_sint16(rec + 0);
    sh.format = get_sint16(rec + 2);
    sh.sz = get_sint32(rec + 4);
    sh.flags = get_sint16(rec + 10);
    return sh;
}

void print_structure_header(FILE *out, char *prefix,
	struct Sigmet_Structure_Header sh)
{
    print_i(out, sh.id, prefix, "<structure_header>.id",
	    "Structure identifier: 22 => Task_configuration."
	    " 23 => Ingest_header. 24 => Ingest_data_header."
	    " 25 => Tape_inventory. 26 => Product_configuration."
	    " 27 => Product_hdr. 28 => Tape_header_record");
    print_i(out, sh.format, prefix, "<structure_header>.format",
	    "Format version number (see headers.h)");
    print_i(out, sh.sz, prefix, "<structure_header>.sz",
	    "Number of bytes in the entire structure");
    print_x(out, (unsigned)sh.flags, prefix, "<structure_header>.flags",
	    "Flags: bit0=structure complete");
}

/* get / print ymds_time. */

struct Sigmet_YMDS_Time get_ymds_time(char *b)
{
    unsigned msec;
    struct Sigmet_YMDS_Time tm;

    tm.sec = get_sint32(b);
    msec = get_uint16(b + 4);
    tm.msec = (msec & 0x3ff);
    tm.utc = (msec & 0x800);
    tm.year = get_sint16(b + 6);
    tm.month = get_sint16(b + 8);
    tm.day = get_sint16(b + 10);
    return tm;
}

void print_ymds_time(FILE *out, struct Sigmet_YMDS_Time tm, char *prefix,
	char *mmb, char *desc)
{
    double fhour, fmin;
    double ihour, imin;
    double sec;
    char struct_path[STR_LEN];

    sec = tm.sec + 0.001 * tm.msec;
    fhour = modf(sec / 3600.0, &ihour);
    fmin = modf(fhour * 60.0, &imin);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, mmb);
    fprintf(out, "%04d/%02d/%02d %02d:%02d:%05.2f. " FS " %s " FS " %s\n",
	    tm.year, tm.month, tm.day,
	    (int)ihour, (int)imin, fmin * 60.0,
	    struct_path, desc);
}

/*
   Print unsigned integer u (a struct value), prefix (a struct hierarchy),
   mmb (the struct member with value u), and desc (descriptor) to stream out.
 */
void print_u(FILE *out, unsigned u, char *prefix, char *mmb, char *desc)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, mmb);
    fprintf(out, "%u " FS " %s " FS " %s\n", u, struct_path, desc);
}

/*
   Print unsigned integer u (a struct value) in hex format, prefix (a struct
   hierarchy), mmb (the struct member with value u), and desc (descriptor) to
   stream out.
 */
void print_x(FILE *out, unsigned u, char *prefix, char *mmb, char *desc)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, mmb);
    fprintf(out, "%#X " FS " %s " FS " %s\n", u, struct_path, desc);
}

/*
   Print integer i (a struct value), prefix (a struct hierarchy),
   mmb (the struct member with value i), and desc (descriptor) to stream out.
 */
void print_i(FILE *out, int i, char *prefix, char *mmb, char *desc)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, mmb);
    fprintf(out, "%d " FS " %s " FS " %s\n", i, struct_path, desc);
}

/*
   Print string s (a struct value), prefix (a struct hierarchy),
   mmb (the struct member with value s), and desc (descriptor) to stream out.
 */
void print_s(FILE *out, char *s, char *prefix, char *mmb, char *desc)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, mmb);
    fprintf(out, "%s " FS " %s " FS " %s\n", s, struct_path, desc);
}

/*
   Trim spaces off the end of a character array
   Returns the input string with a nul character at the start of any
   trailing spaces and at end of string. The input string is modified.
 */

static char *trimRight(char *s, int n)
{
    char *c, *e, *end;

    for (c = e = s, end = s + n; c < end; c++) {
	if ( !isspace((int)*c) ) {
	    e = c;
	}
    }
    if (e + 1 != end) {
	*(e + 1) = '\0';
    }
    *(end - 1) = '\0';
    return s;
}

/* Retrieve a 16 bit signed integer from an address */
static int get_sint16(void *b) {
    I16BIT *s = (I16BIT *)(b);
    Swap_16Bit(s);
    return *s;
}

/* Retrieve a 16 bit unsigned integer from an address */
static unsigned get_uint16(void *b) {
    U16BIT *s = (U16BIT *)(b);
    Swap_16Bit(s);
    return *s;
}

/* Retrieve a 32 bit signed integer from an address */
static int get_sint32(void *b) {
    I32BIT *s = (I32BIT *)(b);
    Swap_32Bit(s);
    return *s;
}

/* Retrieve a 32 bit unsigned integer from an address */
static unsigned get_uint32(void *b) {
    U32BIT *s = (U32BIT *)(b);
    Swap_32Bit(s);
    return *s;
}

/* This function applies swapping to an array of 16 byte integers */
static void swap_arr16(void *r, int nw) {
    U16BIT *p, *pe;

    for (p = r, pe = p + nw; p < pe; p++) {
	Swap_16Bit(p);
    }
}

/* Allocate a 2 dimensional array of doubles */
static double ** calloc2d(long j, long i)
{
    double **dat = NULL;
    long n;
    size_t jj, ii;		/* Addends for pointer arithmetic */
    size_t ji;

    /* Make sure casting to size_t does not overflow anything. */
    if (j <= 0 || i <= 0) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    jj = (size_t)j;
    ii = (size_t)i;
    ji = jj * ii;
    if (ji / jj != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (double **)CALLOC(jj + 2, sizeof(double *));
    if ( !dat ) {
	Err_Append("Could not allocate memory for 1st dimension of two dimensional"
		" array.\n");
	return NULL;
    }
    dat[0] = (double *)CALLOC(ji, sizeof(double));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate memory for values of two dimensional "
		"array.\n");
	return NULL;
    }
    for (n = 1; n <= j; n++) {
	dat[n] = dat[n - 1] + i;
    }
    return dat;
}

/* Free array created by calloc2d */
static void free2d(double **dat)
{
    if (dat && dat[0]) {
	FREE(dat[0]);
    }
    FREE(dat);
}

/* Allocate a 2 dimensional array of ints */
static int ** calloc2i(long j, long i)
{
    int **dat = NULL;
    long n;
    size_t jj, ii;		/* Addends for pointer arithmetic */
    size_t ji;

    /* Make sure casting to size_t does not overflow anything. */
    if (j <= 0 || i <= 0) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    jj = (size_t)j;
    ii = (size_t)i;
    ji = jj * ii;
    if (ji / jj != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (int **)CALLOC(jj + 2, sizeof(int *));
    if ( !dat ) {
	Err_Append("Could not allocate memory for 1st dimension of two dimensional"
		" array.\n");
	return NULL;
    }
    dat[0] = (int *)CALLOC(ji, sizeof(int));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate memory for values of two dimensional "
		"array.\n");
	return NULL;
    }
    for (n = 1; n <= j; n++) {
	dat[n] = dat[n - 1] + i;
    }
    return dat;
}

/* Free array created by calloc2i */
static void free2i(int **dat)
{
    if (dat && dat[0]) {
	FREE(dat[0]);
    }
    FREE(dat);
}

/*
   Allocate a 3 dimensional array of unsigned one byte integers.  Return the
   array. If something goes wrong, post an error message with Err_Append and
   return NULL.
 */
static U1BYT ***calloc3_u1(long kmax, long jmax, long imax)
{
    U1BYT ***dat;
    long k, j;
    size_t kk, jj, ii;

    /* Make sure casting to size_t does not overflow anything. */
    if ( kmax <= 0 || jmax <= 0 || imax <= 0 ) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    kk = (size_t)kmax;
    jj = (size_t)jmax;
    ii = (size_t)imax;
    if ( (kk * jj) / kk != jj || (kk * jj * ii) / (kk * jj) != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (U1BYT ***)CALLOC(kk + 2, sizeof(U1BYT **));
    if ( !dat ) {
	Err_Append("Could not allocate 3rd dimension.\n");
	return NULL;
    }
    dat[0] = (U1BYT **)CALLOC(kk * jj + 1, sizeof(U1BYT *));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate 2nd dimension.\n");
	return NULL;
    }
    dat[0][0] = (U1BYT *)CALLOC(kk * jj * ii + 1, sizeof(U1BYT));
    if ( !dat[0][0] ) {
	FREE(dat[0]);
	FREE(dat);
	Err_Append("Could not allocate 1st dimension.\n");
	return NULL;
    }
    for (k = 1; k <= kmax; k++) {
	dat[k] = dat[k - 1] + jmax;
    }
    for (j = 1; j <= kmax * jmax; j++) {
	dat[0][j] = dat[0][j - 1] + imax;
    }
    return dat;
}

/* Free array created by calloc3_u1 */
static void free3_u1(U1BYT ***dat)
{
    if (dat) {
	if (dat[0]) {
	    if (dat[0][0]) {
		FREE(dat[0][0]);
	    }
	    FREE(dat[0]);
	}
	FREE(dat);
    }
}

/*
   Allocate a 3 dimensional array of unsigned two byte integers.  Return the
   array. If something goes wrong, post an error message with Err_Append and
   return NULL.
 */
static U2BYT ***calloc3_u2(long kmax, long jmax, long imax)
{
    U2BYT ***dat;
    long k, j;
    size_t kk, jj, ii;

    /* Make sure casting to size_t does not overflow anything. */
    if ( kmax <= 0 || jmax <= 0 || imax <= 0 ) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    kk = (size_t)kmax;
    jj = (size_t)jmax;
    ii = (size_t)imax;
    if ( (kk * jj) / kk != jj || (kk * jj * ii) / (kk * jj) != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (U2BYT ***)CALLOC(kk + 2, sizeof(U2BYT **));
    if ( !dat ) {
	Err_Append("Could not allocate 3rd dimension.\n");
	return NULL;
    }
    dat[0] = (U2BYT **)CALLOC(kk * jj + 1, sizeof(U2BYT *));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate 2nd dimension.\n");
	return NULL;
    }
    dat[0][0] = (U2BYT *)CALLOC(kk * jj * ii + 1, sizeof(U2BYT));
    if ( !dat[0][0] ) {
	FREE(dat[0]);
	FREE(dat);
	Err_Append("Could not allocate 1st dimension.\n");
	return NULL;
    }
    for (k = 1; k <= kmax; k++) {
	dat[k] = dat[k - 1] + jmax;
    }
    for (j = 1; j <= kmax * jmax; j++) {
	dat[0][j] = dat[0][j - 1] + imax;
    }
    return dat;
}

/* Free array created by calloc3_u1 */
static void free3_u2(U2BYT ***dat)
{
    if (dat) {
	if (dat[0]) {
	    if (dat[0][0]) {
		FREE(dat[0][0]);
	    }
	    FREE(dat[0]);
	}
	FREE(dat);
    }
}

/*
   Allocate a 3 dimensional array of floats.  Return the array. If something
   goes wrong, post an error message with Err_Append and return NULL.
 */
static float ***calloc3_flt(long kmax, long jmax, long imax)
{
    float ***dat, *d;
    long k, j;
    size_t kk, jj, ii;

    /* Make sure casting to size_t does not overflow anything. */
    if ( kmax <= 0 || jmax <= 0 || imax <= 0 ) {
	Err_Append("Array dimensions must be positive.\n");
	return NULL;
    }
    kk = (size_t)kmax;
    jj = (size_t)jmax;
    ii = (size_t)imax;
    if ( (kk * jj) / kk != jj || (kk * jj * ii) / (kk * jj) != ii) {
	Err_Append("Dimensions too big for pointer arithmetic.\n");
	return NULL;
    }

    dat = (float ***)CALLOC(kk + 2, sizeof(float **));
    if ( !dat ) {
	Err_Append("Could not allocate 3rd dimension.\n");
	return NULL;
    }
    dat[0] = (float **)CALLOC(kk * jj + 1, sizeof(float *));
    if ( !dat[0] ) {
	FREE(dat);
	Err_Append("Could not allocate 2nd dimension.\n");
	return NULL;
    }
    dat[0][0] = (float *)CALLOC(kk * jj * ii + 1, sizeof(float));
    if ( !dat[0][0] ) {
	FREE(dat[0]);
	FREE(dat);
	Err_Append("Could not allocate 1st dimension.\n");
	return NULL;
    }
    for (k = 1; k <= kmax; k++) {
	dat[k] = dat[k - 1] + jmax;
    }
    for (j = 1; j <= kmax * jmax; j++) {
	dat[0][j] = dat[0][j - 1] + imax;
    }
    for (d = dat[0][0]; d < dat[0][0] + kk * jj * ii + 1; d++) {
	*d = Sigmet_NoData();
    }
    return dat;
}

/* Free array created by calloc3_flt */
static void free3_flt(float ***dat)
{
    if (dat) {
	if (dat[0]) {
	    if (dat[0][0]) {
		FREE(dat[0][0]);
	    }
	    FREE(dat[0]);
	}
	FREE(dat);
    }
}
