/*
   -	sigmet_vol.c--
   -		Define functions that store and access
   -		information from Sigmet raw product
   -		volumes.  See sigmet (3).
   -
   .	Copyright (c) 2004 Gordon D. Carrie
   .	All rights reserved.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.4 $ $Date: 2009/10/21 15:25:45 $
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
#include "sigmet.h"

/* Header sizes in data record */
#define SZ_RAW_PROD_BHDR 12
#define SZ_INGEST_DATA_HDR 76
#define SZ_RAY_HDR 12

/* Sigmet scan modes */
enum SCAN_MODE {ppi_sec = 1, rhi, man, ppi_cont, file};

static char *trimRight(char *s, int n);
static int get_sint16(void *b);
static unsigned get_uint16(void *b);
static int get_sint32(void *b);
static unsigned get_uint32(void *b);
static void swap_arr16(void *r, int nw);

/* Default length for character strings */
#define STR_LEN 512

/* Functions to read and print Sigmet raw volume structures */
static struct ymds_time get_ymds_time(char *);
static void print_ymds_time(char *, struct ymds_time, char *, FILE *);
static struct structure_header get_structure_header(char *);
static void print_structure_header(char *, struct structure_header, FILE *);
static struct product_specific_info get_product_specific_info(char *);
static void print_product_specific_info(char *, struct product_specific_info, FILE *);
static struct color_scale_def get_color_scale_def(char *);
static void print_color_scale_def(char *, struct color_scale_def, FILE *);
static struct product_configuration get_product_configuration(char *);
static void print_product_configuration(char *, struct product_configuration, FILE *);
static struct product_end get_product_end(char *);
static void print_product_end(char *, struct product_end, FILE *);
static struct product_hdr get_product_hdr(char *);
static void print_product_hdr(char *, struct product_hdr, FILE *);
static struct ingest_configuration get_ingest_configuration(char *);
static void print_ingest_configuration(char *, struct ingest_configuration, FILE *);
static struct task_sched_info get_task_sched_info(char *);
static void print_task_sched_info(char *, struct task_sched_info, FILE *);
static struct dsp_data_mask get_dsp_data_mask(char *);
static void print_dsp_data_mask(char *, struct dsp_data_mask, char *, FILE *);
static struct task_dsp_mode_batch get_task_dsp_mode_batch(char *);
static void print_task_dsp_mode_batch(char *, struct task_dsp_mode_batch, FILE *);
static struct task_dsp_info get_task_dsp_info(char *);
static void print_task_dsp_info(char *, struct task_dsp_info, FILE *);
static struct task_calib_info get_task_calib_info(char *);
static void print_task_calib_info(char *, struct task_calib_info, FILE *);
static struct task_range_info get_task_range_info(char *);
static void print_task_range_info(char *, struct task_range_info, FILE *);
static struct task_rhi_scan_info get_task_rhi_scan_info(char *);
static void print_task_rhi_scan_info(char *, struct task_rhi_scan_info, FILE *);
static struct task_ppi_scan_info get_task_ppi_scan_info(char *);
static void print_task_ppi_scan_info(char *, struct task_ppi_scan_info, FILE *);
static struct task_file_scan_info get_task_file_scan_info(char *);
static void print_task_file_scan_info(char *, struct task_file_scan_info, FILE *);
static struct task_manual_scan_info get_task_manual_scan_info(char *);
static void print_task_manual_scan_info(char *, struct task_manual_scan_info, FILE *);
static struct task_scan_info get_task_scan_info(char *);
static void print_task_scan_info(char *, struct task_scan_info, FILE *);
static struct task_misc_info get_task_misc_info(char *);
static void print_task_misc_info(char *, struct task_misc_info, FILE *);
static struct task_end_info get_task_end_info(char *);
static void print_task_end_info(char *, struct task_end_info, FILE *);
static struct task_configuration get_task_configuration(char *);
static void print_task_configuration(char *, struct task_configuration, FILE *);
static struct ingest_header get_ingest_header(char *);
static void print_ingest_header(char *, struct ingest_header, FILE *);

/* Print functions */
static void print_u(unsigned , char *, char *, char *, FILE *);
static void print_x(unsigned , char *, char *, char *, FILE *);
static void print_i(int , char *, char *, char *, FILE *);
static void print_s(char *, char *, char *, char *, FILE *);

/* Initialize a Sigmet raw volume structure. */
void Sigmet_InitVol(struct Sigmet_Vol *sigPtr)
{
    int n;

    if (!sigPtr) {
	return;
    }
    memset(sigPtr, 0, sizeof(struct Sigmet_Vol));
    sigPtr->num_types = SIGMET_NTYPES;
    for (n = 0; n < SIGMET_NTYPES; n++) {
	sigPtr->types[n] = DB_XHDR;
    }
    sigPtr->sweep_time = NULL;
    sigPtr->sweep_angle = NULL;
    sigPtr->ray_tilt0 = sigPtr->ray_tilt1
	= sigPtr->ray_az0 = sigPtr->ray_az1 = NULL;
    sigPtr->dat = NULL;
    sigPtr->truncated = 0;
    return;
}

/* Free storage associated with a Sigmet raw volume. */
void Sigmet_FreeVol(struct Sigmet_Vol *sigPtr)
{
    if (!sigPtr) {
	return;
    }
    FREE(sigPtr->sweep_time);
    FREE(sigPtr->sweep_angle);
    FREE(sigPtr->ray_time);
    FREE(sigPtr->ray_nbins);
    FREE(sigPtr->ray_tilt0);
    FREE(sigPtr->ray_tilt1);
    FREE(sigPtr->ray_az0);
    FREE(sigPtr->ray_az1);
    FREE(sigPtr->dat);
    Sigmet_InitVol(sigPtr);
}

/* Read and store a headers from a Sigmet raw product volume. */
int Sigmet_ReadHdr(FILE *f, struct Sigmet_Vol *sigPtr)
{
    char rec[REC_LEN];			/* Input record from file */

    /*
       n_types_fl will receive the number of types in the f.
       types_fl will store type identifiers from the file.
       n_types will receive the number of types in the volume.
       If the volume uses extended headers, n_types_fl = n_types+1,
       and types_fl will have one more element than sigPtr->types.
       The extra type is the "extended header type".
     */
    int n_types_fl;
    int n_types;
    enum Sigmet_DataType types_fl[SIGMET_NTYPES];
    int y;				/* Type index (0 based) */
    int haveXHDR = 0;			/* If true, volume has extended headers */

    /* These masks are placed against the data type mask in the volume structure
       to determine what data types are in the volume. */
    static unsigned type_masks[SIGMET_NTYPES] = {
	(1 <<  0), (1 <<  1), (1 <<  2), (1 <<  3), (1 <<  4),
	(1 <<  5), (1 <<  7), (1 <<  8), (1 <<  9), (1 << 10),
	(1 << 11), (1 << 12), (1 << 13), (1 << 14), (1 << 15),
	(1 << 16), (1 << 17), (1 << 18), (1 << 19), (1 << 20),
	(1 << 21), (1 << 22), (1 << 23), (1 << 24), (1 << 25),
	(1 << 26), (1 << 27), (1 << 28)
    };
    unsigned data_type_mask;

    /* record 1, <product_header> */
    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	Err_Append("Could not read record 1 of Sigmet volume.  ");
	goto error;
    }

    /*
       If first 16 bits of product header != 27, turn on byte swapping
       and check again.  If still not 27, give up.
     */
    if (get_sint16(rec) != 27) {
	Toggle_Swap();
	if (get_sint16(rec) != 27) {
	    Err_Append( "Sigmet volume has bad magic number (should be 27).  ");
	    return 0;
	}
    }

    sigPtr->ph = get_product_hdr(rec);

    /* record 2, <ingest_header> */
    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
	Err_Append("Could not read record 2 of Sigmet volume.  ");
	goto error;
    }
    sigPtr->ih = get_ingest_header(rec);

    /*
       Loop through the bits in the data type mask.  If bit is set, add the
       corresponding type to types array.
     */

    data_type_mask = sigPtr->ih.tc.tdi.curr_data_mask.mask_word_0;
    for (y = 0, n_types_fl = 0, n_types = 0; y < SIGMET_NTYPES; y++) {
	if (data_type_mask & type_masks[y]) {
	    if (y == DB_XHDR) {
		haveXHDR = 1;
	    } else {
		sigPtr->types[n_types] = y;
		n_types++;
	    }
	    types_fl[n_types_fl] = y;
	    n_types_fl++;
	}
    }
    sigPtr->num_types = n_types;

    return 1;

error:
    Sigmet_FreeVol(sigPtr);
    return 0;
}

/* Write headers to a file in ASCII */
void Sigmet_PrintHdr(struct Sigmet_Vol sig_vol, FILE *out)
{
    int y;
    char elem_nm[STR_LEN];

    print_product_hdr("<product_hdr>.", sig_vol.ph, out);
    print_ingest_header("<ingest_hdr>.", sig_vol.ih, out);
    fprintf(out, "%d ! %s ! %s\n", sig_vol.num_types, "num_types", "Number of Sigmet data types");
    for (y = 0; y < sig_vol.num_types; y++) {
	snprintf(elem_nm, STR_LEN, "%s%d%s", "types[", y, "]");
	fprintf(out, "%s ! %s ! %s\n", Sigmet_DataType_Abbrv(sig_vol.types[y]), elem_nm, Sigmet_DataType_Descr(sig_vol.types[y]));
    }
}

/* Read and store a Sigmet volume. */
int Sigmet_ReadVol(FILE *f, struct Sigmet_Vol *sigPtr)
{
    char rec[REC_LEN];			/* Input record from file */


    unsigned short *recP;		/* Pointer into rec */
    unsigned short *recN;		/* Stopping point in rec */
    unsigned short *recEnd = (unsigned short *)(rec + REC_LEN);
    /* End of rec */
    int iRec;				/* Current record index (0 is first) */
    short nSwp;				/* Current sweep number (1 is first) */

    int n_sweeps;			/* Number of sweeps in sig_vol */
    int n_types_fl;			/* Number of types in the file.
					 * This will be one more than
					 * n_types if the volume contains
					 * extended headers. */
    int n_types;			/* Number of types in the volume. */
    int n_rays;				/* Number of rays per sweep */
    int n_rays_tot;			/* Total number of rays */
    int n_bins;				/* Number of output bins */

    /*
       Array of types in the file.  This will be different from the types
       array in sigPtr  if the volume contains extended headers.
     */
    enum Sigmet_DataType types_fl[SIGMET_NTYPES];

    int year, month, day, sec, msec;
    double swpTm;
    double angle;			/* Sweep angle.  Ref. geography (n) */

    int haveXHDR = 0;			/* true => volume uses extended headers */
    int have_hdrs;			/* If true, ray headers have been stored */

    unsigned short *ray = NULL;		/* Buffer ray, receives data from rec */
    unsigned short *rayP = NULL;	/* Point into ray while looping */

    size_t raySz;			/* Allocation size for a ray */
    unsigned char *rayd;		/* Pointer to start of data in ray */
    size_t sz;				/* Tmp value */
    unsigned short numWds;		/* Number of words in a run of data */
    int s, y, r;			/* Sweep, type, ray indeces (0 based) */
    int i, n, ne;			/* Temporary values */
    int *d, *e;

    unsigned char *cPtr;		/* Pointer into ray (1 byte values) */
    unsigned char *cePtr;		/* End of ray */
    unsigned short *sPtr;		/* Pointer into ray (2 byte values) */
    unsigned short *sePtr;		/* End of ray */
    int *df;				/* Pointer into ray in sigPtr
					 * structure when data will
					 * be stored in memory as
					 * floats */
    int tm_incr;			/* Ray time adjustment */

    /* Read headers */
    if ( !Sigmet_ReadHdr(f, sigPtr) ) {
	Err_Append("Could not read volume headers.\n");
	goto error;
    }

    /*
     * Allocate arrays in sigPtr.
     */

    n_sweeps = sigPtr->ih.tc.tni.n_sweeps;
    n_rays = sigPtr->ih.ic.rays_in_sweep;
    n_rays_tot = n_sweeps * n_rays;
    n_bins = sigPtr->ph.pe.n_out_bins;
    sigPtr->sweep_time = (double *)MALLOC(n_sweeps * sizeof(double));
    sigPtr->sweep_angle = (double *)MALLOC(n_sweeps * sizeof(double));
    sz = n_sweeps * (sizeof(double *) + n_rays * sizeof(double));
    sigPtr->ray_time = (double **)MALLOC(sz);
    sigPtr->ray_time[0] = (double *)(sigPtr->ray_time + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_time[s] = sigPtr->ray_time[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(unsigned *) + n_rays * sizeof(unsigned));
    sigPtr->ray_nbins = (unsigned **)MALLOC(sz);
    sigPtr->ray_nbins[0] = (unsigned *)(sigPtr->ray_nbins + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_nbins[s] = sigPtr->ray_nbins[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(double *) + n_rays * sizeof(double));
    sigPtr->ray_tilt0 = (double **)MALLOC(sz);
    sigPtr->ray_tilt0[0] = (double *)(sigPtr->ray_tilt0 + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_tilt0[s] = sigPtr->ray_tilt0[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(double *) + n_rays * sizeof(double));
    sigPtr->ray_tilt1 = (double **)MALLOC(sz);
    sigPtr->ray_tilt1[0] = (double *)(sigPtr->ray_tilt1 + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_tilt1[s] = sigPtr->ray_tilt1[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(double *) + n_rays * sizeof(double));
    sigPtr->ray_az0 = (double **)MALLOC(sz);
    sigPtr->ray_az0[0] = (double *)(sigPtr->ray_az0 + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_az0[s] = sigPtr->ray_az0[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(double *) + n_rays * sizeof(double));
    sigPtr->ray_az1 = (double **)MALLOC(sz);
    sigPtr->ray_az1[0] = (double *)(sigPtr->ray_az1 + n_sweeps);
    for (s = 1; s < n_sweeps; s++) {
	sigPtr->ray_az1[s] = sigPtr->ray_az1[s - 1] + n_rays;
    }

    sz = n_sweeps * (sizeof(int ***) + n_types * (sizeof(int **) 
		+ n_rays * (sizeof(int *) + n_bins * sizeof(int))));
    sigPtr->dat = (int ****)MALLOC(sz);
    sigPtr->dat[0] = (int ***)(sigPtr->dat + n_sweeps);
    sigPtr->dat[0][0] = (int **)(sigPtr->dat[0] + n_sweeps * n_types);
    sigPtr->dat[0][0][0] = (int *)(sigPtr->dat[0][0] + n_sweeps * n_types * n_rays);
    for (n = 1, ne = n_sweeps; n < ne; n++) {
	sigPtr->dat[n] = sigPtr->dat[n - 1] + n_types;
    }
    for (n = 1, ne = n_sweeps * n_types; n < ne; n++) {
	sigPtr->dat[0][n] = sigPtr->dat[0][n - 1] + n_rays;
    }
    for (n = 1, ne = n_sweeps * n_types * n_rays; n < ne; n++) {
	sigPtr->dat[0][0][n] = sigPtr->dat[0][0][n - 1] + n_bins;
    }
    d = sigPtr->dat[0][0][0];
    e = d + n_sweeps * n_types * n_rays *  n_bins;
    while (d < e) {
	*d++ = Sigmet_NoData();
    }

    /*
     * Allocate and partition largest possible ray buffer.
     * Data will be decompressed from rec and copied into ray.
     */

    raySz = SZ_RAY_HDR + sigPtr->ih.ic.extended_ray_headers_sz
	+ sigPtr->ph.pe.n_out_bins * sizeof(unsigned short);
    ray = (unsigned short *)MALLOC(raySz);
    rayd = (unsigned char *)ray + SZ_RAY_HDR;

    /*
     * Process data records
     */

    iRec = 1;
    nSwp = 0;
    while (fread(rec, 1, REC_LEN, f) == REC_LEN) {

	/*
	 * Get record number and sweep number from <raw_prod_bhdr>
	 */

	i = get_sint16(rec);
	n = get_sint16(rec + 2);

	if (i != iRec + 1) {
	    Err_Append(
		    "Sigmet raw product file records out of sequence.  ");
	    goto error;
	}
	iRec = i;

	if (n != nSwp) {
	    /*
	     * Record is start of new sweep.
	     */

	    nSwp = n;
	    s = nSwp - 1;
	    r = 0;

	    /*
	     * If sweep number from <ingest_data_header> has gone back to 0,
	     * there are no more sweeps in volume.
	     */

	    n = get_sint16(rec + 36);
	    if (n == 0) {
		sigPtr->ih.tc.tni.n_sweeps = nSwp - 1;
		break;
	    }

	    /*
	     * Store sweep time and angle (from first <ingest_data_header>)
	     */

	    sec = get_sint32(rec + 24);
	    msec = get_uint16(rec + 28);
	    year = get_sint16(rec + 30);
	    month = get_sint16(rec + 32);
	    day = get_sint16(rec + 34);
	    if (year == 0 || month == 0 || day == 0) {
		Err_Append("Garbled sweep.  ");
		goto error;
	    }

	    /*
	     * Set sweep in volume
	     */

	    swpTm = Tm_CalToJul(year, month, day, 0, 0, sec + 0.001 * msec);
	    angle = Sigmet_Bin2Rad(get_uint16(rec + 46));
	    sigPtr->sweep_time[s] = swpTm;
	    sigPtr->sweep_angle[s] = angle;

	    /*
	     * Byte swap data segment in record, if necessary
	     */

	    recP = (unsigned short *)(rec + SZ_RAW_PROD_BHDR
		    + n_types_fl * SZ_INGEST_DATA_HDR);
	    swap_arr16(recP, recEnd - recP);

	    /*
	     * Initialize ray
	     */

	    have_hdrs = 0;
	    memset(ray, 0, raySz);
	    rayP = ray;
	    y = 0;

	} else {

	    /*
	     * Record continues a sweep started in an earlier record
	     * Byte swap data segment in record, if necessary
	     */

	    recP = (unsigned short *)(rec + SZ_RAW_PROD_BHDR);
	    swap_arr16(recP, recEnd - recP);

	}

	/*
	 * Decompress and store ray data.
	 * Reference: IRIS/Open Programmers Manual, April 2000, p. 3-38 to 3-40.
	 */

	while (recP < recEnd) {

	    if ((0x8000 & *recP) == 0x8000) {
		/*
		 * Run of data words
		 */

		numWds = 0x7FFF & *recP;
		if (numWds > recEnd - recP - 1) {
		    /*
		     * Data run crosses record boundary.
		     * Store number of words in second part of data run.
		     * We will need this when we get to the next record.
		     */

		    numWds = numWds - (recEnd - recP - 1);

		    /*
		     * Read rest of current record
		     */

		    for (recP++; recP < recEnd; recP++, rayP++) {
			*rayP = *recP;
		    }

		    /*
		     * Get next record.  Check record number from
		     * <raw_prod_bhdr>.
		     */

		    if (fread(rec, 1, REC_LEN, f) != REC_LEN) {
			Err_Append( "Failed to read record "
				"from Sigmet raw file.  ");
			goto error;
		    }
		    i = get_sint16(rec);
		    if (i != iRec + 1) {
			Err_Append("Sigmet raw product file records "
				"out of sequence.  ");
			goto error;
		    }
		    iRec = i;

		    /*
		     * Position record pointer at start of data segment
		     * and byte swap data segment in record, if necessary
		     */

		    recP = (unsigned short *)(rec + SZ_RAW_PROD_BHDR);
		    swap_arr16(recP, recEnd - recP);

		    /*
		     * Get second part of data run from the new record.
		     */

		    for (recN = recP + numWds; recP < recN; recP++, rayP++) {
			*rayP = *recP;
		    }

		} else {
		    /*
		     * Current record contains entire data run
		     */

		    for (recP++, recN = recP + numWds; recP < recN;
			    recP++, rayP++) {
			*rayP = *recP;
		    }
		}
	    } else if (*recP == 1) {
		/*
		 * End of ray.
		 */

		if (s > n_sweeps) {
		    Err_Append("Volume storage went beyond"
			    " maximum sweep count.  ");
		    goto error;
		}
		if (r > n_rays) {
		    Err_Append("Volume storage went beyond"
			    " maximum ray count.  ");
		    goto error;
		}
		sigPtr->ray_az0[s][r] = Sigmet_Bin2Rad(ray[0]);
		sigPtr->ray_tilt0[s][r] = Sigmet_Bin2Rad(ray[1]);
		sigPtr->ray_az1[s][r] = Sigmet_Bin2Rad(ray[2]);
		sigPtr->ray_tilt1[s][r] = Sigmet_Bin2Rad(ray[3]);
		sigPtr->ray_nbins[s][r] = ray[4];
		if ( !haveXHDR ) {
		    sigPtr->ray_time[s][r] = swpTm + ray[5];
		}

		/*
		 * Store ray data.
		 */

		switch (types_fl[y]) {
		    case DB_XHDR:
			/*
			 * For extended header, undo the call to
			 * swap_arr16 above, then apply byte swapping
			 * to the raw input.
			 */

			swap_arr16(ray + SZ_RAY_HDR, 2);
			tm_incr = get_sint32(ray + SZ_RAY_HDR);
			sigPtr->ray_time[s][r] = swpTm + 0.001 * tm_incr;
			break;
		    case DB_DBT:
		    case DB_DBZ:
		    case DB_VEL:
		    case DB_WIDTH:
		    case DB_ZDR:
		    case DB_DBZC:
		    case DB_KDP:
		    case DB_PHIDP:
		    case DB_VELC:
		    case DB_SQI:
		    case DB_RHOHV:
		    case DB_LDRH:
		    case DB_LDRV:
			for (cPtr = rayd,
				cePtr = cPtr + sigPtr->ray_nbins[s][r],
				df = sigPtr->dat[s][y - haveXHDR][r];
				cPtr < cePtr;
				cPtr++, df++)  {
			    *df = Sigmet_DataType_ItoF(types_fl[y], *cPtr);
			}
			break;
		    case DB_DBT2:
		    case DB_DBZ2:
		    case DB_VEL2:
		    case DB_WIDTH2:
		    case DB_ZDR2:
		    case DB_RAINRATE2:
		    case DB_KDP2:
		    case DB_RHOHV2:
		    case DB_DBZC2:
		    case DB_VELC2:
		    case DB_SQI2:
		    case DB_PHIDP2:
		    case DB_LDRH2:
		    case DB_LDRV2:
			for (sPtr = (unsigned short *)rayd,
				sePtr = sPtr + sigPtr->ray_nbins[s][r],
				df = sigPtr->dat[s][y - haveXHDR][r];
				sPtr < sePtr; sPtr++, df++) {
			    *df = Sigmet_DataType_ItoF(types_fl[y], *sPtr);
			}
			break;
		}

		/*
		 * Reset for next ray
		 */

		memset(ray, 0, raySz);
		rayP = ray;
		if (++y == n_types_fl) {
		    r++;
		    y = 0;
		    have_hdrs = 0;
		}
		recP++;
	    } else {
		/*
		 * Run of zeros
		 */

		numWds = 0x7FFF & *recP;
		rayP += numWds;
		recP++;
	    }
	}
    }

    if ( !feof(f) ) {
	sigPtr->truncated = 1;
    }
    FREE(ray);
    return 1;

error:
    FREE(ray);
    Sigmet_FreeVol(sigPtr);
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * Sigmet_BadRay --
 *
 *	This function indicates whether a ray is bad.
 *
 * Results:
 *	Return value is true if ray s, r is bogus.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

int Sigmet_BadRay(sigPtr, s, r)
    struct Sigmet_Vol *sigPtr;		/* Sigmet volume */

    int s;				/* Sweep index */
    int r;				/* Ray index */
{
    return sigPtr->ray_az0[s][r] == sigPtr->ray_az1[s][r];
}

/* get and/or print product_hdr (a.k.a. raw volume record 1) */

struct product_hdr get_product_hdr(char *rec)
{
    struct product_hdr ph;

    ph.sh = get_structure_header(rec);
    ph.pc = get_product_configuration(rec);
    rec += 12;
    ph.pe = get_product_end(rec);
    rec += 320;
    return ph;
}

void print_product_hdr(char *prefix, struct product_hdr ph, FILE *out)
{
    print_structure_header("<product_hdr>.", ph.sh, out);
    print_product_configuration("<product_hdr>.",  ph.pc, out);
    print_product_end("<product_hdr>.", ph.pe, out);
}

/* get and/or print product_configuration */

struct product_configuration get_product_configuration(char *rec)
{
    struct product_configuration pc;

    pc.sh = get_structure_header(rec);
    pc.type = get_uint16(rec + 12);
    pc.schedule = get_uint16(rec + 14);
    pc.skip = get_sint32(rec + 16);
    pc.gen_tm = get_ymds_time(rec + 20);
    pc.ingest_sweep_tm = get_ymds_time(rec + 32);
    pc.ingest_file_tm = get_ymds_time(rec + 44);
    strncpy(pc.config_file, rec + 62, 12);
    trimRight(pc.config_file, 12);
    strncpy(pc.task_name, rec + 74, 12);
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
    strncpy(pc.proj, rec + 132, 12);
    trimRight(pc.proj, 12);
    pc.inp_data_type = get_uint16(rec + 144);
    pc.proj_type = *(unsigned char *)(rec + 146);
    pc.rad_smoother = get_sint16(rec + 148);
    pc.run_cnt = get_sint16(rec + 150);
    pc.zr_const = get_sint32(rec + 152);
    pc.zr_exp = get_sint32(rec + 156);
    pc.x_smooth = get_sint16(rec + 160);
    pc.y_smooth = get_sint16(rec + 162);
    pc.psi = get_product_specific_info(rec + 80);
    strncpy(pc.suffixes, rec + 244, 16);
    trimRight(pc.suffixes, 16);
    pc.csd = get_color_scale_def(rec + 272);
    return pc;
}

void print_product_configuration(char *pfx, struct product_configuration pc, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_configuration>.");
    print_structure_header(prefix, pc.sh, out);
    print_u(pc.type, prefix, "type", "Product type code: 1:PPI 2:RHI 3:CAPPI 4:CROSS 5:TOPS 6:TRACK 7:RAIN1 8:RAINN 9:VVP 10:VIL 11:SHEAR 12:WARN 13:CATCH 14:RTI 15:RAW 16:MAX 17:USER 18:USERV 19:OTHER 20:STATUS 21:SLINE 22:WIND 23:BEAM 24:TEXT 25:FCAST 26:NDOP 27:IMAGE 28:COMP 29:TDWR 30:GAGE 31:DWELL 32:SRI 33:BASE 34:HMAX", out);
    print_u(pc.schedule, prefix, "schedule", "Scheduling code: 0:hold; 1:next; 2:all", out);
    print_i(pc.skip, prefix, "skip", "Number of seconds to skip between runs", out);
    print_ymds_time(prefix, pc.gen_tm, "Time product was generated (UTC)", out);
    print_ymds_time(prefix, pc.ingest_sweep_tm, "Time of input ingest sweep (TZ flex)", out);
    print_ymds_time(prefix, pc.ingest_file_tm, "Time of input ingest file (TZ flexible)", out);
    print_s(pc.config_file, prefix, "config_file", "Name of the product configuration file", out);
    print_s(pc.task_name, prefix, "task_name", "Name of the task used to generate the data", out);
    print_x(pc.flag, prefix, "flag", "Flag word: (Bits 0,2,3,4,8,9,10 used internally). Bit1: TDWR style messages. Bit5: Keep this file. Bit6: This is a clutter map. Bit7: Speak warning messages. Bit11: This product has been composited. Bit12: This product has been dwelled. Bit13: Z/R source0, 0:Type­in; 1:Setup; 2:Disdrometer. Bit14: Z/R source1", out);
    print_i(pc.x_scale, prefix, "x_scale", "X scale in cm/pixel", out);
    print_i(pc.y_scale, prefix, "y_scale", "Y scale in cm/pixel", out);
    print_i(pc.z_scale, prefix, "z_scale", "Z scale in cm/pixel", out);
    print_i(pc.x_size, prefix, "x_size", "X direction size of data array", out);
    print_i(pc.y_size, prefix, "y_size", "Y direction size of data array", out);
    print_i(pc.z_size, prefix, "z_size", "Z direction size of data array", out);
    print_i(pc.x_loc, prefix, "x_loc", "X location of radar in data array (signed 1/1000 of pixels)", out);
    print_i(pc.y_loc, prefix, "y_loc", "Y location of radar in data array (signed 1/1000 of pixels)", out);
    print_i(pc.z_loc, prefix, "z_loc", "Z location of radar in data array (signed 1/1000 of pixels)", out);
    print_i(pc.max_rng, prefix, "max_rng", "Maximum range in cm (used only in version 2.0, raw products)", out);
    print_u(pc.data_type, prefix, "data_type", "Data type generated (See Section 3.8 for values)", out);
    print_s(pc.proj, prefix, "proj", "Name of projection used", out);
    print_u(pc.inp_data_type, prefix, "inp_data_type", "Data type used as input (See Section 3.8 for values)", out);
    print_u(pc.proj_type, prefix, "proj_type", "Projection type: 0=Centered Azimuthal, 1=Mercator", out);
    print_i(pc.rad_smoother, prefix, "rad_smoother", "Radial smoother in 1/100 of km", out);
    print_i(pc.run_cnt, prefix, "run_cnt", "Number of times this product configuration has run", out);
    print_i(pc.zr_const, prefix, "zr_const", "Z/R relationship constant in 1/1000", out);
    print_i(pc.zr_exp, prefix, "zr_exp", "Z/R relationship exponent in 1/1000", out);
    print_i(pc.x_smooth, prefix, "x_smooth", "X-direction smoother in 1/100 of km", out);
    print_i(pc.y_smooth, prefix, "y_smooth", "Y-direction smoother in 1/100 of km", out);
    print_product_specific_info(prefix, pc.psi, out);
    print_s(pc.suffixes, prefix, "suffixes", "List of minor task suffixes, null terminated", out);
    print_color_scale_def(prefix, pc.csd, out);
}

/* get and/or print product_specific_info */

struct product_specific_info get_product_specific_info(char *rec)
{
    struct product_specific_info psi;

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

void print_product_specific_info(char *pfx, struct product_specific_info psi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_specific_info>.");
    print_u(psi.data_type_mask, prefix, "data_type_mask", "Data type mask word 0", out);
    print_i(psi.rng_last_bin, prefix, "rng_last_bin", "Range of last bin in cm", out);
    print_u(psi.format_conv_flag, prefix, "format_conv_flag", "Format conversion flag: 0=Preserve all ingest data 1=Convert 8-bit data to 16-bit data 2=Convert 16-bit data to 8-bit data", out);
    print_u(psi.flag, prefix, "flag", "Flag word: Bit 0=Separate product files by sweep Bit 1=Mask data by supplied mask", out);
    print_i(psi.sweep_num, prefix, "sweep_num", "Sweep number if separate files, origin 1", out);
    print_u(psi.xhdr_type, prefix, "xhdr_type", "Xhdr type (unused)", out);
    print_u(psi.data_type_mask1, prefix, "data_type_mask1", "Data type mask 1", out);
    print_u(psi.data_type_mask2, prefix, "data_type_mask2", "Data type mask 2", out);
    print_u(psi.data_type_mask3, prefix, "data_type_mask3", "Data type mask 3", out);
    print_u(psi.data_type_mask4, prefix, "data_type_mask4", "Data type mask 4", out);
    print_u(psi.playback_vsn, prefix, "playback_vsn", "Playback version (low 16-bits)", out);
}

/* get and/or print color_scale_def */

struct color_scale_def get_color_scale_def(char *rec)
{
    struct color_scale_def csd;
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

void print_color_scale_def(char *pfx, struct color_scale_def csd, FILE *out)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<color_scale_def>.");
    print_u(csd.flags, prefix, "flags", "iflags: Bit 8=COLOR_SCALE_VARIABLE Bit 10=COLOR_SCALE_TOP_SAT Bit 11=COLOR_SCALE_BOT_SAT", out);
    print_i(csd.istart, prefix, "istart", "istart: Starting level", out);
    print_i(csd.istep, prefix, "istep", "istep: Level step", out);
    print_i(csd.icolcnt, prefix, "icolcnt", "icolcnt: Number of colors in scale", out);
    print_u(csd.iset_and_scale, prefix, "iset_and_scale", "iset_and_scale: Color set number in low byte, color scale number in high byte.", out);
    for (n = 0; n < 16; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix, "ilevel_seams[", n, "]");
	fprintf(out, "%u ! %s ! %s\n", csd.ilevel_seams[n], struct_path, "ilevel_seams: Variable level starting values");
    }
}

/* get and/or print product_end */

struct product_end get_product_end(char *rec)
{
    struct product_end pe;

    strncpy(pe.site_name_prod, rec + 0, 16);
    trimRight(pe.site_name_prod, 16);
    strncpy(pe.iris_prod_vsn, rec + 16, 8);
    trimRight(pe.iris_prod_vsn, 8);
    strncpy(pe.iris_ing_vsn, rec + 24, 8);
    trimRight(pe.iris_ing_vsn, 8);
    pe.local_wgmt = get_sint16(rec + 72);
    strncpy(pe.hw_name, rec + 74, 16);
    trimRight(pe.hw_name, 16);
    strncpy(pe.site_name_ing, rec + 90, 16);
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
    pe.n_samples = get_sint16(rec + 132);
    strncpy(pe.clutter_filter, rec + 134, 12);
    trimRight(pe.clutter_filter, 12);
    pe.lin_filter = get_uint16(rec + 146);
    pe.wave_len = get_sint32(rec + 148);
    pe.trunc_ht = get_sint32(rec + 152);
    pe.rng_bin0 = get_sint32(rec + 156);
    pe.rng_last_bin = get_sint32(rec + 160);
    pe.n_out_bins = get_sint32(rec + 164);
    pe.flag = get_uint16(rec + 168);
    pe.polarization = get_uint16(rec + 172);
    pe.io_cal_hpol = get_sint16(rec + 174);
    pe.noise_cal_hpol = get_sint16(rec + 176);
    pe.radar_const = get_sint16(rec + 178);
    pe.recv_bandw = get_uint16(rec + 180);
    pe.noise_hpol = get_sint16(rec + 182);
    pe.noise_vpol = get_sint16(rec + 184);
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
    pe.n_logfilter = get_uint16(rec + 236);
    pe.cluttermap_used = get_uint16(rec + 238);
    pe.proj_lat = get_uint32(rec + 240);
    pe.proj_lon = get_uint32(rec + 244);
    pe.i_prod = get_sint16(rec + 248);
    pe.melt_level = get_sint16(rec + 282);
    pe.radar_ht_ref = get_sint16(rec + 284);
    pe.n_elem = get_sint16(rec + 286);
    pe.wind_spd = *(unsigned char *)(rec + 288);
    pe.wind_dir = *(unsigned char *)(rec + 289);
    strncpy(pe.tz, rec + 292, 8);
    trimRight(pe.tz, 8);
    return pe;
}

void print_product_end(char *pfx, struct product_end pe, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<product_end>.");
    print_s(pe.site_name_prod, prefix, "site_name_prod", "Site name -- where product was made (space padded)", out);
    print_s(pe.iris_prod_vsn, prefix, "iris_prod_vsn", "IRIS version where product was made (null terminated)", out);
    print_s(pe.iris_ing_vsn, prefix, "iris_ing_vsn", "IRIS version where ingest data came from", out);
    print_i(pe.local_wgmt, prefix, "local_wgmt", "Number of minutes local standard time is west of GMT", out);
    print_s(pe.hw_name, prefix, "hw_name", "Hardware name where ingest data came from (space padded)", out);
    print_s(pe.site_name_ing, prefix, "site_name_ing", "Site name where ingest data came from (space padded)", out);
    print_i(pe.rec_wgmt, prefix, "rec_wgmt", "Number of minutes recorded standard time is west of GMT", out);
    print_u(pe.center_latitude, prefix, "center_latitude", "Latitude of center (binary angle) *", out);
    print_u(pe.center_longitude, prefix, "center_longitude", "Longitude of center (binary angle) *", out);
    print_i(pe.ground_elev, prefix, "ground_elev", "Signed ground height in meters relative to sea level", out);
    print_i(pe.radar_ht, prefix, "radar_ht", "Height of radar above the ground in meters", out);
    print_i(pe.prf, prefix, "prf", "PRF in hertz", out);
    print_i(pe.pulse_w, prefix, "pulse_w", "Pulse width in 1/100 of microseconds", out);
    print_u(pe.proc_type, prefix, "proc_type", "Type of signal processor used", out);
    print_u(pe.trigger_rate_scheme, prefix, "trigger_rate_scheme", "Trigger rate scheme", out);
    print_i(pe.n_samples, prefix, "n_samples", "Number of samples used", out);
    print_s(pe.clutter_filter, prefix, "clutter_filter", "Clutter filter file name", out);
    print_u(pe.lin_filter, prefix, "lin_filter", "Number of linear based filter for the first bin", out);
    print_i(pe.wave_len, prefix, "wave_len", "Wavelength in 1/100 of centimeters", out);
    print_i(pe.trunc_ht, prefix, "trunc_ht", "Truncation height (cm above the radar)", out);
    print_i(pe.rng_bin0, prefix, "rng_bin0", "Range of the first bin in cm", out);
    print_i(pe.rng_last_bin, prefix, "rng_last_bin", "Range of the last bin in cm", out);
    print_i(pe.n_out_bins, prefix, "n_out_bins", "Number of output bins", out);
    print_u(pe.flag, prefix, "flag", "Flag word Bit0:Disdrometer failed, we used setup for Z/R source instead", out);
    print_u(pe.polarization, prefix, "polarization", "Type of polarization used", out);
    print_i(pe.io_cal_hpol, prefix, "io_cal_hpol", "I0 cal value, horizontal pol, in 1/100 dBm", out);
    print_i(pe.noise_cal_hpol, prefix, "noise_cal_hpol", "Noise at calibration, horizontal pol, in 1/100 dBm", out);
    print_i(pe.radar_const, prefix, "radar_const", "Radar constant, horizontal pol, in 1/100 dB", out);
    print_u(pe.recv_bandw, prefix, "recv_bandw", "Receiver bandwidth in kHz", out);
    print_i(pe.noise_hpol, prefix, "noise_hpol", "Current noise level, horizontal pol, in 1/100 dBm", out);
    print_i(pe.noise_vpol, prefix, "noise_vpol", "Current noise level, vertical pol, in 1/100 dBm", out);
    print_i(pe.ldr_offset, prefix, "ldr_offset", "LDR offset, in 1/100 dB", out);
    print_i(pe.zdr_offset, prefix, "zdr_offset", "ZDR offset, in 1/100 dB", out);
    print_u(pe.tcf_cal_flags, prefix, "tcf_cal_flags", "TCF Cal flags, see struct task_calib_info (added in 8.12.3)", out);
    print_u(pe.tcf_cal_flags2, prefix, "tcf_cal_flags2", "TCF Cal flags2, see struct task_calib_info (added in 8.12.3)", out);
    print_u(pe.std_parallel1, prefix, "std_parallel1", "More projection info these 4 words: Standard parallel #1", out);
    print_u(pe.std_parallel2, prefix, "std_parallel2", "Standard parallel #2", out);
    print_u(pe.rearth, prefix, "rearth", "Equatorial radius of the earth, cm (zero = 6371km sphere)", out);
    print_u(pe.flatten, prefix, "flatten", "1/Flattening in 1/1000000 (zero = sphere)", out);
    print_u(pe.fault, prefix, "fault", "Fault status of task, see ingest_configuration 3.2.14 for details", out);
    print_u(pe.insites_mask, prefix, "insites_mask", "Mask of input sites used in a composite", out);
    print_u(pe.n_logfilter, prefix, "n_logfilter", "Number of log based filter for the first bin", out);
    print_u(pe.cluttermap_used, prefix, "cluttermap_used", "Nonzero if cluttermap applied to the ingest data", out);
    print_u(pe.proj_lat, prefix, "proj_lat", "Latitude of projection reference *", out);
    print_u(pe.proj_lon, prefix, "proj_lon", "Longitude of projection reference *", out);
    print_i(pe.i_prod, prefix, "i_prod", "Product sequence number", out);
    print_i(pe.melt_level, prefix, "melt_level", "Melting level in meters, msb complemented (0=unknown)", out);
    print_i(pe.radar_ht_ref, prefix, "radar_ht_ref", "Height of radar above reference height in meters", out);
    print_i(pe.n_elem, prefix, "n_elem", "Number of elements in product results array", out);
    print_u(pe.wind_spd, prefix, "wind_spd", "Mean wind speed", out);
    print_u(pe.wind_dir, prefix, "wind_dir", "Mean wind direction (unknown if speed and direction 0)", out);
    print_s(pe.tz, prefix, "tz", "TZ Name of recorded data", out);
}

/* get and/or print ingest header (a.k.a. raw volume record 2) */

struct ingest_header get_ingest_header(char *rec)
{
    struct ingest_header ih;

    ih.sh = get_structure_header(rec);
    rec += 12;
    ih.ic = get_ingest_configuration(rec);
    rec += 480;
    ih.tc = get_task_configuration(rec);
    return ih;
}

void print_ingest_header(char *prefix, struct ingest_header ih, FILE *out)
{
    print_structure_header(prefix, ih.sh, out);
    print_ingest_configuration(prefix, ih.ic, out);
    print_task_configuration(prefix, ih.tc, out);
}

/* get and/or print ingest_configuration */

struct ingest_configuration get_ingest_configuration(char *rec)
{
    struct ingest_configuration ic;
    char *p, *p1;
    int *q;

    strncpy(ic.file_name, rec + 0, 80);
    trimRight(ic.file_name, 80);
    ic.num_assoc_files = get_sint16(rec + 80);
    ic.n_sweeps = get_sint16(rec + 82);
    ic.size_files = get_sint32(rec + 84);
    ic.vol_start_time = get_ymds_time(rec + 88);
    ic.ray_headers_sz = get_sint16(rec + 112);
    ic.extended_ray_headers_sz = get_sint16(rec + 114);
    ic.task_config_table_num = get_sint16(rec + 116);
    ic.playback_vsn = get_sint16(rec + 118);
    strncpy(ic.IRIS_vsn, rec + 124, 8);
    trimRight(ic.IRIS_vsn, 8);
    strncpy(ic.hw_site_name, rec + 132, 16);
    trimRight(ic.hw_site_name, 16);
    ic.local_wgmt = get_sint16(rec + 148);
    strncpy(ic.su_site_name, rec + 150, 16);
    trimRight(ic.su_site_name, 16);
    ic.rec_wgmt = get_sint16(rec + 166);
    ic.latitude = get_uint32(rec + 168);
    ic.longitude = get_uint32(rec + 172);
    ic.ground_elev = get_sint16(rec + 176);
    ic.radar_ht = get_sint16(rec + 178);
    ic.resolution = get_uint16(rec + 180);
    ic.index_first_ray = get_uint16(rec + 182);
    ic.rays_in_sweep = get_uint16(rec + 184);
    ic.nbytes_gparam = get_sint16(rec + 186);
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
    ic.meltz = get_sint16(rec + 220);
    strncpy(ic.tz, rec + 224, 8);
    trimRight(ic.tz, 8);
    ic.flags = get_uint32(rec + 232);
    strncpy(ic.config_name, rec + 236, 16);
    trimRight(ic.config_name, 16);
    return ic;
}

void print_ingest_configuration(char *pfx, struct ingest_configuration ic, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<ingest_configuration>.");
    print_s(ic.file_name, prefix, "file_name", "Name of file on disk", out);
    print_i(ic.num_assoc_files, prefix, "num_assoc_files", "Number of associated data files extant", out);
    print_i(ic.n_sweeps, prefix, "n_sweeps", "Number of sweeps completed so far", out);
    print_i(ic.size_files, prefix, "size_files", "Total size of all files in bytes", out);
    print_ymds_time(prefix, ic.vol_start_time, "Time that volume scan was started, TZ spec in bytes 166 & 224", out);
    print_i(ic.ray_headers_sz, prefix, "ray_headers_sz", "Number of bytes in the ray headers", out);
    print_i(ic.extended_ray_headers_sz, prefix, "extended_ray_headers_sz", "Number of bytes in extended ray headers (includes normal ray header)", out);
    print_i(ic.task_config_table_num, prefix, "task_config_table_num", "Number of task configuration table", out);
    print_i(ic.playback_vsn, prefix, "playback_vsn", "Playback version number", out);
    print_s(ic.IRIS_vsn, prefix, "IRIS_vsn", "IRIS version, null terminated", out);
    print_s(ic.hw_site_name, prefix, "hw_site_name", "Hardware name of site", out);
    print_i(ic.local_wgmt, prefix, "local_wgmt", "Time zone of local standard time, minutes west of GMT", out);
    print_s(ic.su_site_name, prefix, "su_site_name", "Name of site, from setup utility", out);
    print_i(ic.rec_wgmt, prefix, "rec_wgmt", "Time zone of recorded standard time, minutes west of GMT", out);
    print_u(ic.latitude, prefix, "latitude", "Latitude of radar (binary angle: 20000000 hex is 45_ North)", out);
    print_u(ic.longitude, prefix, "longitude", "Longitude of radar (binary angle: 20000000 hex is 45_ East)", out);
    print_i(ic.ground_elev, prefix, "ground_elev", "Height of ground at site (meters above sea level)", out);
    print_i(ic.radar_ht, prefix, "radar_ht", "Height of radar above ground (meters)", out);
    print_u(ic.resolution, prefix, "resolution", "Resolution specified in number of rays in a 360_ sweep", out);
    print_u(ic.index_first_ray, prefix, "index_first_ray", "Index of first ray from above set of rays", out);
    print_u(ic.rays_in_sweep, prefix, "rays_in_sweep", "Number of rays in a sweep", out);
    print_i(ic.nbytes_gparam, prefix, "nbytes_gparam", "Number of bytes in each gparam", out);
    print_i(ic.altitude, prefix, "altitude", "Altitude of radar (cm above sea level)", out);
    print_i(ic.velocity[0], prefix, "velocity east", "Velocity of radar platform (cm/sec) east", out);
    print_i(ic.velocity[1], prefix, "velocity north", "Velocity of radar platform (cm/sec) north", out);
    print_i(ic.velocity[2], prefix, "velocity up", "Velocity of radar platform (cm/sec) up", out);
    print_i(ic.offset_inu[0], prefix, "offset_inu starboard", "Antenna offset from INU (cm) starboard", out);
    print_i(ic.offset_inu[1], prefix, "offset_inu bow", "Antenna offset from INU (cm) bow", out);
    print_i(ic.offset_inu[2], prefix, "offset_inu up", "Antenna offset from INU (cm) up", out);
    print_u(ic.fault, prefix, "fault", "Fault status at the time the task was started, bits: 0:Normal BITE 1:Critical BITE 2:Normal RCP 3:Critical RCP 4:Critical system 5:Product gen. 6:Output 7:Normal system ", out);
    print_i(ic.meltz, prefix, "meltz", "Height of melting layer (meters above sea level) MSB is complemented, zero=Unknown", out);
    print_s(ic.tz, prefix, "tz", "Local timezone string, null terminated", out);
    print_u(ic.flags, prefix, "flags", "Flags, Bit 0=First ray not centered on zero degrees", out);
    print_s(ic.config_name, prefix, "config_name", "Configuration name in the dpolapp.conf file, null terminated", out);
}

/* get and/or print task_configuration */

struct task_configuration get_task_configuration(char *rec)
{
    struct task_configuration tc;

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

void print_task_configuration(char *prefix, struct task_configuration tc, FILE *out)
{
    print_structure_header(prefix, tc.sh, out);
    print_task_sched_info(prefix, tc.tsi, out);
    print_task_dsp_info(prefix, tc.tdi, out);
    print_task_calib_info(prefix, tc.tci, out);
    print_task_range_info(prefix, tc.tri, out);
    print_task_scan_info(prefix, tc.tni, out);
    print_task_misc_info(prefix, tc.tmi, out);
    print_task_end_info(prefix, tc.tei, out);
}

/* get and/or print task_sched_info */

struct task_sched_info get_task_sched_info(char *rec)
{
    struct task_sched_info tsi;

    tsi.start_time = get_sint32(rec + 0);
    tsi.stop_time = get_sint32(rec + 4);
    tsi.skip = get_sint32(rec + 8);
    tsi.time_last_run = get_sint32(rec + 12);
    tsi.time_used_last_run = get_sint32(rec + 16);
    tsi.rel_day_last_run = get_sint32(rec + 20);
    tsi.flag = get_uint16(rec + 24);
    return tsi;
}

void print_task_sched_info(char *pfx, struct task_sched_info tsi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_sched_info>.");
    print_i(tsi.start_time, prefix, "start_time", "Start time (seconds within a day)", out);
    print_i(tsi.stop_time, prefix, "stop_time", "Stop time (seconds within a day)", out);
    print_i(tsi.skip, prefix, "skip", "Desired skip time (seconds)", out);
    print_i(tsi.time_last_run, prefix, "time_last_run", "Time last run (seconds within a day)(0 for passive ingest)", out);
    print_i(tsi.time_used_last_run, prefix, "time_used_last_run", "Time used on last run (seconds) (in file time to writeout)", out);
    print_i(tsi.rel_day_last_run, prefix, "rel_day_last_run", "Relative day of last run (zero for passive ingest)", out);
    print_u(tsi.flag, prefix, "flag", "Flag: Bit 0 = ASAP Bit 1 = Mandatory Bit 2 = Late skip Bit 3 = Time used has been measured Bit 4 = Stop after running", out);
}

/* get and/or print task_dsp_mode_batch */

struct task_dsp_mode_batch get_task_dsp_mode_batch(char *rec)
{
    struct task_dsp_mode_batch tdmb;

    tdmb.lo_prf = get_uint16(rec + 0);
    tdmb.lo_prf_frac = get_uint16(rec + 2);
    tdmb.lo_prf_sampl = get_sint16(rec + 4);
    tdmb.lo_prf_avg = get_sint16(rec + 6);
    tdmb.dz_unfold_thresh = get_sint16(rec + 8);
    tdmb.vr_unfold_thresh = get_sint16(rec + 10);
    tdmb.sw_unfold_thresh = get_sint16(rec + 12);
    return tdmb;
}

void print_task_dsp_mode_batch(char *pfx, struct task_dsp_mode_batch tdmb, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_dsp_mode_batch>.");
    print_u(tdmb.lo_prf, prefix, "lo_prf", "Low PRF in Hz", out);
    print_u(tdmb.lo_prf_frac, prefix, "lo_prf_frac", "Low PRF fraction part, scaled by 2**­16", out);
    print_i(tdmb.lo_prf_sampl, prefix, "lo_prf_sampl", "Low PRF sample size", out);
    print_i(tdmb.lo_prf_avg, prefix, "lo_prf_avg", "Low PRF range averaging in bins", out);
    print_i(tdmb.dz_unfold_thresh, prefix, "dz_unfold_thresh", "Theshold for reflectivity unfolding in 1/100 of dB", out);
    print_i(tdmb.vr_unfold_thresh, prefix, "vr_unfold_thresh", "Threshold for velocity unfolding in 1/100 of dB", out);
    print_i(tdmb.sw_unfold_thresh, prefix, "sw_unfold_thresh", "Threshold for width unfolding in 1/100 of dB", out);
}

/* get and/or print task_dsp_info */

struct task_dsp_info get_task_dsp_info(char *rec)
{
    struct task_dsp_info tdi;

    tdi.major_mode = get_uint16(rec + 0);
    tdi.dsp_type = get_uint16(rec + 2);
    tdi.curr_data_mask = get_dsp_data_mask(rec + 4);
    tdi.orig_data_mask = get_dsp_data_mask(rec + 28);
    tdi.mb = get_task_dsp_mode_batch(rec + 52);
    tdi.prf = get_sint32(rec + 136);
    tdi.pulse_w = get_sint32(rec + 140);
    tdi.m_prf_mode = get_uint16(rec + 144);
    tdi.dual_prf = get_sint16(rec + 146);
    tdi.agc_feebk = get_uint16(rec + 148);
    tdi.sampl_sz = get_sint16(rec + 150);
    tdi.gain_flag = get_uint16(rec + 152);
    strncpy(tdi.clutter_file, rec + 154, 12);
    trimRight(tdi.clutter_file, 12);
    tdi.lin_filter_num = *(unsigned char *)(rec + 166);
    tdi.log_filter_num = *(unsigned char *)(rec + 167);
    tdi.attenuation = get_sint16(rec + 168);
    tdi.gas_attenuation = get_uint16(rec + 170);
    tdi.clutter_flag = get_uint16(rec + 172);
    tdi.xmt_phase = get_uint16(rec + 174);
    tdi.ray_hdr_mask = get_uint32(rec + 176);
    tdi.time_series_flag = get_uint16(rec + 180);
    strncpy(tdi.custom_ray_hdr, rec + 184, 16);
    trimRight(tdi.custom_ray_hdr, 16);
    return tdi;
}

void print_task_dsp_info(char *pfx, struct task_dsp_info tdi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_dsp_info>.");
    print_u(tdi.major_mode, prefix, "major_mode", "Major mode", out);
    print_u(tdi.dsp_type, prefix, "dsp_type", "DSP type", out);
    print_dsp_data_mask(prefix, tdi.curr_data_mask, "Current Data type mask", out);
    print_dsp_data_mask(prefix, tdi.orig_data_mask, "Original Data type mask", out);
    print_task_dsp_mode_batch(prefix, tdi.mb, out);
    print_i(tdi.prf, prefix, "prf", "PRF in Hertz", out);
    print_i(tdi.pulse_w, prefix, "pulse_w", "Pulse width in 1/100 of microseconds", out);
    print_i(tdi.m_prf_mode, prefix, "m_prf_mode", "Multi PRF mode flag: 0=1:1, 1=2:3, 2=3:4, 3=4:5", out);
    print_i(tdi.dual_prf, prefix, "dual_prf", "Dual PRF delay", out);
    print_u(tdi.agc_feebk, prefix, "agc_feebk", "AGC feedback code", out);
    print_i(tdi.sampl_sz, prefix, "sampl_sz", "Sample size", out);
    print_u(tdi.gain_flag, prefix, "gain_flag", "Gain Control flag (0=fixed, 1=STC, 2=AGC)", out);
    print_s(tdi.clutter_file, prefix, "clutter_file", "Name of file used for clutter filter", out);
    print_u(tdi.lin_filter_num, prefix, "lin_filter_num", "Linear based filter number for first bin", out);
    print_u(tdi.log_filter_num, prefix, "log_filter_num", "Log based filter number for first bin", out);
    print_i(tdi.attenuation, prefix, "attenuation", "Attenuation in 1/10 dB applied in fixed gain mode", out);
    print_u(tdi.gas_attenuation, prefix, "gas_attenuation", "Gas attenuation in 1/100000 dB/km for first 10000, then stepping in 1/10000 dB/km", out);
    print_u(tdi.clutter_flag, prefix, "clutter_flag", "Flag nonzero means cluttermap used", out);
    print_u(tdi.xmt_phase, prefix, "xmt_phase", "XMT phase sequence: 0:Fixed, 1:Random, 3:SZ8/64", out);
    print_u(tdi.ray_hdr_mask, prefix, "ray_hdr_mask", "Mask used for to configure the ray header.", out);
    print_u(tdi.time_series_flag, prefix, "time_series_flag", "Time series playback flags, see OPTS_* in dsp.h", out);
    print_s(tdi.custom_ray_hdr, prefix, "custom_ray_hdr", "Name of custom ray header", out);
}

/* get and/or print task_calib_info */

struct task_calib_info get_task_calib_info(char *rec)
{
    struct task_calib_info tci;

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
    tci.h_io_cal = get_sint16(rec + 48);
    tci.v_io_cal = get_sint16(rec + 50);
    tci.h_noise = get_sint16(rec + 52);
    tci.v_noise = get_sint16(rec + 54);
    tci.h_radar_const = get_sint16(rec + 56);
    tci.v_radar_const = get_sint16(rec + 58);
    tci.bandwidth = get_uint16(rec + 60);
    tci.flags2 = get_uint16(rec + 62);
    return tci;
}

void print_task_calib_info(char *pfx, struct task_calib_info tci, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_calib_info>.");
    print_i(tci.dbz_slope, prefix, "dbz_slope", "Reflectivity slope (4096*dB/ A/D count)", out);
    print_i(tci.dbz_noise_thresh, prefix, "dbz_noise_thresh", "Reflectivity noise threshold (1/16 dB above Noise)", out);
    print_i(tci.clutter_corr_thesh, prefix, "clutter_corr_thesh", "Clutter Correction threshold (1/16 dB)", out);
    print_i(tci.sqi_thresh, prefix, "sqi_thresh", "SQI threshold (0­1)*256", out);
    print_i(tci.pwr_thresh, prefix, "pwr_thresh", "Power threshold (1/16 dBZ)", out);
    print_i(tci.cal_dbz, prefix, "cal_dbz", "Calibration Reflectivity (1/16 dBZ at 1 km)", out);
    print_u(tci.dbt_flags, prefix, "dbt_flags", "Threshold flags for uncorrected reflectivity", out);
    print_u(tci.dbz_flags, prefix, "dbz_flags", "Threshold flags for corrected reflectivity", out);
    print_u(tci.vel_flags, prefix, "vel_flags", "Threshold flags for velocity", out);
    print_u(tci.sw_flags, prefix, "sw_flags", "Threshold flags for width", out);
    print_u(tci.zdr_flags, prefix, "zdr_flags", "Threshold flags for ZDR", out);
    print_u(tci.flags, prefix, "flags", "Flags: Bit 0: Speckle remover for log channel Bit 3: Speckle remover for linear channel Bit 4: Flag to indicate data is range normalized Bit 5: Flag to indicate pulse at beginning of ray Bit 6: Flag to indicate pulse at end of ray Bit 7: Vary number of pulses in dual PRF Bit 8: Use 3 lag processing in PP02 Bit 9: Apply velocity correction for ship motion Bit 10: Vc is unfolded Bit 11: Vc has fallspeed correction Bit 12: Zc has beam blockage correction Bit 13: Zc has Z-based attenuation correction Bit 14: Zc has target detection Bit 15: Vc has storm relative velocity correction", out);
    print_i(tci.ldr_bias, prefix, "ldr_bias", "LDR bias in signed 1/100 dB", out);
    print_i(tci.zdr_bias, prefix, "zdr_bias", "ZDR bias in signed 1/16 dB", out);
    print_i(tci.nx_clutter_thresh, prefix, "nx_clutter_thresh", "NEXRAD point clutter threshold in 1/100 of dB", out);
    print_u(tci.nx_clutter_skip, prefix, "nx_clutter_skip", "NEXRAD point clutter bin skip in low 4 bits", out);
    print_i(tci.h_io_cal, prefix, "h_io_cal", "I0 cal value, horizontal pol, in 1/100 dBm", out);
    print_i(tci.v_io_cal, prefix, "v_io_cal", "I0 cal value, vertical pol, in 1/100 dBm", out);
    print_i(tci.h_noise, prefix, "h_noise", "Noise at calibration, horizontal pol, in 1/100 dBm", out);
    print_i(tci.v_noise, prefix, "v_noise", "Noise at calibration, vertical pol, in 1/100 dBm", out);
    print_i(tci.h_radar_const, prefix, "h_radar_const", "Radar constant, horizontal pol, in 1/100 dB", out);
    print_i(tci.v_radar_const, prefix, "v_radar_const", "Radar constant, vertical pol, in 1/100 dB", out);
    print_u(tci.bandwidth, prefix, "bandwidth", "Receiver bandwidth in kHz", out);
    print_u(tci.flags2, prefix, "flags2", "Flags2: Bit 0: Zc and ZDRc has DP attenuation correction Bit 1: Z and ZDR has DP attenuation correction", out);
}

/* get and/or print task_range_info */

struct task_range_info get_task_range_info(char *rec)
{
    struct task_range_info tri;

    tri.rng_1st_bin = get_sint32(rec + 0);
    tri.rng_last_bin = get_sint32(rec + 4);
    tri.nbins_in = get_sint16(rec + 8);
    tri.nbins_out = get_sint16(rec + 10);
    tri.step_in = get_sint32(rec + 12);
    tri.step_out = get_sint32(rec + 16);
    tri.flag = get_uint16(rec + 20);
    tri.rng_avg_flag = get_sint16(rec + 22);
    return tri;
}

void print_task_range_info(char *pfx, struct task_range_info tri, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_range_info>.");
    print_i(tri.rng_1st_bin, prefix, "rng_1st_bin", "Range of first bin in centimeters", out);
    print_i(tri.rng_last_bin, prefix, "rng_last_bin", "Range of last bin in centimeters", out);
    print_i(tri.nbins_in, prefix, "nbins_in", "Number of input bins", out);
    print_i(tri.nbins_out, prefix, "nbins_out", "Number of output range bins", out);
    print_i(tri.step_in, prefix, "step_in", "Step between input bins", out);
    print_i(tri.step_out, prefix, "step_out", "Step between output bins (in centimeters)", out);
    print_u(tri.flag, prefix, "flag", "Flag for variable range bin spacing (1=var, 0=fixed)", out);
    print_i(tri.rng_avg_flag, prefix, "rng_avg_flag", "Range bin averaging flag", out);
}

/* get and/or print task_scan_info */

struct task_scan_info get_task_scan_info(char *rec)
{
    struct task_scan_info tsi;

    tsi.scan_mode = get_uint16(rec + 0);
    tsi.resoln = get_sint16(rec + 2);
    tsi.n_sweeps = get_sint16(rec + 6);
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

void print_task_scan_info(char *pfx, struct task_scan_info tsi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_scan_info>.");
    print_u(tsi.scan_mode, prefix, "scan_mode", "Antenna scan mode 1:PPI sector, 2:RHI, 3:Manual, 4:PPI cont, 5:file", out);
    print_i(tsi.resoln, prefix, "resoln", "Desired angular resolution in 1/1000 of degrees", out);
    print_i(tsi.n_sweeps, prefix, "n_sweeps", "Number of sweeps to perform", out);
    switch (tsi.scan_mode) {
	case RHI:
	    print_task_rhi_scan_info(prefix, tsi.scan_info.rhi_info, out);
	    break;
	case PPI_S:
	case PPI_C:
	    print_task_ppi_scan_info(prefix, tsi.scan_info.ppi_info, out);
	    break;
	case FILE_SCAN:
	    print_task_file_scan_info(prefix, tsi.scan_info.file_info, out);
	    break;
	case MAN_SCAN:
	    print_task_manual_scan_info(prefix, tsi.scan_info.man_info, out);
	    break;
    }
}

/* get and/or print task_rhi_scan_info */

struct task_rhi_scan_info get_task_rhi_scan_info(char *rec)
{
    struct task_rhi_scan_info trsi;
    char *p, *p1;
    unsigned *q;

    trsi.lo_elev = get_uint16(rec + 0);
    trsi.hi_elev = get_uint16(rec + 2);
    q = trsi.az;
    p = rec + 4;
    p1 = p + sizeof(*trsi.az) * 40;
    for ( ; p < p1; p += sizeof(*trsi.az), q++) {
	*q = get_uint16(p);
    }
    trsi.start = *(unsigned char *)(rec + 199);
    return trsi;
}

void print_task_rhi_scan_info(char *pfx, struct task_rhi_scan_info trsi, FILE *out)
{
    int n;
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<rhi_scan_info>.");
    print_u(trsi.lo_elev, prefix, "lo_elev", "Lower elevation limit (binary angle, only for sector)", out);
    print_u(trsi.hi_elev, prefix, "hi_elev", "Upper elevation limit (binary angle, only for sector)", out);
    for (n = 0; n < 40; n++) {
	fprintf(out, "%u ! %s%s%d%s ! %s\n", trsi.az[n], prefix, "az[", n, "]", "List of azimuths (binary angles) to scan at");
    }
    print_u(trsi.start, prefix, "start", "Start of first sector sweep: 0=Nearest, 1=Lower, 2=Upper Sector sweeps alternate in direction.", out);
}

/* get and/or print task_ppi_scan_info */

struct task_ppi_scan_info get_task_ppi_scan_info(char *rec)
{
    struct task_ppi_scan_info tpsi;
    char *p, *p1;
    unsigned *q;

    tpsi.left_az = get_uint16(rec + 0);
    tpsi.right_az = get_uint16(rec + 2);
    q = tpsi.elevs;
    p = rec + 16;
    p1 = p + sizeof(*tpsi.elevs) * 40;
    for ( ; p < p1; p += sizeof(*tpsi.elevs), q++) {
	*q = get_uint16(p);
    }
    tpsi.start = *(unsigned char *)(rec + 199);
    return tpsi;
}

void print_task_ppi_scan_info(char *pfx, struct task_ppi_scan_info tpsi, FILE *out)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_ppi_scan_info>.");
    print_u(tpsi.left_az, prefix, "left_az", "Left azimuth limit (binary angle, only for sector)", out);
    print_u(tpsi.right_az, prefix, "right_az", "Right azimuth limit (binary angle, only for sector)", out);
    for (n = 0; n < 40; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix, "elevs[", n, "]");
	fprintf(out, "%u ! %s ! %s\n", tpsi.elevs[n], struct_path, "List of elevations (binary angles) to scan at");
    }
    print_u(tpsi.start, prefix, "start", "Start of first sector sweep: 0=Nearest, 1=Left, 2=Right Sector sweeps alternate in direction.", out);
}

/* get and/or print task_file_scan_info */

struct task_file_scan_info get_task_file_scan_info(char *rec)
{
    struct task_file_scan_info tfsi;

    tfsi.az0 = get_uint16(rec + 0);
    tfsi.elev0 = get_uint16(rec + 2);
    strncpy(tfsi.ant_ctrl, rec + 4, 12);
    trimRight(tfsi.ant_ctrl, 12);
    return tfsi;
}

void print_task_file_scan_info(char *pfx, struct task_file_scan_info tfsi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_file_scan_info>.");
    print_u(tfsi.az0, prefix, "az0", "First azimuth angle (binary angle)", out);
    print_u(tfsi.elev0, prefix, "elev0", "First elevation angle (binary angle)", out);
    print_s(tfsi.ant_ctrl, prefix, "ant_ctrl", "Filename for antenna control", out);
}

/* get and/or print task_manual_scan_info */

struct task_manual_scan_info get_task_manual_scan_info(char *rec)
{
    struct task_manual_scan_info tmsi;

    tmsi.flags = get_uint16(rec + 0);
    return tmsi;
}

void print_task_manual_scan_info(char *pfx, struct task_manual_scan_info tmsi, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_manual_scan_info>.");
    print_u(tmsi.flags, prefix, "flags", "Flags: bit 0=Continuous recording", out);
}

/* get and/or print task_misc_info */

struct task_misc_info get_task_misc_info(char *rec)
{
    struct task_misc_info tmi;
    char *p, *p1;
    unsigned *q;

    tmi.wave_len = get_sint32(rec + 0);
    strncpy(tmi.tr_ser, rec + 4, 16);
    trimRight(tmi.tr_ser, 16);
    tmi.power = get_sint32(rec + 20);
    tmi.flags = get_uint16(rec + 24);
    tmi.polarization = get_uint16(rec + 26);
    tmi.trunc_ht = get_sint32(rec + 28);
    tmi.comment_sz = get_sint16(rec + 62);
    tmi.h_beam_width = get_uint32(rec + 64);
    tmi.v_beam_width = get_uint32(rec + 68);
    q = tmi.custom;
    p = rec + 72;
    p1 = p + sizeof(*tmi.custom) * 10;
    for ( ; p < p1; p += sizeof(*tmi.custom), q++) {
	*q = get_uint32(p);
    }
    return tmi;
}

void print_task_misc_info(char *pfx, struct task_misc_info tmi, FILE *out)
{
    int n;
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_misc_info>.");
    print_i(tmi.wave_len, prefix, "wave_len", "Wavelength in 1/100 of cm", out);
    print_s(tmi.tr_ser, prefix, "tr_ser", "T/R Serial Number", out);
    print_i(tmi.power, prefix, "power", "Transmit Power in watts", out);
    print_u(tmi.flags, prefix, "flags", "Flags: Bit 0: Digital signal simulator in use Bit 1: Polarization in use Bit 4: Keep bit", out);
    print_u(tmi.polarization, prefix, "polarization", "Type of polarization", out);
    print_i(tmi.trunc_ht, prefix, "trunc_ht", "Truncation height (centimeters above the radar)", out);
    print_i(tmi.comment_sz, prefix, "comment_sz", "Number of bytes of comments entered", out);
    print_u(tmi.h_beam_width, prefix, "h_beam_width", "Horizontal beamwidth (binary angle, starting in 7.18)", out);
    for (n = 0; n < 10; n++) {
	snprintf(struct_path, STR_LEN, "%s%s%d%s", prefix, "custom[", n, "]");
	fprintf(out, "%u ! %s ! %s\n", tmi.custom[n], struct_path, "Customer defined storage (starting in 7.27)");
    }
}

struct task_end_info get_task_end_info(char *rec)
{
    struct task_end_info tei;

    tei.task_major = get_sint16(rec + 0);
    tei.task_minor = get_sint16(rec + 2);
    strncpy(tei.task_config, rec + 4, 12);
    trimRight(tei.task_config, 12);
    strncpy(tei.task_descr, rec + 16, 80);
    trimRight(tei.task_descr, 80);
    tei.hybrid_ntasks = get_sint32(rec + 96);
    tei.task_state = get_uint16(rec + 100);
    tei.data_time = get_ymds_time(rec + 104);
    return tei;
}

void print_task_end_info(char *pfx, struct task_end_info tei, FILE *out)
{
    char prefix[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<task_end_info>.");
    print_i(tei.task_major, prefix, "task_major", "Task major number", out);
    print_i(tei.task_minor, prefix, "task_minor", "Task minor number", out);
    print_s(tei.task_config, prefix, "task_config", "Name of task configuration file", out);
    print_s(tei.task_descr, prefix, "task_descr", "Task description", out);
    print_i(tei.hybrid_ntasks, prefix, "hybrid_ntasks", "Number of tasks in hybrid task", out);
    print_u(tei.task_state, prefix, "task_state", "Task state: 0=no task; 1=task being modified; 2=inactive; 3=scheduled, 4=running.", out);
    print_ymds_time(prefix, tei.data_time, "Data time of task (TZ flexible)", out);
}

/* get and/or print dsp_data_mask */

struct dsp_data_mask get_dsp_data_mask(char *rec)
{
    struct dsp_data_mask ddm;

    ddm.mask_word_0 = get_uint32(rec + 0);
    ddm.ext_hdr_type = get_uint32(rec + 4);
    ddm.mask_word_1 = get_uint32(rec + 8);
    ddm.mask_word_2 = get_uint32(rec + 12);
    ddm.mask_word_3 = get_uint32(rec + 16);
    ddm.mask_word_4 = get_uint32(rec + 20);
    return ddm;
}

void print_dsp_data_mask(char *pfx, struct dsp_data_mask ddm, char *suffix, FILE *out)
{
    char prefix[STR_LEN];
    char struct_path[STR_LEN];

    snprintf(prefix, STR_LEN, "%s%s", pfx, "<dsp_data_mask>.");
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_0");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.mask_word_0, struct_path, "Mask word 0", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "ext_hdr_type");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.ext_hdr_type, struct_path, "Extended header type", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_1");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.mask_word_1, struct_path, "Mask word 1 Contains bits set for all data recorded.", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_2");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.mask_word_2, struct_path, "Mask word 2 See parameter DB_* in Table 3­6 for", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_3");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.mask_word_3, struct_path, "Mask word 3 bit specification.", suffix);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "mask_word_4");
    fprintf(out, "%u ! %s ! %s.  %s\n", ddm.mask_word_4, struct_path, "Mask word 4", suffix);
}

/* get and/or print structure_header */

struct structure_header get_structure_header(char *rec)
{
    struct structure_header sh;

    sh.id = get_sint16(rec + 0);
    sh.format = get_sint16(rec + 2);
    sh.sz = get_sint32(rec + 4);
    sh.flags = get_sint16(rec + 10);
    return sh;
}

void print_structure_header(char *prefix, struct structure_header sh, FILE *out)
{
    print_i(sh.id, prefix, "<structure_header>.id", "Structure identifier: 22 => Task_configuration.  23 => Ingest_header.  24 => Ingest_data_header.  25 => Tape_inventory.  26 => Product_configuration.  27 => Product_hdr.  28 => Tape_header_record", out);
    print_i(sh.format, prefix, "<structure_header>.format", "Format version number (see headers.h)", out);
    print_i(sh.sz, prefix, "<structure_header>.sz", "Number of bytes in the entire structure", out);
    print_i(sh.flags, prefix, "<structure_header>.flags", "Flags: bit0=structure complete", out);
}

/* get and/or print ymds_time */

struct ymds_time get_ymds_time(char *b)
{
    unsigned short msec;
    struct ymds_time tm;

    tm.sec = get_sint32(b);
    msec = get_uint16(b + 4);
    tm.msec = (msec & 0x3ff);
    tm.utc = (msec & 0x800);
    tm.year = get_sint16(b + 6);
    tm.month = get_sint16(b + 8);
    tm.day = get_sint16(b + 10);
    return tm;
}

void print_ymds_time(char *prefix, struct ymds_time tm, char *suffix, FILE *out)
{
    double fhour, fmin;
    double ihour, imin;
    double sec;
    char struct_path[STR_LEN];

    sec = tm.sec + 0.001 * tm.msec;
    fhour = modf(sec / 3600.0, &ihour);
    fmin = modf(fhour * 60.0, &imin);
    snprintf(struct_path, STR_LEN, "%s%s", prefix, "<ymds_time>");
    fprintf(out, "%04d/%02d/%02d %02d:%02d:%05.2f. ! %s ! %s\n", tm.year, tm.month, tm.day,
	    (int)ihour, (int)imin, fmin * 60.0, struct_path, suffix);
}

/*
   Print an unsigned integer, the structure hierarchy and component where it is stored,
   and a descriptor for it to stream out
 */
void print_u(unsigned u, char *prefix, char *comp, char *desc, FILE *out)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, comp);
    fprintf(out, "%u ! %s ! %s\n", u, struct_path, desc);
}

/*
   Print an unsigned integer in hex format, the structure hierarchy and component where
   it is stored, and a descriptor for it to stream out
 */
void print_x(unsigned u, char *prefix, char *comp, char *desc, FILE *out)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, comp);
    fprintf(out, "%-40x ! %s ! %s\n", u, struct_path, desc);
}

/*
   Print an integer, the structure hierarchy and component where it is stored,
   and a descriptor for it to stream out
 */
void print_i(int u, char *prefix, char *comp, char *desc, FILE *out)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, comp);
    fprintf(out, "%d ! %s ! %s\n", u, struct_path, desc);
}

/*
   Print a string, the structure hierarchy and component where it is stored,
   and a descriptor for it to stream out
 */
void print_s(char *s, char *prefix, char *comp, char *desc, FILE *out)
{
    char struct_path[STR_LEN];

    snprintf(struct_path, STR_LEN, "%s%s", prefix, comp);
    fprintf(out, "%s ! %s ! %s\n", s, struct_path, desc);
}

/*
   Trim spaces off the end of a character array
   Returns the input string with a nul character at the start of any
   trailing spaces and at end of string.  The input string is modified.
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
