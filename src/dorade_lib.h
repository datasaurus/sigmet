/*
   -    dorade_lib.h --
   -            This header file declares structures and functions
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
   .    $Revision: 1.40 $ $Date: 2014/10/08 09:07:06 $
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

#ifndef DORADE_LIB_H_
#define DORADE_LIB_H_

#include "unix_defs.h"
#include <limits.h>
#include <float.h>
#include <time.h>
#include <stdio.h>

#define DORADE_VERSION "0.3"

/*
   Maximum number of parameters.
 */

#define DORADE_MAX_PARMS 512

/*
   Not applicable, miss, bad, or deleted.
 */

#define DORADE_BAD_I2 -999
#define DORADE_BAD_I4 -999
#define DORADE_BAD_F -999.0
#define DORADE_BAD_D -999.0

/*
 * Binary formats
 */

# define      DD_8_BITS 1
# define     DD_16_BITS 2
# define     DD_24_BITS 3
# define   DD_32_BIT_FP 4
# define   DD_16_BIT_FP 5

/*
   Side comments for struc members give (offset,width) of member within block.
   See the DORADE FORMAT pdf.
 */

/*
   Comment block - COMM
   Length -  508 bytes
 */

struct Dorade_COMM {
    char comment[501];                  /* (8,1) comment text */
};

/*
   Super Sweep Identification block - SSWB
   Length -  196 bytes
 */

struct Dorade_SSWB {
    int last_used;                      /* (8,4) Unix time "last_used " is
                                           an age off indicator where >0
					   implies Unix time of the last access
					   and 0 implies this sweep should not
					   be aged off */
    int i_start_time;	                /* (12,4) */
    int i_stop_time;			/* (16,4) */
    int sizeof_file;               	/* (20,4) */
    int compression_flag;               /* (24,4) */
    int volume_time_stamp;              /* (28,4) to reference current volume */
    int num_parms;			/* (32,4) number of parameters */
    char radar_name[9];                /* (36,1) */
    double start_time;                  /* (44,8) */
    double stop_time;                   /* (52,8) */
    int version_num;                    /* (60,4) */
    int status;                         /* (68,4) */
};

/*
   Volume description block - VOLD
   Length -  72 bytes
 */

struct Dorade_VOLD {
    int format_version;                 /* (8,2) ELDORA/ASTRAEA field format
					   revision number. */
    int volume_num;                     /* (10,2) Volume Number in current
					   operations */
    int maximum_bytes;                  /* (12,4) Maximum number of bytes
					   in any physical record in this
					   volume */
    char proj_name[21];                 /* (16,1) Project number or name */
    int year;                           /* (36,2) Year data taken in years */
    int month;                          /* (38,2) Month data taken in months */
    int day;                            /* (40,2) Day data taken in days */
    int data_set_hour;                  /* (42,2) Hour data taken in hours */
    int data_set_minute;                /* (44,2) Minute data taken in minutes
					 */
    int data_set_second;                /* (46,2) Second data taken in seconds
					 */
    char flight_number[9];              /* (48,1) Flight number */
    char gen_facility[9];               /* (56,1) Identifier of facility
					   that generated this recording */
    int gen_year;                       /* (64,2) Year this recording was
					   generated in years */
    int gen_month;                      /* (66,2) Month this recording was
					   generated in months */
    int gen_day;                        /* (68,2) Day this recording was
					   generated in days */
    int num_sensors;                    /* (70,2) Total number of sensor
					   descriptors that follow */
};

/*
   Radar description - RADD
   Length -  300 bytes
 */

struct Dorade_RADD {
    char radar_name[9];                 /* (8,1) Eight character radar name */
    double radar_const;                 /* (16,4) Radar/lidar constant in?? */
    double peak_power;                  /* (20,4) Typical peak power of the
					   sensor in kw. Pulse energy is really
					   the peak_power pulse_width */
    double noise_power;                 /* (24,4) Typical noise power of
					   the sensor in dBm. */
    double receiver_gain;               /* (28,4) Gain of the receiver in db */
    double antenna_gain;                /* (32,4) Gain of the antenna in db */
    double system_gain;                 /* (36,4) System gain in db.
					   (Ant G WG loss) */
    double horz_beam_width;             /* (40,4) Horizontal beam width in
					   degrees.  Beam divergence in
					   milliradians is equivalent to
					   beamwidth */
    double vert_beam_width;             /* (44,4) Vertical beam width in degrees
					 */
    int radar_type;                     /* (48,2) RadarType (0) Ground,
					   1)Airborne Fore, 2)Airborne Aft,
					   3) airborne tail, 4)Airborne lower
					   fuselage, 5)Shipborne */
    int scan_mode;                      /* (50,2) Scan Mode (0)Calibration,
					   1)PPI (constant elevation) 2)Coplane,
					   3)RHI (Constant azimuth), 4)Vertical
					   Pointing, 5)Target (Stationary),
					   6)Manual, 7)Idle (out of control) */
    double req_rotat_vel;               /* (52,4) Requested rotational velocity
					   of the antenna in degrees / sec */
    double scan_mode_pram0;             /* (56,4) Scan mode specific
					   parameter #0 (Has different meaning
					   for different scan modes */
    double scan_mode_pram1;             /* (60,4) Scan mode specific
					   parameter #1 */
    int num_parms;                      /* (64,2) Total number of additional
					   descriptor block for this radar */
    int total_num_des;                  /* (66,2) Total number of additional
					   descriptor block for this radar */
    int data_compress;                  /* (68,2) Data compression. 0 =none,
					   1 = HRD scheme */
    int data_reduction;                 /* (70,2) Data reduction algorithm:
					   1 = none, 2 = between 2 angles,
					   3 = between concentric circles,
					   4 = above / below certain altitudes
					 */
    double data_red_parm0;              /* (72,4) 1 = smallest positive angle
					   in degrees, 2 = inner circle
					   diameter, km, 4 = minimum altitude,
					   km */
    double data_red_parm1;              /* (76,4) 1 = largest positive angle,
					   degrees, 2 = outer circle diameter,
					   km, 4 = maximum altitude */
    double radar_longitude;             /* (80,4) Longitude of radar in degrees
					 */
    double radar_latitude;              /* (84,4) Latitude of radar in degrees
					 */
    double radar_altitude;              /* (88,4) Altitude of radar above msl in
					   km */
    double eff_unamb_vel;               /* (92,4) Effective unambiguous velocity,
					   km */
    double eff_unamb_range;             /* (96,4) Effective unambiguous range,
					   km */
    int num_freq_trans;                 /* (100,2) Number of frequencies
					   transmitted */
    int num_ipps_trans;                 /* (102,2) Number of different
					   inter-pulse periods transmitted */
    double freq1;                       /* (104,4) Frequency 1 */
    double freq2;                       /* (108,4) Frequency 2 */
    double freq3;                       /* (112,4) Frequency 3 */
    double freq4;                       /* (116,4) Frequency 4 */
    double freq5;                       /* (120,4) Frequency 5 */
    double interpulse_per1;             /* (124,4) Interpulse period 1 */
    double interpulse_per2;             /* (128,4) Interpulse period 2 */
    double interpulse_per3;             /* (132,4) Interpulse period 3 */
    double interpulse_per4;             /* (136,4) Interpulse period 4 */
    double interpulse_per5;             /* (140,4) Interpulse period 5 */
    int extension_num;                  /* (144,4) 1995 extension #1 */
    char config_name[9];                /* (148,1) Used to identify this set of
					   unique radar characteristics */
    int config_num;                     /* (156,4) Facilitates a quick lookup of
					   radar characteristics for each ray.
					   Extend the radar descriptor to
					   include unique lidar parameters */
    double aperture_size;               /* (160,4) Diameter of the lidar
					   aperature in cm */
    double field_of_view;               /* (164,4) Field of view of the
					   receiver.mra; */
    double aperture_eff;                /* (168,4) Aperature efficiency in %. */
    double freq[11];                    /* (172,4) Make space for a total of 16
					   freqs */
    double interpulse_per[11];          /* (216,4) And ipps other extensions to
					   the radar descriptor */
    double pulse_width;                 /* (260,4) Typical pulse width in
					   microseconds. Pulse width is inverse
					   of the band width. */
    double primary_cop_baseln;          /* (264,4) Coplane baselines */
    double secondary_cop_baseln;        /* (268,4) */
    double pc_xmtr_bandwidth;           /* (272,4) Pulse compression transmitter
					   bandwidth */
    int pc_waveform_type;               /* (276,4) Pulse compression waveform
					   type */
    char site_name[21];                 /* (280,1) */
};

/*
   Correction factor ­ CFAC
   Length -  72 bytes
 */

struct Dorade_CFAC {
    float azimuth_corr;                /* (8,4) Correction added to
					   azimuth(deg) */
    float elevation_corr;              /* (12,4) Correction added to elevation
					   (deg) */
    float range_delay_corr;            /* (16,4) Correction used for range
					   delay(m) */
    float longitude_corr;              /* (20,4) Correction added to radar
					   longitude */
    float latitude_corr;               /* (24,4) Correction added to radar
					   latitude */
    float pressure_alt_corr;           /* (28,4) Correction added to pressure
					   altitude (msl) (km) */
    float radar_alt_corr;              /* (32,4) Correction added to radar
					   altitude above ground level (agl)
					   (km) */
    float ew_gndspd_corr;              /* (36,4) Correction added to radar
					   platform ground speed (E-W) (m/s) */
    float ns_gndspd_corr;              /* (40,4) Correction added to radar
					   platform ground speed (N-S) (m/s) */
    float vert_vel_corr;               /* (44,4) Correction added to radar
					   platform vertical velocity (m/s) */
    float heading_corr;                /* (48,4) Correction added to radar
					   platform heading (deg) */
    float roll_corr;                   /* (52,4) Correction added to radar
					   platform roll (deg) */
    float pitch_corr;                  /* (56,4) Correction added to radar
					   platform picth (deg) */
    float drift_corr;                  /* (60,4) Correction added to radar
					   platform drift (deg) */
    float rot_angle_corr;              /* (64,4) Correction added to radar
					   rotation angle (deg) */
    float tilt_corr;                   /* (68,4) Correction added to radar
					   tilt angle */
};

/*
   Parameter (data field) description ­ PARM
   Length -  216 bytes
 */

struct Dorade_PARM {
    char parm_nm[9];             	/* (8,1) Name parameter of being
					   described */
    char parm_description[41];		/* (16,1) Detailed description of this
					   parameter */
    char parm_units[9];			/* (56,1) Units parameter is written in
					 */
    int interpulse_time;                /* (64,2) Inter-pulse periods used.
					   Bits 0-1 = frequencies 1-2 */
    int xmitted_freq;                   /* (66,2) Frequencies used for this
					   parameter */
    double recvr_bandwidth;             /* (68,4) Effective receiver bandwidth
					   for this parameter in MHz */
    int pulse_width;                    /* (72,2) Effective pulse width of
					   parameter in m */
    int polarization;                   /* (74,2) Polarization of the radar
					   beam for this parameter
					   (0 Horizontal, 1 vertical,
					   2 circular, 3 elliptical) in na */
    int num_samples;                    /* (76,2) Number of samples used in
					   estimate for this parameter */
    int binary_format;                  /* (78,2) Binary format of radar data */
    char threshold_field[9];            /* (80,1) Name of parameter upon which
					   this parameter is thresholded (ascii
					   characters NONE if not thresholded)
					 */
    double threshold_value;             /* (88,4) Value of threshold in ? */
    double parameter_scale;		/* (92,4) Scale factor for parameter */
    double parameter_bias;              /* (96,4) Bias factor for parameter */
    int bad_data;                       /* (100,4) Bad data flag. */
    int extension_num;                  /* (104,4) 1995 extension #1 */
    char config_name[9];                /* (108,1) Used to identify this set of
					   unique radar characteristics */
    int config_num;                     /* (116,4) */
    int offset_to_data;                 /* (120,4) Bytes added to the data
					   struct pointer to point to the first
					   datum whether it's an RDAT or a QDAT
					 */
    double mks_conversion;              /* (124,4) */
    int num_qnames;                     /* (128,4) */
    char qdata_names[33];               /* (132,1) Each of 4 names occupies 8
					   characters of this space and is blank
					   filled. Each name identifies some
					   interesting segment of data in a
					   particular ray for this parameter. */
    int num_criteria;                   /* (164,4) */
    char criteria_names[33];            /* (168,1) Each of 4 names occupies 8
					   characters and is blank filled. These
					   names identify a single interesting
					   floating point value that is
					   associated with a particular ray for
					   this parameter. Examples might be a
					   brightness temperature or the
					   percentage of cells above or below
					   a certain value */
    int num_cells;                      /* (200,4) */
    double meters_to_first_cell;        /* (204,4) center */
    double meters_between_cells;        /* (208,4) */
    double eff_unamb_vel;               /* (212,4) Effective unambiguous
					   velocity, m/s */
    struct Dorade_PARM *next;		/* Next parameter in sweep */
};

/*
   Note. some files have a smaller size of 104 bytes, with only the first part
   filled in
 */

/*
   Cell vector block ­ CELV
   Length -  6012 bytes
 */

struct Dorade_CELV {
    int num_cells;                      /* (8,4) Number of sample volumes */
    float *dist_cells;			/* (12,4) Distance from the radar to
					   cell n in meters */
};

/*
   Cell spacing table ­ CSFD
   Length -  64 bytes
 */

struct Dorade_CSFD {
    int num_segments;                   /* (8,4) Number of segments that contain
					   cells of */
    double dist_to_first;               /* (12,4) Distance to first gate in
					   meters */
    double spacing[8];                  /* (16,4) Width of cells in each segment
					   in m */
    short num_cells[8];                 /* (48,2) Number of cells in each
					   segment.  Equal widths */
};

/*
   Sweep information table ­ SWIB
   Length -  40 bytes
 */

struct Dorade_SWIB {
    char radar_name[9];                 /* (8,1) */
    int sweep_num;                      /* (16,4) Sweep number from the
					   beginning of the volume */
    int num_rays;                       /* (20,4) Number of rays recorded in
					   this sweep */
    double start_angle;                 /* (24,4) True start angle (deg) */
    double stop_angle;                  /* (28,4) True stop angle (deg) */
    double fixed_angle;                 /* (32,4) */
    int filter_flag;                    /* (36,4) */
};

/*
   Platform geo-reference block ­ ASIB
   Length -  80 bytes
 */

struct Dorade_ASIB {
    double longitude;                   /* (8,4) Antenna longitude (Eastern
					   hemisphere is positive, West
					   negative) in degrees */
    double latitude;                    /* (12,4) Antenna latitude (northern
					   hemisphere is positive, south
					   negative) in degrees */
    double altitude_msl;                /* (16,4) Antenna altitude above mean
					   sea level (MSL) in km */
    double altitude_agl;                /* (20,4) Antenna altitude above ground
					   level (AGL) in km */
    double ew_velocity;                 /* (24,4) Antenna east-west ground speed
					   (towards East is positive) in m/sec
					 */
    double ns_velocity;                 /* (28,4) Antenna north-south ground
					   speed (towards North is positive) in
					   m/sec */
    double vert_velocity;               /* (32,4) Antenna vertical velocity in
					   degrees (up is positive) */
    double heading;                     /* (36,4) Antenna heading (angle between
					   rotodome rotational axis and true
					   North, clockwise (looking down
					   positive) in degrees */
    double roll;                        /* (40,4) Roll angle of aircraft tail
					   section (horizontal zero, positive
					   left wing up) in degrees */
    double pitch;                       /* (44,4) Pitch angle of rotodome
					   (horizontal is zero positive front
					   up) in degrees */
    double drift_angle;                 /* (48,4) Antenna drift angle. (angle
					   between platform true velocity and
					   heading, positive is a drift more
					   clockwise looking down) in degrees */
    double rotation_angle;              /* (52,4) Angle of the radar beam with
					   respect to the airframe 9zero is
					   along vertical stabilizer, positive
					   is clockwise) in deg */
    double tilt;                        /* (56,4) Angle of radar beam and line
					   normal to longitudinal axis of
					   aircraft, positive is towards nose of
					   aircraft in degrees */
    double ew_horiz_wind;               /* (60,4) East-west wind velocity at the
					   platform (towards East is positive)
					   in m/sec */
    double ns_horiz_wind;               /* (64,4) North-south wind velocity at
					   the platform (towards North is
					   positive) in m/sec */
    double vert_wind;                   /* (68,4) Vertical wind velocity at the
					   platform (up is positive) in m/sec */
    double heading_change;              /* (72,4) Heading change rate in
					   degrees/second */
    double pitch_change;                /* (76,4) Pitch change rate in
					   degrees/second */
};

/*
   Ray information block ­ RYIB
   Length -  44 bytes
 */

struct Dorade_RYIB {
    int sweep_num;                      /* (8,4) Sweep number for this radar */
    int julian_day;                     /* (12,4) guess */
    int hour;                           /* (16,2) Hour in hours */
    int minute;                         /* (18,2) Minute in minutes */
    int second;                         /* (20,2) Second in seconds */
    int millisecond;                    /* (22,2) Millisecond in milliseconds */
    double azimuth;                     /* (24,4) Azimuth in degrees */
    double elevation;                   /* (28,4) Elevation in degrees */
    double peak_power;                  /* (32,4) Last measured peak transmitted
					   power in kw */
    double true_scan_rate;              /* (36,4) Actual scan rate in
					   degrees/second */
    int ray_status;                     /* (40,4) 0 = normal, 1 = transition,
					   2 = bad */
};

/*
   Header for field data block ­ RDAT
   Length -  16 bytes
 */

struct Dorade_RDAT {
    char pdata_name[9];                 /* (8,1) Name of parameter */
};

/*
   Specify cell geometry type.
   CG_CELV => sweep uses a cell range vector (CELV)
   CG_CSFD => sweep used a cell spacing table (CSFD).
 */

enum Dorade_Cell_Geo {CG_CELV, CG_CSFD};

/*
   Sensor descriptor
 */

struct Dorade_Sensor {
    struct Dorade_RADD radd;
    struct Dorade_PARM parms[DORADE_MAX_PARMS];
    struct Dorade_PARM *parm0;		/* First parameter read */
    enum Dorade_Cell_Geo cell_geo_t;	/* Geometry type in cell_geo member */
    union {
        struct Dorade_CELV celv;
        struct Dorade_CSFD csfd;
    } cell_geo;				/* Cell geometry */
    struct Dorade_CFAC cfac;
};

/*
   Headers and platform info for one ray.  NOT the data.
 */

struct Dorade_Ray_Hdr {
    struct Dorade_RYIB ryib;
    struct Dorade_ASIB asib;
};

/*
   Contents of a sweep file.
 */

struct Dorade_Sweep {
    char *swp_fl_nm;			/* File that provided the sweep */
    struct Dorade_COMM comm;
    struct Dorade_SSWB sswb;
    struct Dorade_VOLD vold;
    struct Dorade_Sensor sensor;	/* Assume only one sensor */
    struct Dorade_SWIB swib;
    struct Dorade_Ray_Hdr *ray_hdr;     /* Dimension [swib.num_rays] */
    float **dat[DORADE_MAX_PARMS];	/* Dimensions [ray][cell] */
    int mod;				/* If true, sweep has been modified in
					   memory since reading. */
};

/*
   Global functions. Defined in dorade_lib.c
 */

void Dorade_COMM_Init(struct Dorade_COMM *);
int Dorade_COMM_Read(struct Dorade_COMM *, char *);
int Dorade_COMM_Write(struct Dorade_COMM *, FILE *);
void Dorade_COMM_Print(struct Dorade_COMM *, FILE *);
void Dorade_SSWB_Init(struct Dorade_SSWB *);
int Dorade_SSWB_Read(struct Dorade_SSWB *, char *);
int Dorade_SSWB_Write(struct Dorade_Sweep *, FILE *);
void Dorade_SSWB_Print(struct Dorade_SSWB *, FILE *);
void Dorade_VOLD_Init(struct Dorade_VOLD *);
int Dorade_VOLD_Read(struct Dorade_VOLD *, char *);
int Dorade_VOLD_Write(struct Dorade_VOLD *, FILE *);
void Dorade_VOLD_Print(struct Dorade_VOLD *, FILE *);
void Dorade_RADD_Init(struct Dorade_RADD *);
int Dorade_RADD_Read(struct Dorade_RADD *, char *);
int Dorade_RADD_Write(struct Dorade_RADD *, FILE *);
void Dorade_RADD_Print(struct Dorade_RADD *, FILE *);
void Dorade_CFAC_Init(struct Dorade_CFAC *);
int Dorade_CFAC_Read(struct Dorade_CFAC *, char *);
int Dorade_CFAC_Write(struct Dorade_CFAC *, FILE *);
void Dorade_CFAC_Print(struct Dorade_CFAC *, FILE *);
void Dorade_PARM_Init(struct Dorade_PARM *);
int Dorade_PARM_Read(struct Dorade_PARM *, char *);
int Dorade_PARM_Write(struct Dorade_PARM *, FILE *);
void Dorade_PARM_Print(struct Dorade_PARM *, int n, FILE *);
void Dorade_CELV_Init(struct Dorade_CELV *);
int Dorade_CELV_Read(struct Dorade_CELV *, char *);
int Dorade_CELV_Write(struct Dorade_CELV *, FILE *);
void Dorade_CELV_Print(struct Dorade_CELV *, FILE *);
void Dorade_CSFD_Init(struct Dorade_CSFD *);
int Dorade_CSFD_Read(struct Dorade_CSFD *, char *);
int Dorade_CSFD_Write(struct Dorade_CSFD *, FILE *);
void Dorade_CSFD_Print(struct Dorade_CSFD *, FILE *);
void Dorade_SWIB_Init(struct Dorade_SWIB *);
int Dorade_SWIB_Read(struct Dorade_SWIB *, char *);
int Dorade_SWIB_Write(struct Dorade_SWIB *, FILE *);
void Dorade_SWIB_Print(struct Dorade_SWIB *, FILE *);
void Dorade_ASIB_Init(struct Dorade_ASIB *);
int Dorade_ASIB_Read(struct Dorade_ASIB *, char *);
int Dorade_ASIB_Write(struct Dorade_ASIB *, FILE *);
void Dorade_ASIB_Print(struct Dorade_ASIB *, int, FILE *);
void Dorade_RYIB_Init(struct Dorade_RYIB *);
int Dorade_RYIB_Read(struct Dorade_RYIB *, char *);
int Dorade_RYIB_Write(struct Dorade_RYIB *, FILE *);
void Dorade_RYIB_Print(struct Dorade_RYIB *, int, FILE *);
void Dorade_Sensor_Init(struct Dorade_Sensor *);
int Dorade_Sensor_Write(struct Dorade_Sweep *, FILE *);
void Dorade_Sensor_Print(struct Dorade_Sweep *, FILE *);
void Dorade_Ray_Hdr_Init(struct Dorade_Ray_Hdr *);
int Dorade_Ray_Hdr_Write(struct Dorade_Ray_Hdr *, FILE *);
void Dorade_Ray_Hdr_Print(struct Dorade_Ray_Hdr *, int, FILE *);
void Dorade_Sweep_Init(struct Dorade_Sweep *);
int Dorade_Sweep_Read(struct Dorade_Sweep *, FILE *);
int Dorade_Sweep_Write(struct Dorade_Sweep *, char *);
int Dorade_NCells(struct Dorade_Sweep *);
void Dorade_CellRng(struct Dorade_Sweep *, float *);
int Dorade_Parm_NewIdx(struct Dorade_Sweep *, char *);
int Dorade_Parm_Idx(struct Dorade_Sweep *, char *);
int Dorade_Parm_Cpy(struct Dorade_Sweep *, char *, char *, char *);
struct Dorade_PARM *Dorade_NextParm(struct Dorade_Sweep *,
	struct Dorade_PARM *);
void Dorade_Data(struct Dorade_Sweep *, float ***);
float **Dorade_ParmData(struct Dorade_Sweep *, char *);
void Dorade_ShiftAz(struct Dorade_Sweep *, double);
void Dorade_ShiftEl(struct Dorade_Sweep *, double);
int Dorade_IncrTime(struct Dorade_Sweep *, double);
int Dorade_Smooth(struct Dorade_Sweep *, int, int);
void Dorade_Sweep_Free(struct Dorade_Sweep *);
float **Dorade_Alloc2F(int, int);
void Dorade_Free2F(float **);

#endif
