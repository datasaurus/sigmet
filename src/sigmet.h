/*
   -	sigmet.h --
   -		This header file declares structures and functions
   -		that store and access Sigmet raw product files.
   -
   .	Copyright (c) 2009 Gordon D. Carrie.
   .	All rights reserved.
   .
   .	Please send feedback to user0@tkgeomap.org
   .
   .	$Revision: 1.45 $ $Date: 2010/11/17 16:13:14 $
   .
   .	Reference: IRIS Programmer's Manual, February 2009.
 */

#ifndef SIGMET_H_
#define SIGMET_H_

#define SIGMET_VSN "1.0"

#include <float.h>
#include <stdio.h>
#include "type_nbit.h"
#include "dorade_lib.h"

#define	PI		3.1415926535897932384
#define	PI_2		1.57079632679489661923
#define RAD_PER_DEG	0.01745329251994329576
#define DEG_PER_RAD	57.29577951308232087648

/*
   The IRIS Programmer's Manual (section 3.3) defines SIGMET_NTYPES data types.
   The Sigmet_DataTypeN enumerator indexes these types, plus generic and pseudo-
   types.
   DB_DBL means double value. Use as is. Type has no conversion from integer to
   floating point measurement.
   DB_SKIP means do not use. Place holder.
   DB_ERROR means unknown or failure.
 */

#define SIGMET_NTYPES 28
enum Sigmet_DataTypeN {
    DB_XHDR,	DB_DBT,		DB_DBZ,		DB_VEL,		DB_WIDTH,
    DB_ZDR,	DB_DBZC,	DB_DBT2,	DB_DBZ2,	DB_VEL2,
    DB_WIDTH2,	DB_ZDR2,	DB_RAINRATE2,	DB_KDP,		DB_KDP2,
    DB_PHIDP,	DB_VELC,	DB_SQI,		DB_RHOHV,	DB_RHOHV2,
    DB_DBZC2,	DB_VELC2,	DB_SQI2,	DB_PHIDP2,	DB_LDRH,
    DB_LDRH2,	DB_LDRV,	DB_LDRV2,	DB_DBL,		DB_SKIP,
    DB_ERROR
};

/*
   Structures of this type store a Sigmet data type enumerator, and associated
   abbreviation.
   Abbreviations for Sigmet data types from IRIS Programmer's Manual Section 3.3
   are hard coded in sigmet_data.c. This structure is needed for additional,
   user defined data types, which are DB_DBL with some user supplied
   abbreviation.
 */

struct Sigmet_DataType {
    enum Sigmet_DataTypeN sig_type;
    char *abbrv;
    char *unit;
};

/*
   Multi PRF mode flags
 */

enum Sigmet_Multi_PRF {ONE_ONE, TWO_THREE, THREE_FOUR, FOUR_FIVE};

/*
   Volume scan modes.  Refer to task_scan_info struct in IRIS Programmer's Manual
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
    int sec;		/* Seconds since midnight */
    unsigned msec;	/* Milliseconds */
    char utc;		/* If true, time is UTC */
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
    char config_file[12];
    char task_name[12];
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
    char proj[12];
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
    char site_name_prod[16];
    char iris_prod_vsn[8];
    char iris_ing_vsn[8];
    int local_wgmt;
    char hw_name[16];
    char site_name_ing[16];
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
    char clutter_filter[12];
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
    char tz[8];
};

struct Sigmet_Product_Hdr {
    struct Sigmet_Structure_Header sh;
    struct Sigmet_Product_Configuration pc;
    struct Sigmet_Product_End pe;
};

struct Sigmet_Ingest_Configuration {
    char file_name[80];
    int num_assoc_files;
    int num_sweeps;
    int size_files;
    struct Sigmet_YMDS_Time vol_start_time;
    int ray_headers_sz;
    int extended_ray_headers_sz;
    int task_config_table_num;
    int playback_vsn;
    char IRIS_vsn[8];
    char hw_site_name[16];
    int local_wgmt;
    char su_site_name[16];
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
    char tz[8];
    unsigned flags;
    char config_name[16];
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
    char clutter_file[12];
    unsigned lin_filter_num;
    unsigned log_filter_num;
    int attenuation;
    unsigned gas_attenuation;
    unsigned clutter_flag;
    unsigned xmt_phase;
    unsigned ray_hdr_mask;
    unsigned time_series_flag;
    char custom_ray_hdr[16];
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
    char ant_ctrl[12];
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
    char tr_ser[16];
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
    char task_config[12];
    char task_descr[80];
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
   Data array, dimensioned [sweep][ray][bin]. Type used depends on
   Sigmet data type. U1BYT and U2BYT are declared in type_nbit.h.
 */

union Sigmet_DatArr {
    U1BYT ***d1;				/* 1 byte data */
    U2BYT ***d2;				/* 2 byte data */
    double ***dbl;				/* Floating point values */
};

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

    /*
       Volume headers
     */

    int has_headers;				/* true => struct has headers */
    struct Sigmet_Product_Hdr ph;		/* Record #1 */
    struct Sigmet_Ingest_Header ih;		/* Record #2 */

    /*
       Ray headers and data
     */

    int xhdr;					/* true => extended headers */
    int num_types;				/* Number of data types */
    struct Sigmet_DataType *types;		/* Data types in the volume.
						   This includes Sigmet data
						   types and user defined types,
						   but not DB_XHDR */
    enum Sigmet_DataTypeN
	types_fl[SIGMET_NTYPES];		/* Data types in raw product
						   file. This means Sigmet
						   types, including DB_XDR. */
    int truncated;				/* If true, volume does not
						   have data for the number
						   of sweeps and rays given
						   in the headers.  This usually
						   happens when operator orders
						   "STOP NOW" during the task,
						   or if a volume transfer fails */
    int num_sweeps_ax;				/* Actual number of sweeps */
    int *sweep_ok;				/* Sweep status, dimensioned
						   [sweep]. If sweep_ok[i],
						   i'th sweep is complete. */
    double *sweep_time;				/* Sweep start time, Julian day,
						   dimensioned [sweep] */
    double *sweep_angle;			/* Sweep angle, radians,
						   dimensioned [sweep] */
    int **ray_ok;				/* Ray status, dimesioned
						   [sweep][ray].  If ray_ok[j][i]
						   == 1, ray is good */
    double **ray_time;				/* Ray time, Julian day,
						   dimesioned [sweep][ray] */
    int **ray_num_bins;				/* Number of bins in ray,
						   dimensioned [sweep][ray],
						   varies from ray to ray */
    double **ray_tilt0;				/* Tilt at start of ray, radians,
						   dimensioned [sweep][ray] */
    double **ray_tilt1;				/* Tilt at end of ray, radians,
						   dimensioned [sweep][ray] */
    double **ray_az0;				/* Azimuth at start of ray, radians,
						   dimensioned [sweep][ray] */
    double **ray_az1;				/* Azimuth at end of ray, radians,
						   dimensioned [sweep][ray] */
    union Sigmet_DatArr *dat;			/* Data, dimensioned [type] */
    size_t size;				/* Number of bytes of memory this
						   structure is using */
};

/*
   These functions provide information about built in Sigmet data types.
   They do NOT provide information about additional (DB_DBL) or bogus types.
   Use the DataType interface for additional types.
 */

enum Sigmet_DataTypeN Sigmet_DataTypeN(char *);
char *Sigmet_DataType_Abbrv(enum Sigmet_DataTypeN);
char *Sigmet_DataType_Descr(enum Sigmet_DataTypeN);
char *Sigmet_DataType_Unit(enum Sigmet_DataTypeN);
double Sigmet_NoData(void);
int Sigmet_IsData(double);
int Sigmet_IsNoData(double);
double Sigmet_Bin4Rad(unsigned long);
double Sigmet_Bin2Rad(unsigned short);
unsigned long Sigmet_RadBin4(double);
unsigned long Sigmet_RadBin2(double);

/*
   Values of this type are returned by Sigmet_ReadHdr and Sigmet_ReadVol.
   They indicate whether reading succeeded, or if not, how it failed.
 */

enum Sigmet_ReadStatus {SIGMET_VOL_READ_OK, SIGMET_VOL_INPUT_FAIL,
    SIGMET_VOL_BAD_VOL, SIGMET_VOL_MEM_FAIL};

/*
   These functions access Sigmet raw product files.
 */

void Sigmet_InitVol(struct Sigmet_Vol *);
void Sigmet_FreeVol(struct Sigmet_Vol *);
int Sigmet_GoodVol(FILE *);
enum Sigmet_ReadStatus Sigmet_ReadHdr(FILE *, struct Sigmet_Vol *);
void Sigmet_PrintHdr(FILE *, struct Sigmet_Vol *);
enum Sigmet_ReadStatus Sigmet_ReadVol(FILE *, struct Sigmet_Vol *);
int Sigmet_BadRay(struct Sigmet_Vol *, int, int);
int Sigmet_BinOutl(struct Sigmet_Vol *, int, int, int, double *);
double Sigmet_VNyquist(struct Sigmet_Vol *);
double Sigmet_VolDat(struct Sigmet_Vol *, int, int, int, int);
int Sigmet_ToDorade(struct Sigmet_Vol *, int, struct Dorade_Sweep *);

#endif
