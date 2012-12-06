/*
   -	sigmet.h --
   -		This header file declares structures and functions
   -		that store and access Sigmet raw product files.
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
   .	$Revision: 1.128 $ $Date: 2012/12/05 23:28:03 $
   .
   .	Reference: IRIS Programmer's Manual, February 2009.
 */

#ifndef SIGMET_H_
#define SIGMET_H_

#define SIGMET_VERSION "1.1"

#include "unix_defs.h"
#include <float.h>
#include <stdio.h>
#include <sys/types.h>
#include "type_nbit.h"
#include "dorade_lib.h"
#include "geog_proj.h"

#ifndef M_PI
#define M_PI     3.141592653589793238462
#endif
#ifndef RAD_PER_DEG
#define RAD_PER_DEG	0.01745329251994329576
#endif
#ifndef DEG_PER_RAD	
#define DEG_PER_RAD	57.29577951308232087648
#endif

/*
   Enumerator for the data types defined in the IRIS Programmer's Manual
   (section 3.3).
 */

#define SIGMET_NTYPES 28
enum Sigmet_DataTypeN {
    DB_XHDR,	DB_DBT,		DB_DBZ,		DB_VEL,		DB_WIDTH,
    DB_ZDR,	DB_DBZC,	DB_DBT2,	DB_DBZ2,	DB_VEL2,
    DB_WIDTH2,	DB_ZDR2,	DB_RAINRATE2,	DB_KDP,		DB_KDP2,
    DB_PHIDP,	DB_VELC,	DB_SQI,		DB_RHOHV,	DB_RHOHV2,
    DB_DBZC2,	DB_VELC2,	DB_SQI2,	DB_PHIDP2,	DB_LDRH,
    DB_LDRH2,	DB_LDRV,	DB_LDRV2
};

/*
   This enumerator indicates a storage format.
       SIGMET_U1	1 byte unsigned integer
       SIGMET_U2	2 byte unsigned integer
       SIGMET_FLT	float
       SIGMET_DBL	double
       SIGMET_MT	empty. Unknown or pseudo data type
 */

enum Sigmet_StorFmt {
    SIGMET_U1, SIGMET_U2, SIGMET_FLT, SIGMET_DBL, SIGMET_MT
};

/*
   Multi PRF mode flags
 */

enum Sigmet_Multi_PRF {ONE_ONE, TWO_THREE, THREE_FOUR, FOUR_FIVE};

/*
   Volume scan modes.  Refer to task_scan_info struct in IRIS Programmer's
   Manual
 */

enum Sigmet_ScanMode {PPI_S = 1, RHI, MAN_SCAN, PPI_C, FILE_SCAN};

/*
   The following structures store data from volume headers.
   Ref. IRIS Programmer's Manual
 */

/*
   Time as represented in various Sigmet raw headers.
 */

struct Sigmet_YMDS_Time {
    int sec;				/* Seconds since midnight */
    unsigned msec;			/* Milliseconds */
    char utc;				/* If true, time is UTC */
    int year;
    int month;
    int day;
};

struct Sigmet_Structure_Header {
    int id;
    int format;
    int sz;
    int flags;
};

/*
   For raw volume, product_specific_info is raw_psi_struct
   See IRIS Programmer's Manual, 3.2.26.
 */

struct Sigmet_Product_Specific_Info {
    unsigned data_type_mask;
    int rng_last_bin;
    unsigned format_conv_flag;
    unsigned flag;
    int sweep_num;
    unsigned xhdr_type;
    unsigned data_type_mask1;
    unsigned data_type_mask2;
    unsigned data_type_mask3;
    unsigned data_type_mask4;
    unsigned playback_vsn;
};

struct Sigmet_Color_Scale_Def {
    unsigned flags;
    int istart;
    int istep;
    int icolcnt;
    unsigned iset_and_scale;
    unsigned ilevel_seams[16];
};

struct Sigmet_Product_Configuration {
    struct Sigmet_Structure_Header sh;
    unsigned type;
    unsigned schedule;
    int skip;
    struct Sigmet_YMDS_Time gen_tm;
    struct Sigmet_YMDS_Time ingest_sweep_tm;
    struct Sigmet_YMDS_Time ingest_file_tm;
    char config_file[13];
    char task_name[13];
    unsigned flag;
    int x_scale;
    int y_scale;
    int z_scale;
    int x_size;
    int y_size;
    int z_size;
    int x_loc;
    int y_loc;
    int z_loc;
    int max_rng;
    unsigned data_type;
    char proj[13];
    unsigned inp_data_type;
    unsigned proj_type;
    int rad_smoother;
    int num_runs;
    int zr_const;
    int zr_exp;
    int x_smooth;
    int y_smooth;
    struct Sigmet_Product_Specific_Info psi;
    char suffixes[16];
    struct Sigmet_Color_Scale_Def csd;
};

struct Sigmet_Product_End {
    char site_name_prod[17];
    char iris_prod_vsn[9];
    char iris_ing_vsn[9];
    int local_wgmt;
    char hw_name[17];
    char site_name_ing[17];
    int rec_wgmt;
    unsigned center_latitude;
    unsigned center_longitude;
    int ground_elev;
    int radar_ht;
    int prf;
    int pulse_w;
    unsigned proc_type;
    unsigned trigger_rate_scheme;
    int num_samples;
    char clutter_filter[13];
    unsigned lin_filter;
    int wave_len;
    int trunc_ht;
    int rng_bin0;
    int rng_last_bin;
    int num_bins_out;
    unsigned flag;
    unsigned polarization;
    int hpol_io_cal;
    int hpol_cal_noise;
    int hpol_radar_const;
    unsigned recv_bandw;
    int hpol_noise;
    int vpol_noise;
    int ldr_offset;
    int zdr_offset;
    unsigned tcf_cal_flags;
    unsigned tcf_cal_flags2;
    unsigned std_parallel1;
    unsigned std_parallel2;
    unsigned rearth;
    unsigned flatten;
    unsigned fault;
    unsigned insites_mask;
    unsigned logfilter_num;
    unsigned cluttermap_used;
    unsigned proj_lat;
    unsigned proj_lon;
    int i_prod;
    int melt_level;
    int radar_ht_ref;
    int num_elem;
    unsigned wind_spd;
    unsigned wind_dir;
    char tz[9];
};

struct Sigmet_Product_Hdr {
    struct Sigmet_Structure_Header sh;
    struct Sigmet_Product_Configuration pc;
    struct Sigmet_Product_End pe;
};

struct Sigmet_Ingest_Configuration {
    char file_name[81];
    int num_assoc_files;
    int num_sweeps;
    int size_files;
    struct Sigmet_YMDS_Time vol_start_time;
    int ray_headers_sz;
    int extended_ray_headers_sz;
    int task_config_table_num;
    int playback_vsn;
    char IRIS_vsn[9];
    char hw_site_name[17];
    int local_wgmt;
    char su_site_name[17];
    int rec_wgmt;
    unsigned latitude;
    unsigned longitude;
    int ground_elev;
    int radar_ht;
    unsigned resolution;
    unsigned index_first_ray;
    unsigned num_rays;
    int num_bytes_gparam;
    int altitude;
    int velocity[3];
    int offset_inu[3];
    unsigned fault;
    int melt_level;
    char tz[9];
    unsigned flags;
    char config_name[17];
};

struct Sigmet_Task_Sched_Info {
    int start_time;
    int stop_time;
    int skip;
    int time_last_run;
    int time_used_last_run;
    int rel_day_last_run;
    unsigned flag;
};

struct Sigmet_DSP_Data_Mask {
    unsigned mask_word_0;
    unsigned ext_hdr_type;
    unsigned mask_word_1;
    unsigned mask_word_2;
    unsigned mask_word_3;
    unsigned mask_word_4;
};

struct Sigmet_Task_DSP_Mode_Batch {
    unsigned lo_prf;
    unsigned lo_prf_frac;
    int lo_prf_sampl;
    int lo_prf_avg;
    int dz_unfold_thresh;
    int vr_unfold_thresh;
    int sw_unfold_thresh;
};

struct Sigmet_Task_DSP_Info {
    unsigned major_mode;
    unsigned dsp_type;
    struct Sigmet_DSP_Data_Mask curr_data_mask;
    struct Sigmet_DSP_Data_Mask orig_data_mask;
    struct Sigmet_Task_DSP_Mode_Batch mb;
    int prf;
    int pulse_w;
    enum Sigmet_Multi_PRF m_prf_mode;
    int dual_prf;
    unsigned agc_feebk;
    int sampl_sz;
    unsigned gain_flag;
    char clutter_file[13];
    unsigned lin_filter_num;
    unsigned log_filter_num;
    int attenuation;
    unsigned gas_attenuation;
    unsigned clutter_flag;
    unsigned xmt_phase;
    unsigned ray_hdr_mask;
    unsigned time_series_flag;
    char custom_ray_hdr[17];
};

struct Sigmet_Task_Calib_Info {
    int dbz_slope;
    int dbz_noise_thresh;
    int clutter_corr_thesh;
    int sqi_thresh;
    int pwr_thresh;
    int cal_dbz;
    unsigned dbt_flags;
    unsigned dbz_flags;
    unsigned vel_flags;
    unsigned sw_flags;
    unsigned zdr_flags;
    unsigned flags;
    int ldr_bias;
    int zdr_bias;
    int nx_clutter_thresh;
    unsigned nx_clutter_skip;
    int hpol_io_cal;
    int vpol_io_cal;
    int hpol_noise;
    int vpol_noise;
    int hpol_radar_const;
    int vpol_radar_const;
    unsigned bandwidth;
    unsigned flags2;
};

struct Sigmet_Task_Range_Info {
    int rng_1st_bin;
    int rng_last_bin;
    int num_bins_in;
    int num_bins_out;
    int step_in;
    int step_out;
    unsigned flag;
    int rng_avg_flag;
};

struct Sigmet_Task_RHI_Scan_Info {
    unsigned lo_elev;
    unsigned hi_elev;
    unsigned az[40];
    unsigned start;
};

struct Sigmet_Task_PPI_Scan_Info {
    unsigned left_az;
    unsigned right_az;
    unsigned elevs[40];
    unsigned start;
};

struct Sigmet_Task_File_Scan_Info {
    unsigned az0;
    unsigned elev0;
    char ant_ctrl[13];
};

struct Sigmet_Task_Manual_Scan_Info {
    unsigned flags;
};

struct Sigmet_Task_Scan_Info {
    enum Sigmet_ScanMode scan_mode;
    int resoln;
    int num_sweeps;
    union {
	struct Sigmet_Task_RHI_Scan_Info rhi_info;
	struct Sigmet_Task_PPI_Scan_Info ppi_info;
	struct Sigmet_Task_File_Scan_Info file_info;
	struct Sigmet_Task_Manual_Scan_Info man_info;
    } scan_info;
};

struct Sigmet_Task_Misc_Info {
    int wave_len;
    char tr_ser[17];
    int power;
    unsigned flags;
    unsigned polarization;
    int trunc_ht;
    int comment_sz;
    unsigned horiz_beam_width;
    unsigned vert_beam_width;
    unsigned custom[10];
};

struct Sigmet_Task_End_Info {
    int task_major;
    int task_minor;
    char task_config[13];
    char task_descr[81];
    int hybrid_ntasks;
    unsigned task_state;
    struct Sigmet_YMDS_Time data_time;
};

struct Sigmet_Task_Configuration {
    struct Sigmet_Structure_Header sh;
    struct Sigmet_Task_Sched_Info tsi;
    struct Sigmet_Task_DSP_Info tdi;
    struct Sigmet_Task_Calib_Info tci;
    struct Sigmet_Task_Range_Info tri;
    struct Sigmet_Task_Scan_Info tni;
    struct Sigmet_Task_Misc_Info tmi;
    struct Sigmet_Task_End_Info tei;
};

struct Sigmet_Ingest_Header {
    struct Sigmet_Structure_Header sh;
    struct Sigmet_Ingest_Configuration ic;
    struct Sigmet_Task_Configuration tc;
};

/*
   Functions of this type convert storage values to computational values
   (measurements).
 */

typedef double (*Sigmet_StorToMxFn)(double, void *);

/*
   These functions access and manipulate built in Sigmet data types.
   They do NOT provide information about additional, user defined, types.
 */

double Sigmet_Bin4Rad(unsigned long);
double Sigmet_Bin2Rad(unsigned short);
unsigned long Sigmet_RadBin4(double);
unsigned long Sigmet_RadBin2(double);

int Sigmet_DataType_GetN(char *, enum Sigmet_DataTypeN *);
char *Sigmet_DataType_Abbrv(enum Sigmet_DataTypeN);
char *Sigmet_DataType_Descr(enum Sigmet_DataTypeN);
char *Sigmet_DataType_Unit(enum Sigmet_DataTypeN);
enum Sigmet_StorFmt Sigmet_DataType_StorFmt(enum Sigmet_DataTypeN);
double Sigmet_DblDbl(double, void *);
Sigmet_StorToMxFn Sigmet_DataType_StorToComp(enum Sigmet_DataTypeN);

/*
   Sweep header
 */

struct Sigmet_Sweep_Hdr {
    int ok;				/* Sweep status. If ok[i],
					   i'th sweep is complete. */
    double time;			/* Sweep start time, Julian
					   day */
    double angle;			/* Sweep angle, radians */
};

/*
   Ray header
 */

struct Sigmet_Ray_Hdr {
    int ok;				/* Status. ok == 1 => ray is good */
    double time;			/* Time, Julian day, */
    int num_bins;			/* Number of bins in ray,
					   varies from ray to ray */
    double tilt0;			/* Tilt at start of ray, radians */
    double tilt1;			/* Tilt at end of ray, radians, */
    double az0;				/* Azimuth at start of ray, radians */
    double az1;				/* Azimuth at end of ray, radians */
};

/*
   Data array. A volume will have one of these for each data type in the
   volume. If not NULL, u1, u2, or f is an array with dimensions
   [sweep][ray][bin] with data values from the volume.
 */

#define SIGMET_NAME_LEN 32
#define SIGMET_DESCR_LEN 128

struct Sigmet_Dat {
    char data_type_s[SIGMET_NAME_LEN];	/* Data type abbreviation */
    char descr[SIGMET_DESCR_LEN];	/* Information about the data type */
    char unit[SIGMET_NAME_LEN];		/* Physical unit */
    enum Sigmet_StorFmt stor_fmt;	/* Storage format, determines which
					   member of vals is in use for this
					   data type */
    enum Sigmet_DataTypeN sig_type;	/* Sigmet data type, if any */
    Sigmet_StorToMxFn stor_to_comp;     /* Function to convert storage value to
                                           computation value */
    union {
	U1BYT ***u1;			/* 1 byte data */
	U2BYT ***u2;			/* 2 byte data */
	float ***f;			/* Floating point data */
    } vals;
    int vals_id;			/* Shared memory identifier for vals,
					   or -1 */
};

/*
   Maximum number of data types allowed in a Sigmet volume.
 */

#define SIGMET_MAX_TYPES 512


/*
   struct Sigmet_Vol:

   Structure for a Sigmet raw product file.  Sequence of members
   imitates sequence of data in the file, so there is a some
   repetition and several unused members.

   Units for members taken directly from the Sigmet volume are as indicated
   in the IRIS Programmer Manual (i.e. nothing is converted during input).
   Units for derived members are as indicated.  In particular, angles from
   the volume are unsigned integer binary angles (cf. IRIS Programmer's Manual,
   3.1).
 */

struct Sigmet_Vol {
    int has_headers;			/* true => struct has headers */
    struct Sigmet_Product_Hdr ph;	/* Record #1 */
    struct Sigmet_Ingest_Header ih;	/* Record #2 */
    int xhdr;				/* true => extended headers present */
    int num_types;			/* Number of data types */
    enum Sigmet_DataTypeN
	types_fl[SIGMET_NTYPES];	/* Data types in raw product
					   file. This means Sigmet
					   types, including DB_XDR. */
    int truncated;			/* If true, volume does not
					   have data for the number
					   of sweeps and rays given
					   in the headers.  This usually
					   happens when operator orders
					   "STOP NOW" during the task,
					   or if a volume transfer fails */
    int num_sweeps_ax;			/* Actual number of sweeps */
    struct Sigmet_Sweep_Hdr *sweep_hdr;	/* Sweep headers, dimensioned
					   num_sweeps_ax */
    int sweep_hdr_id;			/* Shared memory identifier for sweep
					   headers */
    struct Sigmet_Ray_Hdr **ray_hdr;	/* Ray headers,
					   dimensioned [sweep][ray] */
    int ray_hdr_id;			/* Shared memory identifier for ray
					   headers */

    /*
       Data array. One element for each data type.
     */

    struct Sigmet_Dat dat[SIGMET_MAX_TYPES];

    /*
       Look up table, associates data type names with offsets in dat array
     */

    struct {
	char data_type_s[SIGMET_NAME_LEN];/* Data type abbreviation */
	int y;				/* Index in dat of data identified as of
					   type data_type_s */
    } types_tbl[SIGMET_MAX_TYPES];

    size_t size;			/* Number of bytes of memory
					   this structure is using */
    int mod;				/* If true, volume in memory
					   is different from volume in
					   raw product file */
    int shm;				/* If true, volume allocations are in
					   shared memory. Otherwise, allocations
					   are in process address space. */
};

/*
   Return values. See sigmet(3).
 */

enum SigmetStatus {
    SIGMET_OK, SIGMET_IO_FAIL, SIGMET_BAD_FILE, SIGMET_BAD_VOL,
    SIGMET_MEM_FAIL, SIGMET_BAD_ARG, SIGMET_RNG_ERR, SIGMET_BAD_TIME,
    SIGMET_HELPER_FAIL
};

/*
   These functions access Sigmet raw product files.
 */

void Sigmet_Vol_Init(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_Vol_Free(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_ShMemAttach(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_ShMemDetach(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_Vol_ReadHdr(FILE *, struct Sigmet_Vol *);
enum SigmetStatus Sigmet_Vol_DataTypeHdrs(struct Sigmet_Vol *, int, char **,
	char **, char **);
void Sigmet_Vol_PrintHdr(FILE *, struct Sigmet_Vol *);
void Sigmet_Vol_PrintMinHdr(FILE *, struct Sigmet_Vol *);
enum Sigmet_ScanMode Sigmet_Vol_ScanMode(struct Sigmet_Vol *);
int Sigmet_Vol_NumTypes(struct Sigmet_Vol *);
int Sigmet_Vol_NumSweeps(struct Sigmet_Vol *);
int Sigmet_Vol_NumRays(struct Sigmet_Vol *);
int Sigmet_Vol_NumBins(struct Sigmet_Vol *, int, int);
size_t Sigmet_Vol_MemSz(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_Vol_SweepHdr(struct Sigmet_Vol *, int, int *, double *,
	double *);
enum SigmetStatus Sigmet_Vol_RayHdr(struct Sigmet_Vol *, int, int, int *,
	double *, int *, double *, double *, double *, double *);
int Sigmet_Vol_IsPPI(struct Sigmet_Vol *);
int Sigmet_Vol_IsRHI(struct Sigmet_Vol *);
enum SigmetStatus Sigmet_Vol_Read(FILE *, struct Sigmet_Vol *);
void Sigmet_Vol_LzCpy(struct Sigmet_Vol *, struct Sigmet_Vol *);
double Sigmet_Vol_RadarLon(struct Sigmet_Vol *, double *);
double Sigmet_Vol_RadarLat(struct Sigmet_Vol *, double *);
int Sigmet_Vol_NearSweep(struct Sigmet_Vol *, double);
int Sigmet_Vol_BadRay(struct Sigmet_Vol *, int, int);
void Sigmet_Vol_RayGeom(struct Sigmet_Vol *, int, double *, double *, double *,
	int *);
double Sigmet_Vol_BinStart(struct Sigmet_Vol *, int);
enum SigmetStatus Sigmet_Vol_BinOutl(struct Sigmet_Vol *, int, int, int,
	double *);
enum SigmetStatus Sigmet_Vol_PPI_Bnds(struct Sigmet_Vol *, int,
	struct GeogProj *, double *, double *, double *, double *);
enum SigmetStatus Sigmet_Vol_RHI_Bnds(struct Sigmet_Vol *, int, double *,
	double *);
enum SigmetStatus Sigmet_Vol_PPI_Outlns(struct Sigmet_Vol *, char *, int,
	double, double, int, FILE *);
enum SigmetStatus Sigmet_Vol_RHI_Outlns(struct Sigmet_Vol *, char *, int,
	double, double, int, int, FILE *);
enum SigmetStatus Sigmet_Vol_NewField(struct Sigmet_Vol *, char *, char *,
	char *);
enum SigmetStatus Sigmet_Vol_DelField(struct Sigmet_Vol *, char *);
enum SigmetStatus Sigmet_Vol_Fld_SetVal(struct Sigmet_Vol *, char *, float);
enum SigmetStatus Sigmet_Vol_Fld_SetRBeam(struct Sigmet_Vol *, char *);
enum SigmetStatus Sigmet_Vol_Fld_Copy(struct Sigmet_Vol *, char *, char *);
enum SigmetStatus Sigmet_Vol_Fld_AddVal(struct Sigmet_Vol *, char *, float);
enum SigmetStatus Sigmet_Vol_Fld_AddFld(struct Sigmet_Vol *, char *, char *);
enum SigmetStatus Sigmet_Vol_Fld_SubVal(struct Sigmet_Vol *, char *, float);
enum SigmetStatus Sigmet_Vol_Fld_SubFld(struct Sigmet_Vol *, char *, char *);
enum SigmetStatus Sigmet_Vol_Fld_MulVal(struct Sigmet_Vol *, char *, float);
enum SigmetStatus Sigmet_Vol_Fld_MulFld(struct Sigmet_Vol *, char *, char *);
enum SigmetStatus Sigmet_Vol_Fld_DivVal(struct Sigmet_Vol *, char *, float);
enum SigmetStatus Sigmet_Vol_Fld_DivFld(struct Sigmet_Vol *, char *, char *);
enum SigmetStatus Sigmet_Vol_Fld_Log10(struct Sigmet_Vol *, char *);
enum SigmetStatus Sigmet_Vol_IncrTm(struct Sigmet_Vol *, double);
enum SigmetStatus Sigmet_Vol_ShiftAz(struct Sigmet_Vol *, double);
double Sigmet_Vol_VNyquist(struct Sigmet_Vol *);
int Sigmet_Vol_GetFld(struct Sigmet_Vol *, char *, struct Sigmet_Dat **);
float Sigmet_Vol_GetDatum(struct Sigmet_Vol *, int, int, int, int);
enum SigmetStatus Sigmet_Vol_GetRayDat(struct Sigmet_Vol *, int, int, int,
	float **);
enum SigmetStatus Sigmet_Vol_ToDorade(struct Sigmet_Vol *, int,
	struct Dorade_Sweep *);

#endif
