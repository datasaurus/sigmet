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
   .	$Revision: 1.1 $ $Date: 2009/10/02 21:58:31 $
   .
   .	Reference: IRIS Programmer's Manual, September 2002.
 */

#ifndef SIGMET_H_
#define SIGMET_H_

#include <float.h>
#include <stdio.h>

/* Length of a record in a Sigmet raw file */
#define REC_LEN 6144

/* Year, month, day, second */
struct ymds {
    int sec;		/* Seconds since midnight */
    unsigned msec;	/* Milliseconds */
    char utc;		/* If true, time is UTC */
    int year;
    int month;
    int day;
};

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

/* Volume scan modes.  Refer to task_scan_info struct in IRIS Programmer's Manual */
enum Sigmet_ScanMode {PPI_S = 1, RHI, MAN_SCAN, PPI_C, FILE_SCAN};

/*
   Structure for a Sigmet raw product file.  Sequence of members
   imitates sequence of data in the file, so there is a some
   repetition and several unused members.

   Side comments indicate (record#,offset) to the value in a record
   from the raw product file.

   Units for members taken directly from the Sigmet volume are as indicated
   in the IRIS Programmer Manual (i.e. nothing is converted during input).
   Units for derived members are as indicated.  In particular, angles from
   the volume are unsigned integer binary angles.
 */

struct Sigmet_Vol {

    /* Record #1 = product_hdr */

    /* Values from product_hdr->product_configuration */
    unsigned short product_type_code;			/* (1,24) */
    unsigned short scheduling_code;			/* (1,26) */
    long seconds_between_runs;				/* (1,28) */
    struct ymds input_ingest_sweep_time;		/* (1,44) */
    struct ymds input_ingest_file_time;			/* (1,56) */
    char prod_config_file[12];				/* (1,74) */
    char task_name[12];					/* (1,86) */
    unsigned short prod_config_flag_word;		/* (1,98) */
    long x_scale;					/* (1,100) */
    long y_scale;					/* (1,104) */
    long z_scale;					/* (1,108) */
    long x_size;					/* (1,112) */
    long y_size;					/* (1,116) */
    long z_size;					/* (1,120) */
    long radar_X;					/* (1,124) */
    long radar_Y;					/* (1,128) */
    long radar_Z;					/* (1,132) */
    long max_range_cm;					/* (1,136) */
    unsigned short data_type_generated;			/* (1,142) */
    char projection_name[12];				/* (1,144) */
    unsigned short input_data_type;			/* (1,156) */
    unsigned char projection_type;			/* (1,158) */
    short radial_smoother;				/* (1,160) */
    short prod_conf_run_count;				/* (1,162) */
    long zr_constant;					/* (1,164) */
    long zr_exponent;					/* (1,168) */
    short x_smoother;					/* (1,172) */
    short y_smoother;					/* (1,174) */

    /* Values from product_hdr->product_configuration->product_specific_info */
    long data_bit_mask;					/* (1,176) */
    long range_last_bin_cm;				/* (1,180) */
    unsigned long format_conversion_flag;		/* (1,184) */
    unsigned long prod_spec_flag_word;			/* (1,188) */
    long sweep_number;					/* (1,192) */

    /* Values from product_hdr->product_configuration->color_scale_def */
    unsigned long color_scale_flags;			/* (1,284) */
    long starting_level;				/* (1,288) */
    long level_step;					/* (1,292) */
    short num_colors;					/* (1,296) */
    unsigned short color_set_and_scale;			/* (1,298) */
    unsigned lev_start_vals[16];			/* (1,300) */

    /* Values from product_hdr->product_end */
    char site_name[16];					/* (1,332) */
    char prod_IRIS_Open_vsn[8];				/* (1,348) */
    char ing_IRIS_Open_vsn[8];				/* (1,356) */
    struct ymds oldest_ingest_input_time;		/* (1,364) */
    char ingest_site_name[16];				/* (1,422) */
    short minutes_ahead_gmt;				/* (1,438) */
    unsigned center_latitude;				/* (1,440) */
    unsigned center_longitude;				/* (1,444) */
    short ground_elevation;				/* (1,448) */
    short tower_height;					/* (1,450) */
    long prf;						/* (1,452) */
    long pulse_width;					/* (1,456) */
    unsigned short signal_processor;			/* (1,460) */
    unsigned short trigger_rate_scheme;			/* (1,462) */
    short num_of_samples;				/* (1,464) */
    char clutter_file[12];				/* (1,466) */
    unsigned short filter_number;			/* (1,478) */
    long wavelen_cm;					/* (1,480) */
    long truncation_height_cm;				/* (1,484) */
    long range_1st_bin_cm;				/* (1,488) */
    long range_last_bin_cm_1;				/* (1,492) */
    long num_output_bins;				/* (1,496) */
    unsigned short prod_end_flag;			/* (1,500) */
    short num_ing_prod_files;				/* (1,502) */
    unsigned short log_based_filter_1st_bin;		/* (1,568) */
    unsigned projection_ref_lat;			/* (1,572) */
    unsigned projection_ref_lon;			/* (1,576) */
    short product_sequence_number;			/* (1,580) */
    int pic_color_nums[16];				/* (1,582) */
    short reference_height_meters;			/* (1,616) */
    short num_elems_prod_rslt;				/* (1,618) */

    /* Record #2 = ingest_header */

    /* Values from ingest_header->ingest_configuration */
    char file_name[80];					/* (2,12) */
    short num_assoc_data_files;				/* (2,92) */
    long size_files;					/* (2,96) */
    struct ymds volume_start_time;			/* (2,100) */
    short ray_headers_sz;				/* (2,124) */
    short extended_ray_headers_sz;			/* (2,126) */
    short task_config_table_num;			/* (2,128) */
    char IRIS_Open_version[8];				/* (2,136) */
    char site_name_1[16];				/* (2,162) */
    short minutes_ahead_gmt_1;				/* (2,178) */
    unsigned latitude;					/* (2,180) */
    unsigned longitude;					/* (2,184) */
    short ground_height;				/* (2,188) m above SL */
    short tower_height_1;				/* (2,190) m */
    short resolution; 					/* (2,192) rays/rev */
    short index_first_ray;				/* (2,194) */
    short rays_in_sweep;				/* (2,196) */
    short num_bytes_each_gparam;			/* (2,198) */
    long altitude;					/* (2,200) cm above SL */
    int velocity[3];					/* (2,204) cm/sec e,n,up */
    int offset_fm_INU[3];				/* (2,216) */

    /* Values from ingest_header->task_configuration->task_sched_info */
    long start_time;					/* (2,504) sec in day */
    long stop_time;					/* (2,508) sec in day */
    long desired_skip_time;				/* (2,512) sec */
    long time_last_run;					/* (2,516) sec in day */
    long time_used_last_run;				/* (2,520) sec */
    long relative_day_last_run;				/* (2,524) */
    unsigned short task_sched_flag;			/* (2,528) */

    /* Values from ingest_header->task_configuration->task_dsp_info */
    unsigned short dsp_type;				/* (2,626) */
    unsigned long data_type_mask;			/* (2,628) */
    unsigned aux_data_def[27];				/* (2,632) */
    long prf_1;						/* (2,760) Hertz */
    long pulse_width_1;					/* (2,764) 1/100 microsec */
    enum Sigmet_Multi_PRF multi_prf_mode_flag;		/* (2,768) */
    short dual_prf_delay;				/* (2,770) */
    unsigned short agc_feedback_code;			/* (2,772) */
    short sample_size;					/* (2,774) */
    unsigned short gain_control_flag;			/* (2,776) */
    char clutter_file_name[12];				/* (2,778) */
    unsigned char linear_based_filter_num;		/* (2,790) */
    unsigned char log_based_filter_num;			/* (2,791) */
    short attenuation;					/* (2,792) in 1/10 dB */
    short gas_attenuation;				/* (2,794) */

    /* Values from ingest_header->task_configuration->task_calib_info */
    short reflectiviy_slope;				/* (2,944) */
    short reflectivity_noise;				/* (2,946) */
    short clutter_corr_threshold;			/* (2,948) */
    short sqi_threshold;				/* (2,950) */
    short power_threshold;				/* (2,952) */
    short calibration_reflectivity;			/* (2,962) */
    unsigned short uncor_refl_threshold_flags;		/* (2,964) */
    unsigned short cor_refl_threshold_flags;		/* (2,966) */
    unsigned short velocity_threshold_flags; 		/* (2,968) */
    unsigned short width_threshold_flags;		/* (2,970) */
    unsigned short zdr_threshold_flags;			/* (2,972) */
    unsigned short task_calib_flags;			/* (2,980) */
    short zdr_bias;					/* (2,986) */

    /* Values from ingest_header->task_configuration->task_range_info */
    long rng_1st_bin;					/* (2,1264) cm */
    long rng_last_bin;					/* (2,1268) cm */
    short num_input_bins;				/* (2,1272) */
    short num_output_bins_1;				/* (2,1274) */
    long input_bin_step;				/* (2,1276) */
    long output_bin_step;				/* (2,1280) cm */
    unsigned short var_rng_bin_spacing_flag;		/* (2,1284) */
    short rng_bin_avg_flag;				/* (2,1286) */

    /* Values from ingest_header->task_configuration->task_scan_info */
    unsigned short scan_mode;				/* (2,1424) */
    short desired_angular_resolution;			/* (2,1426) */
    short num_sweeps;					/* (2,1430) */

    /*
       Scan mode info.  Member used depends on the type of scan, one of
       task_ppi_scan_info, task_rhi_scan_info, task_file_scan_info,
       or task_manual_scan_info.  All angles are binary angles.
     */
    union {
	struct {
	    unsigned start_az;				/* (2,1432) */
	    unsigned end_az;				/* (2,1434) */
	    unsigned elevs[40];				/* (2,1436) */
	} ppi;
	struct {
	    unsigned start_elev;			/* (2,1432) */
	    unsigned end_elev;				/* (2,1434) */
	    unsigned az[40];				/* (2,1436) */
	} rhi;
	struct {
	    unsigned first_az;				/* (2,1432) */
	    unsigned first_elev;			/* (2,1434) */
	    char ant_ctrl[12];				/* (2,1436) */
	} file;
	struct {
	    unsigned short Flags;			/* (2,1432) */
	} manual;
    } scan_mode_info;

    /* Values from ingest_header->task_configuration->task_misc_info */
    long wavelength;					/* (2,1744) 1/100 cm */
    char t_r_serial_number[16];				/* (2,1748) */
    long transmit_power;				/* (2,1764) watts */
    unsigned short task_misc_flags;			/* (2,1768) */
    unsigned short polarization;			/* (2,1770) */
    long truncation_height;				/* (2,1772) cm */
    unsigned short task_misc_flag;			/* (2,1794) */
    short comments_sz;					/* (2,1806) byte */

    /* Values from ingest_header->task_configuration->task_end_info */
    short task_major_number;				/* (2,2064) */
    short task_minor_number;				/* (2,2066) */
    char task_config_file_name[12];			/* (2,2068) */
    char task_description[80];				/* (2,2080) */
    long num_tasks_in_hybrid_task;			/* (2,2160) */
    unsigned short task_state;				/* (2,2164) */

    /* Members NOT taken directly from the volume */
    unsigned num_types;				/* Number of data types */
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
int Sigmet_ReadVol(FILE *f, struct Sigmet_Vol *sigPtr);
int Sigmet_BadRay(struct Sigmet_Vol *sigPtr, int s, int r);

#endif
