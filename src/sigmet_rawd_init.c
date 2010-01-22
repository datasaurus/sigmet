/*
 -	sigmet_rawd.c --
 -		Server that accesses Sigmet raw volumes.
 -		See sigmet_rawd (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.70 $ $Date: 2010/01/21 22:41:14 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include "alloc.h"
#include "str.h"
#include "err_msg.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/* Application and subcommand name */
char *cmd;
char *cmd1;

/* Size for various strings */
#define LEN 1024

/* Subcommands */
#define NCMD 11
char *cmd1v[NCMD] = {
    "cmd_len", "log", "types", "good", "read", "volume_headers", "ray_headers",
    "data", "bin_outline", "bintvls", "stop"
};
typedef int (callback)(int , char **);
callback cmd_len_cb;
callback log_cb;
callback types_cb;
callback good_cb;
callback read_cb;
callback volume_headers_cb;
callback ray_headers_cb;
callback data_cb;
callback bin_outline_cb;
callback bintvls_cb;
callback stop_cb;
callback *cb1v[NCMD] = {
    cmd_len_cb, log_cb, types_cb, good_cb, read_cb, volume_headers_cb,
    ray_headers_cb, data_cb, bin_outline_cb, bintvls_cb, stop_cb
};

char *time_stamp(void);

/* If true, use degrees instead of radians */
int use_deg = 0;

/* The global Sigmet volume, used by most callbacks. */
struct Sigmet_Vol vol;	/* Sigmet volume */
int have_hdr;		/* If true, vol has headers from a Sigmet volume */
int have_vol;		/* If true, vol has headers and data from a Sigmet volume */
char *vol_nm;		/* Name of volume file */
size_t vol_nm_l;	/* Number of characters that can be stored at vol_nm */
void unload(void);	/* Delete and reinitialize contents of vol */

/* Bounds limit indicating all possible index values */
#define ALL -1

/* Files and process streams */
char dir[LEN];		/* Working directory for server */
int i_cmd_in;		/* Stream from the file named in_nm */
int i_cmd_out;		/* Unused outptut stream (see explanation below). */
int idlog;		/* Error log */
char log_nm[LEN];	/* Name of log file */
FILE *dlog;		/* Error log */
FILE *rslt;		/* Send results to client */

/* Input line - has commands for the daemon */
char *buf;
size_t buf_l;		/* Allocation at buf */

/* Shell type determines type of printout */
enum SHELL_TYPE {C, SH};

/* Maximum number of arguments in input command */
#define ARGCX 512

int main(int argc, char *argv[])
{
    enum SHELL_TYPE shtyp;	/* Controls how environment variables are printed */
    char cmd_in_nm[LEN];	/* Space to print file names */
    pid_t pid;			/* Process id for this process */
    struct sigaction schld;	/* Signal action to ignore zombies */
    int i_rslt;			/* File descriptor for rslt stream */
    char *ang_u;		/* Angle unit */
    char *b, *b1;		/* Point into buf, end of buf */

    shtyp = SH;
    if ( (cmd = strrchr(argv[0], '/')) ) {
	cmd++;
    } else {
	cmd = argv[0];
    }
    if ( argc == 1 ) {
	shtyp = SH;
    } else if ( argc == 2 && strcmp(argv[1], "-c") == 0 ) {
	shtyp = C;
    } else {
	fprintf(stderr, "Usage: %s [-c]\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Check for angle unit */
    if ((ang_u = getenv("ANGLE_UNIT")) != NULL) {
	if (strcmp(ang_u, "DEGREE") == 0) {
	    use_deg = 1;
	} else if (strcmp(ang_u, "RADIAN") == 0) {
	    use_deg = 0;
	} else {
	    fprintf(stderr, "%s: Unknown angle unit %s.\n", cmd, ang_u);
	    exit(EXIT_FAILURE);
	}
    }

    /* Initialize globals */
    have_hdr = have_vol = 0;
    Sigmet_InitVol(&vol);
    vol_nm_l = 1;
    if ( !(vol_nm = MALLOC(vol_nm_l)) ) {
	fprintf(stderr, "%s: could not allocate storage for volume file name.\n",
		cmd);
	exit(EXIT_FAILURE);
    }
    *vol_nm = '\0';

    /* Create working directory */
    if ( snprintf(dir, LEN, "%s/.sigmet_raw", getenv("HOME")) >= LEN ) {
	fprintf(stderr, "%s: could not create name for working directory.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (mkdir(dir, 0700) == -1) ) {
	fprintf(stderr, "%s: could not create\n%s\n%s\n",
		cmd, dir, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Create named pipe for command input */
    if ( snprintf(cmd_in_nm, LEN, "%s/%s", dir, "sigmet.in") >= LEN ) {
	fprintf(stderr, "%s: could not create name for server input pipe.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (mkfifo(cmd_in_nm, 0600) == -1) ) {
	fprintf(stderr, "%s: sigmet_rawd could not create input pipe.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Allocate input line */
    if ( (buf_l = fpathconf(i_cmd_in, _PC_PIPE_BUF)) == -1 ) {
	fprintf(stderr, "%s: Could not get pipe buffer size.\n%s\n",
		cmd, strerror(errno));
	exit(EXIT_FAILURE);
    }
    if ( !(buf = CALLOC(buf_l, 1)) ) {
	fprintf(stderr, "%s: Could not allocate input buffer.\n", cmd);
	exit(EXIT_FAILURE);
    }
    b1 = buf + buf_l;

    /* Open log file */
    if ( snprintf(log_nm, LEN, "%s/%s", dir, "sigmet.log") >= LEN ) {
	fprintf(stderr, "%s: could not create name for log file.\n", cmd);
	exit(EXIT_FAILURE);
    }
    if ( (idlog = open(log_nm, O_CREAT | O_TRUNC | O_WRONLY, 0600)) == -1
	    || !(dlog = fdopen(idlog, "w")) ) {
	fprintf(stderr, "%s: could not create log file.\n", cmd);
	exit(EXIT_FAILURE);
    }

    /* Print information about input streams and put server in background */
    switch (pid = fork()) {
	case -1:
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not spawn gzip process.  ");
	    return 0;
	case 0:
	    /* Child. Run the server. */
	    break;
	default:
	    /* Parent. Print information and exit. Server will go to background. */
	    if (shtyp == SH) {
		printf("SIGMET_RAWD_PID=%d; export SIGMET_RAWD_PID;\n", pid);
		printf("SIGMET_RAWD_DIR=%s; export SIGMET_RAWD_DIR;\n", dir);
		printf("SIGMET_RAWD_IN=%s; export SIGMET_RAWD_IN;\n", cmd_in_nm);
	    } else {
		printf("setenv SIGMET_RAWD_PID %d;\n", pid);
		printf("setenv SIGMET_RAWD_DIR %s;\n", dir);
		printf("setenv SIGMET_RAWD_IN=%s;\n", cmd_in_nm);
	    }
	    printf("echo Starting sigmet_rawd. Process id = %d.;\n", pid);
	    printf("echo Working directory = %s;\n", dir);
	    printf("echo Log file = %s;\n", log_nm);
	    exit(EXIT_SUCCESS);
    }
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);

    fprintf(dlog, "%s: sigmet_rawd started. pid=%d\n", time_stamp(), getpid());
    fflush(dlog);

    /* Catch signals from children to prevent zombies */
    schld.sa_handler = SIG_DFL;
    if ( sigfillset(&schld.sa_mask) == -1) {
	fprintf(dlog, "%s: Could not initialize signal mask\n%s\n",
		time_stamp(), strerror(errno));
	exit(EXIT_FAILURE);
    }
    schld.sa_flags = SA_NOCLDWAIT;
    if ( sigaction(SIGCHLD, &schld, 0) == -1 ) {
	fprintf(dlog, "%s: Could not set up signals for piping\n%s\n",
		time_stamp(), strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* Open command input stream.  It will block until a client writes something. */
    if ( (i_cmd_in = open(cmd_in_nm, O_RDONLY, 0600)) == -1 ) {
	fprintf(stderr, "%s: Could not open %s for input.\n%s\n",
		cmd, cmd_in_nm, strerror(errno));
	exit(EXIT_FAILURE);
    }
    i_cmd_out = -2;

    /*
       Read command from i_cmd_in into buf.
       Contents of buf: argc argv rslt_nm
       argc transferred as binary integer. argv members and rslt_nm nul separated.
     */
    while ( read(i_cmd_in, buf, buf_l) == buf_l ) {
	int argc1;		/* Number of arguments in an input line */
	char *argv1[ARGCX];	/* Arguments from an input line */
	int a;			/* Index into argv1 */
	char *rslt_fl;		/* Where to send output */
	int i;			/* Loop index */

	/*
	   Daemon now has something to read. Open command pipe for output so
	   daemon keeps running while waiting for more clients.
	 */
	if ( i_cmd_out == -2 && (i_cmd_out = open(cmd_in_nm, O_WRONLY)) == -1 ) {
	    fprintf(stderr, "%s: Could not open %s for output.\n%s\n",
		    cmd, cmd_in_nm, strerror(errno));
	    exit(EXIT_FAILURE);
	}

	/* Break input line into arguments */
	argc1 = *(int *)buf;
	if (argc1 > ARGCX) {
	    fprintf(dlog, "%s: Unable to parse command with %d arguments. "
		    "Limit is %d\n", time_stamp(), argc1, ARGCX);
	    continue;
	}
	buf += sizeof(int);
	for (a = 0, argv1[a] = b = buf; b < b1 && a < argc1; b++) {
	    if ( *b == '\0' ) {
		argv1[++a] = b + 1;
	    }
	}
	if ( b == b1 ) {
	    fprintf(dlog, "%s: Command line gives no destination.\n", time_stamp());
	    continue;
	}
	rslt_fl = b;

	/* First argument tells where to send output.  Open and block. */
	rslt = NULL;
	if (       access(rslt_fl, W_OK) == -1
		|| (i_rslt = open(rslt_fl, O_WRONLY)) == -1
		|| !(rslt = fdopen(i_rslt, "w")) ) {
	    fprintf(dlog, "%s: Could not open %s for output.\n%s\n", time_stamp(),
		    rslt_fl, strerror(errno));
	    continue;
	}

	/* Execute command on rest of command line */
	cmd1 = argv1[0];
	if ( (i = Sigmet_RawCmd(cmd1)) == -1) {
	    fprintf(rslt, "No option or subcommand named \"%s\"\n",
		    cmd1);
	    fprintf(rslt, "Subcommand must be one of: ");
	    for (i = 0; i < NCMD; i++) {
		fprintf(rslt, "%s ", cmd1v[i]);
	    }
	    fprintf(rslt, "\n");
	} else if ( !(cb1v[i])(argc1, argv1) ) {
	    fprintf(rslt, "%s: %s failed.\n%s\n", cmd, cmd1, Err_Get());
	    fprintf(dlog, "%s: %s failed.\n%s\n", time_stamp(), cmd1, Err_Get());
	}
	if ( rslt && (fclose(rslt) == EOF) ) {
	    fprintf(dlog, "%s: Could not close %s.\n%s\n", time_stamp(),
		    rslt_fl, strerror(errno));
	    continue;
	}
    }
    fprintf(dlog, "%s: exiting. No more input.\n", time_stamp());
    if ( close(i_cmd_in) == -1 ) {
	fprintf(dlog, "%s: could not close command stream.\n%s\n",
		time_stamp(), strerror(errno));
    }
    fclose(dlog);
    unload();
    FREE(vol_nm);
    FREE(buf);

    return 0;
}

void unload(void)
{
    Sigmet_FreeVol(&vol);
    have_hdr = have_vol = 0;
    vol_nm[0] = '\0';
}

/* Get a character string with the current time */
char *time_stamp(void)
{
    static char ts[LEN];
    time_t now;

    now = time(NULL);
    if (strftime(ts, LEN, "%Y/%m/%d %H:%M:%S %Z", localtime(&now))) {
	return ts;
    } else {
	return "";
    }
}

int cmd_len_cb(int argc, char *argv[])
{
    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    fprintf(rslt, "%ld\n", buf_l);
    return 1;
}

int log_cb(int argc, char *argv[])
{
    FILE *l;
    int c;

    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    if ( !(l = fopen(log_nm, "r")) ) {
	Err_Append("Could not open log file.\n");
	return 0;
    }
    while ( (c = fgetc(l)) != EOF && fputc(c, rslt) != EOF ) {
	continue;
    }
    if ( ferror(l) || ferror(rslt) ) {
	Err_Append("Error writing log file.  ");
	Err_Append(strerror(errno));
    }
    if ( fclose(l) == EOF ) {
	Err_Append("Could not close log file.\n");
	return 0;
    }
    return 1;
}

int types_cb(int argc, char *argv[])
{
    int y;

    if (argc != 1) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	return 0;
    }
    for (y = 0; y < SIGMET_NTYPES; y++) {
	fprintf(rslt, "%s | %s\n",
		Sigmet_DataType_Abbrv(y), Sigmet_DataType_Descr(y));
    }
    return 1;
}

int good_cb(int argc, char *argv[])
{
    char *in_nm;
    FILE *in;

    if (argc == 1) {
	in_nm = "-";
    } else if (argc == 2) {
	in_nm = argv[1];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [sigmet_volume]");
	return 0;
    }
    if ( !(in = fopen(in_nm, "r")) ) {
	Err_Append("Could not open ");
	Err_Append(in_nm);
	Err_Append(" for input.\n");
	return 0;
    }
    if ( !Sigmet_GoodVol(in) ) {
	fclose(in);
	/* Skip output messages */
	exit(1);
    }
    fclose(in);
    return 1;
}

/*
   Callback for the "read" command.
   Read a volume into memory.
   Usage --
   read raw_file
   read -h raw_file
 */
int read_cb(int argc, char *argv[])
{
    int hdr_only;
    char *in_nm;
    FILE *in;
    char *t;
    size_t l;
    pid_t pid = 0;
    char *sfx;		/* Filename suffix */
    int pfd[2];		/* Pipe for data */

    hdr_only = 0;
    if ( argc == 2 ) {
	in_nm = argv[1];
    } else if ( argc == 3 && (strcmp(argv[1], "-h") == 0) ) {
	hdr_only = 1;
	in_nm = argv[2];
    } else {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [-h] sigmet_volume");
	return 0;
    }

    sfx = strrchr(in_nm , '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/* If filename ends with ".gz", read from gunzip pipe */
	if ( pipe(pfd) == -1 ) {
	    Err_Append(strerror(errno));
	    Err_Append("\nCould not create pipe for gzip.  ");
	    return 0;
	}
	switch (pid = fork()) {
	    case -1:
		Err_Append(strerror(errno));
		Err_Append("\nCould not spawn gzip process.  ");
		return 0;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( close(i_cmd_in) == -1 || close(i_cmd_out) == -1
			|| fclose(rslt) == EOF) {
		    fprintf(dlog, "%s: gzip child could not close"
			    " server streams", time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1
			|| close(pfd[1]) == -1 ) {
		    fprintf(dlog, "%s: could not set up gzip process",
			    time_stamp());
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(idlog, STDERR_FILENO) == -1 || close(idlog) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", in_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append(strerror(errno));
		    Err_Append("\nCould not read gzip process.  ");
		    return 0;
		}
	}
    } else if ( !(in = fopen(in_nm, "r")) ) {
	/* Uncompressed file */
	Err_Append("Could not open ");
	Err_Append(in_nm);
	Err_Append(" for input.\n");
	return 0;
    }
    if (hdr_only) {
	if( have_hdr && (strcmp(in_nm, vol_nm) == 0) ) {
	    /* No need to read headers */
	    fclose(in);
	    return 1;
	}
	/* Read headers */
	unload();
	if ( !Sigmet_ReadHdr(in, &vol) ) {
	    Err_Append("Could not read headers from ");
	    Err_Append(in_nm);
	    Err_Append(".\n");
	    fclose(in);
	    return 0;
	}
	have_hdr = 1;
	have_vol = 0;
    } else {
	if ( have_vol && !vol.truncated && (strcmp(in_nm, vol_nm) == 0) ) {
	    /* No need to read volume */
	    fclose(in);
	    return 1;
	}
	/* Read volume */
	unload();
	if ( !Sigmet_ReadVol(in, &vol) ) {
	    Err_Append("Could not read volume from ");
	    Err_Append(in_nm);
	    Err_Append(".\n");
	    fclose(in);
	    return 0;
	}
	have_hdr = have_vol = 1;
    }
    fclose(in);
    l = 0;
    if ( !(t = Str_Append(vol_nm, &l, &vol_nm_l, in_nm, strlen(in_nm))) ) {
	Err_Append("Could not store name of global volume.  ");
	unload();
	return 0;
    }
    vol_nm = t;
    return 1;
}

int volume_headers_cb(int argc, char *argv[])
{
    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    Sigmet_PrintHdr(rslt, vol);
    return 1;
}

int ray_headers_cb(int argc, char *argv[])
{
    int s, r;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    for (s = 0; s < vol.ih.tc.tni.num_sweeps; s++) {
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    int yr, mon, da, hr, min;
	    double sec;

	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(rslt, "sweep %3d ray %4d | ", s, r);
	    if ( !Tm_JulToCal(vol.ray_time[s][r],
			&yr, &mon, &da, &hr, &min, &sec) ) {
		Err_Append("Bad ray time.  ");
		return 0;
	    }
	    fprintf(rslt, "%04d/%02d/%02d %02d:%02d:%04.1f | ",
		    yr, mon, da, hr, min, sec);
	    fprintf(rslt, "az %7.3f %7.3f | ",
		    vol.ray_az0[s][r] * DEG_PER_RAD,
		    vol.ray_az1[s][r] * DEG_PER_RAD);
	    fprintf(rslt, "tilt %6.3f %6.3f\n",
		    vol.ray_tilt0[s][r] * DEG_PER_RAD,
		    vol.ray_tilt1[s][r] * DEG_PER_RAD);
	}
    }
    return 1;
}

int data_cb(int argc, char *argv[])
{
    int s, y, r, b;
    char *abbrv;
    float d;
    enum Sigmet_DataType type;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }

    /*
       Identify input and desired output
       Possible forms:
	   data			(argc = 1)
	   data y		(argc = 2)
	   data y s		(argc = 3)
	   data y s r		(argc = 4)
	   data y s r b		(argc = 5)
     */

    y = s = r = b = ALL;
    type = DB_ERROR;
    if (argc > 1 && (type = Sigmet_DataType(argv[1])) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(argv[1]);
	Err_Append(".  ");
	return 0;
    }
    if (argc > 2 && sscanf(argv[2], "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (argc > 3 && sscanf(argv[3], "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (argc > 4 && sscanf(argv[4], "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (argc > 5) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" [type] [sweep] [ray] [bin]");
	return 0;
    }

    if (type != DB_ERROR) {
	/*
	   User has specified a data type.  Search for it in the volume,
	   and set y to the specified type (instead of ALL).
	 */
	abbrv = Sigmet_DataType_Abbrv(type);
	for (y = 0; y < vol.num_types; y++) {
	    if (type == vol.types[y]) {
		break;
	    }
	}
	if (y == vol.num_types) {
	    Err_Append("Data type ");
	    Err_Append(abbrv);
	    Err_Append(" not in volume.\n");
	    return 0;
	}
    }
    if (s != ALL && s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r != ALL && r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b != ALL && b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }

    /* Write */
    if (y == ALL && s == ALL && r == ALL && b == ALL) {
	for (y = 0; y < vol.num_types; y++) {
	    type = vol.types[y];
	    abbrv = Sigmet_DataType_Abbrv(type);
	    for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
		fprintf(rslt, "%s. sweep %d\n", abbrv, s);
		for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		    fprintf(rslt, "ray %d: ", r);
		    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
			d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
			if (Sigmet_IsData(d)) {
			    fprintf(rslt, "%f ", d);
			} else {
			    fprintf(rslt, "nodat ");
			}
		    }
		    fprintf(rslt, "\n");
		}
	    }
	}
    } else if (s == ALL && r == ALL && b == ALL) {
	for (s = 0; s < vol.ih.ic.num_sweeps; s++) {
	    fprintf(rslt, "%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol.ih.ic.num_rays; r++) {
		    if ( !vol.ray_ok[s][r] ) {
			continue;
		    }
		fprintf(rslt, "ray %d: ", r);
		for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		    if (Sigmet_IsData(d)) {
			fprintf(rslt, "%f ", d);
		    } else {
			fprintf(rslt, "nodat ");
		    }
		}
		fprintf(rslt, "\n");
	    }
	}
    } else if (r == ALL && b == ALL) {
	fprintf(rslt, "%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol.ih.ic.num_rays; r++) {
	    if ( !vol.ray_ok[s][r] ) {
		continue;
	    }
	    fprintf(rslt, "ray %d: ", r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(rslt, "%f ", d);
		} else {
		    fprintf(rslt, "nodat ");
		}
	    }
	    fprintf(rslt, "\n");
	}
    } else if (b == ALL) {
	if (vol.ray_ok[s][r]) {
	    fprintf(rslt, "%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
		d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
		if (Sigmet_IsData(d)) {
		    fprintf(rslt, "%f ", d);
		} else {
		    fprintf(rslt, "nodat ");
		}
	    }
	    fprintf(rslt, "\n");
	}
    } else {
	if (vol.ray_ok[s][r]) {
	    fprintf(rslt, "%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_DataType_ItoF(type, vol, vol.dat[y][s][r][b]);
	    if (Sigmet_IsData(d)) {
		fprintf(rslt, "%f ", d);
	    } else {
		fprintf(rslt, "nodat ");
	    }
	    fprintf(rslt, "\n");
	}
    }
    return 1;
}

int bin_outline_cb(int argc, char *argv[])
{
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];
    double c;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" sweep ray bin");
	return 0;
    }
    s_s = argv[1];
    r_s = argv[2];
    b_s = argv[3];

    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }
    if (sscanf(r_s, "%d", &r) != 1) {
	Err_Append("Ray index must be an integer.  ");
	return 0;
    }
    if (sscanf(b_s, "%d", &b) != 1) {
	Err_Append("Bin index must be an integer.  ");
	return 0;
    }
    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if (r >= vol.ih.ic.num_rays) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if (b >= vol.ih.tc.tri.num_bins_out) {
	Err_Append("Ray index greater than number of rays.  ");
	return 0;
    }
    if ( !Sigmet_BinOutl(&vol, s, r, b, corners) ) {
	Err_Append("Could not compute bin outlines.  ");
	return 0;
    }
    Sigmet_FreeVol(&vol);
    c = (use_deg ? DEG_RAD : 1.0);
    fprintf(rslt, "%f %f %f %f %f %f %f %f\n",
	    corners[0] * c, corners[1] * c, corners[2] * c, corners[3] * c,
	    corners[4] * c, corners[5] * c, corners[6] * c, corners[7] * c);

    return 1;
}

/* Usage: sigmet_raw bintvls type s bounds raw_vol */
int bintvls_cb(int argc, char *argv[])
{
    char *s_s;
    int s, y, r, b;
    char *abbrv;
    double d;
    enum Sigmet_DataType type_t;

    if ( !have_vol ) {
	Err_Append("No volume loaded.  ");
	return 0;
    }
    if (argc != 4) {
	Err_Append("Usage: ");
	Err_Append(cmd1);
	Err_Append(" type sweep bounds");
	return 0;
    }
    abbrv = argv[1];
    if ((type_t = Sigmet_DataType(abbrv)) == DB_ERROR) {
	Err_Append("No data type named ");
	Err_Append(abbrv);
	Err_Append(".  ");
    }
    s_s = argv[2];
    if (sscanf(s_s, "%d", &s) != 1) {
	Err_Append("Sweep index must be an integer.  ");
	return 0;
    }

    for (y = 0; y < vol.num_types; y++) {
	if (type_t == vol.types[y]) {
	    break;
	}
    }
    if (y == vol.num_types) {
	Err_Append("Data type ");
	Err_Append(abbrv);
	Err_Append(" not in volume.\n");
	return 0;
    }

    if (s >= vol.ih.ic.num_sweeps) {
	Err_Append("Sweep index greater than number of sweeps.  ");
	return 0;
    }
    if ( !vol.sweep_ok[s] ) {
	Err_Append("Sweep not valid in this volume.  ");
	return 0;
    }

    for (r = 0; r < vol.ih.ic.num_rays; r++) {
	if ( !vol.ray_ok[s][r] ) {
	    continue;
	}
	for (b = 0; b < vol.ray_num_bins[s][r]; b++) {
	    d = Sigmet_DataType_ItoF(type_t, vol, vol.dat[y][s][r][b]);
	}
	fprintf(rslt, "\n");
    }

    return 1;
}

int stop_cb(int argc, char *argv[])
{
    char rm[LEN];

    fclose(dlog);
    unload();
    FREE(vol_nm);
    FREE(buf);
    if (snprintf(rm, LEN, "rm -r %s", dir) < LEN) {
	system(rm);
    } else {
	fprintf(dlog, "%s: could not delete working directory.\n", time_stamp());
    }
    fprintf(rslt, "unset SIGMET_RAWD_PID SIGMET_RAWD_DIR SIGMET_RAWD_IN");
    exit(EXIT_SUCCESS);
    return 0;
}
