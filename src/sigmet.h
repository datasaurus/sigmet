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
   .	$Revision: 1.5 $ $Date: 2009/10/21 15:39:19 $
   .
   .	Reference: IRIS Programmer's Manual, September 2002.
 */

#ifndef SIGMET_H_
#define SIGMET_H_

#include <float.h>
#include <stdio.h>

#define	PI		3.1415926535897932384
#define	PI_2		1.57079632679489661923
#define RAD_PER_DEG	0.01745329251994329576
#define DEG_PER_RAD	57.29577951308232087648

/* Length of a record in a Sigmet raw file */
#define REC_LEN 6144

/* "Value" when there is not data */
#define NODAT FLT_MAX
#define ISNODAT(x) (x == FLT_MAX)

/* These constants identify the Sigmet data types */
#define SIGMET_NTYPES 28
enum Sigmet_DataType {
    DB_XHDR,	DB_DBT,		DB_DBZ,		DB_VEL,		DB_WIDTH,
    DB_ZDR,	DB_DBZC,	DB_DBT2,	DB_DBZ2,	DB_VEL2,
    DB_WIDTH2,	DB_ZDR2,	DB_RAINRATE2,	DB_KDP,		DB_KDP2,
    DB_PHIDP,	DB_VELC,	DB_SQI,		DB_RHOHV,	DB_RHOHV2,
    DB_DBZC2,	DB_VELC2,	DB_SQI2,	DB_PHIDP2,	DB_LDRH,
    DB_LDRH2,	DB_LDRV,	DB_LDRV2
};

/* Multi PRF mode flags */
enum Sigmet_Multi_PRF {ONE_ONE, TWO_THREE, THREE_FOUR, FOUR_FIVE};

/* Functions to use with Sigmet data types  */
char *Sigmet_DataType_Abbrv(enum Sigmet_DataType y);
char *Sigmet_DataType_Descr(enum Sigmet_DataType y);
float Sigmet_NoData(void);
int Sigmet_IsData(float);
int Sigmet_IsNoData(float);
float Sigmet_DataType_ItoF(enum Sigmet_DataType y, unsigned i);
double Sigmet_Bin4Rad(unsigned long a);
double Sigmet_Bin2Rad(unsigned short a);

/* Volume scan modes.  Refer to task_scan_info struct in IRIS Programmer's Manual */
enum Sigmet_ScanMode {PPI_S = 1, RHI, MAN_SCAN, PPI_C, FILE_SCAN};

/* The following structures store data from volume headers.  Ref. IRIS Programmer's Manual */

/* Year, month, day, second */
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

/* For raw volume, product_specific_info is raw_psi_struct */
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
    int run_cnt;
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
    int n_samples;
    char clutter_filter[12];
    unsigned lin_filter;
    int wave_len;
    int trunc_ht;
    int rng_bin0;
    int rng_last_bin;
    int n_out_bins;
    unsigned flag;
    unsigned polarization;
    int io_cal_hpol;
    int noise_cal_hpol;
    int radar_const;
    unsigned recv_bandw;
    int noise_hpol;
    int noise_vpol;
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
    unsigned n_logfilter;
    unsigned cluttermap_used;
    unsigned proj_lat;
    unsigned proj_lon;
    int i_prod;
    int melt_level;
    int radar_ht_ref;
    int n_elem;
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
    int n_sweeps;
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
    unsigned rays_in_sweep;
    int nbytes_gparam;
    int altitude;
    int velocity[3];
    int offset_inu[3];
    unsigned fault;
    int meltz;
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
    int h_io_cal;
    int v_io_cal;
    int h_noise;
    int v_noise;
    int h_radar_const;
    int v_radar_const;
    unsigned bandwidth;
    unsigned flags2;
};

struct Sigmet_Task_Range_Info {
    int rng_1st_bin;
    int rng_last_bin;
    int nbins_in;
    int nbins_out;
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
    unsigned scan_mode;
    int resoln;
    int n_sweeps;
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
    unsigned h_beam_width;
    unsigned v_beam_width;
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
   struct Sigmet_Vol:

   Structure for a Sigmet raw product file.  Sequence of members
   imitates sequence of data in the file, so there is a some
   repetition and several unused members.

   Units for members taken directly from the Sigmet volume are as indicated
   in the IRIS Programmer Manual (i.e. nothing is converted during input).
   Units for derived members are as indicated.  In particular, angles from
   the volume are unsigned integer binary angles.
 */

struct Sigmet_Vol {

    /* Volume headers */
    struct Sigmet_Product_Hdr ph;		/* Record #1 */
    struct Sigmet_Ingest_Header ih;		/* Record #2 */

    /* Ray headers and data */
    int num_types;				/* Number of data types */
    enum Sigmet_DataType types[SIGMET_NTYPES];	/* Data types */
    double *sweep_time;				/* Sweep start time, Julian day,
						   dimensioned [sweep] */
    double *sweep_angle;			/* Sweep angle, radians,
						   dimensioned [sweep] */
    double **ray_time;				/* Ray time, Julian day,
						   dimesioned [sweep][ray] */
    unsigned **ray_nbins;			/* Number of bins in ray,
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
    int ****dat;				/* Data.  Dimensioned
						   [sweep][type][ray][bin]
						   Units and encoding depend on
						   type. */
    int truncated;				/* If true, volume does not
						   have data for the number
						   of sweeps and rays given
						   in the headers.  This usually
						   happens when operator orders
						   "STOP NOW" during the task */
};

/* Global functions */
void Sigmet_InitVol(struct Sigmet_Vol *sigPtr);
void Sigmet_FreeVol(struct Sigmet_Vol *sigPtr);
int Sigmet_ReadHdr(FILE *f, struct Sigmet_Vol *sigPtr);
void Sigmet_PrintHdr(struct Sigmet_Vol vol, FILE *out);
int Sigmet_ReadVol(FILE *f, struct Sigmet_Vol *sigPtr);
int Sigmet_BadRay(struct Sigmet_Vol *sigPtr, int s, int r);

#endif
