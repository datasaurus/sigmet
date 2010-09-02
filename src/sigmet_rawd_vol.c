/*
 -	sigmet_raw_vol.c --
 -		This file defines a database with data from Sigmet
 -		raw volumes, and functions that provide this data to
 -		the sigmet_rawd daemon.
 -
 .	Copyright (c) 2010 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: 1.10 $ $Date: 2010/09/01 21:55:12 $
 */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "err_msg.h"
#include "sigmet.h"
#include "sigmet_raw.h"

#define LEN 1024

/* A Sigmet volume struct and data to manage it. */
struct sig_vol {
    struct Sigmet_Vol vol;		/* Sigmet volume struct. See sigmet.h */
    int in_use;				/* True => this entry is associated with
					   a volume */
    char vol_nm[LEN];			/* file that provided the volume */
    dev_t st_dev;			/* Device that provided vol */
    ino_t st_ino;			/* I-number of file that provided vol */
    int users;				/* Number of client sessions using vol */
};

/* Data base of Sigmet volumes for the application */
#define N_VOLS 256
static struct sig_vol vols[N_VOLS];

/* Local functions and variables */
static int file_id(char *, dev_t *, ino_t *);
static int hash(dev_t, ino_t);
static struct sig_vol *new_vol(dev_t, ino_t);
static struct sig_vol *get_vol(dev_t, ino_t);
static FILE *vol_open(const char *, pid_t *, int, FILE *);
static void unload(struct sig_vol *);

/* Initialize this interface */
void SigmetRaw_VolInit(void)
{
    struct sig_vol *sv_p;
    static int init;

    if ( init ) {
	return;
    }
    assert(N_VOLS == 256);
    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	Sigmet_InitVol((struct Sigmet_Vol *)sv_p);
	sv_p->in_use = 0;
	memset(sv_p->vol_nm, 0, LEN);
	sv_p->st_dev = 0;
	sv_p->st_ino = 0;
	sv_p->users = 0;
    }
    init = 1;
}

/* Free memory and reinitialize this interface */
void SigmetRaw_VolFree(void)
{
    struct sig_vol *sv_p;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	Sigmet_FreeVol((struct Sigmet_Vol *)sv_p);
	sv_p->in_use = 0;
	memset(sv_p->vol_nm, 0, LEN);
	sv_p->st_dev = 0;
	sv_p->st_ino = 0;
	sv_p->users = 0;
    }
}

/*
   Return true if vol_nm is a good (navigable) Sigmet volume.
   Print error messages to err (in this application) and i_err (in child process).
 */
int SigmetRaw_GoodVol(char *vol_nm, int i_err, FILE *err)
{
    dev_t st_dev;
    ino_t st_ino;
    struct sig_vol *sv_p;
    FILE *in;
    int rslt;
    pid_t p;

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return 0;
    }

    /* If volume is already loaded and not truncated, it is good */
    if ( (sv_p = get_vol(st_dev, st_ino)) && !sv_p->vol.truncated ) {
	return 1;
    }

    /* Volume not loaded. Inspect vol_nm with Sigmet_GoodVol. */
    if ( !(in = vol_open(vol_nm, &p, i_err, err)) ) {
	fprintf(err, "Could not open %s\n", vol_nm);
	return 0;
    }
    rslt = Sigmet_GoodVol(in);
    fclose(in);
    if ( p != -1 ) {
	waitpid(p, NULL, 0);
    }

    return rslt;
}

/*
   Fetch a volume with headers from the data base, loading it if necessary.
   If function loads it, only load headers.
   User count for the volume's entry is incremented. Send error messages to
   err or i_err.
 */
enum Sigmet_CB_Return SigmetRaw_ReadHdr(char *vol_nm, FILE *err, int i_err,
	struct Sigmet_Vol ** vol_pp)
{
    dev_t st_dev;		/* Device where vol_nm resides */
    ino_t st_ino;		/* File system index number for vol_nm */
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    enum Sigmet_ReadStatus status; /* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return SIGMET_CB_INPUT_FAIL;
    }

    /*
       Find or make entry for the volume in vols. If entry already exists and
       has headers, return.
     */
    if ( (sv_p = get_vol(st_dev, st_ino)) ) {
	vol_p = (struct Sigmet_Vol *)sv_p;
	if ( vol_p->has_headers ) {
	    sv_p->users++;
	    *vol_pp = vol_p;
	    return SIGMET_CB_SUCCESS;
	}
    } else if ( !(sv_p = new_vol(st_dev, st_ino)) ) {
	fprintf(err, "Volume table full. Could not (re)load %s\n", vol_nm);
	return SIGMET_CB_FAIL;
    }
    sv_p->users++;
    vol_p = (struct Sigmet_Vol *)sv_p;

    /* Try to read volume headers */
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err, err)) ) {
	    fprintf(err, "Could not open %s for input.\n", vol_nm);
	    unload(sv_p);
	    return SIGMET_CB_INPUT_FAIL;
	}
	switch (status = Sigmet_ReadHdr(in, vol_p)) {
	    case SIGMET_VOL_READ_OK:
		/* Success. Break out. */
		loaded = 1;
		break;
	    case SIGMET_VOL_MEM_FAIL:
		/* Try to free some memory and try again */
		fprintf(err, "Out of memory. Offloading unused volumes\n");
		SigmetRaw_Flush();
		break;
	    case SIGMET_VOL_INPUT_FAIL:
	    case SIGMET_VOL_BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fprintf(err, "%s\n", Err_Get());
		try = max_try;
		break;
	}
	while (fgetc(in) != EOF) {
	    continue;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    if ( !loaded ) {
	fprintf(err, "Could not read %s\n", vol_nm);
	sv_p->users--;
	unload(sv_p);
	switch (status) {
	    case SIGMET_VOL_READ_OK:
		/* Suppress compiler warning */
		break;
	    case SIGMET_VOL_MEM_FAIL:
		return SIGMET_CB_MEM_FAIL;
		break;
	    case SIGMET_VOL_INPUT_FAIL:
		return SIGMET_CB_INPUT_FAIL;
		break;
	    case SIGMET_VOL_BAD_VOL:
		return SIGMET_CB_FAIL;
		break;
	}
    }
    strncpy(sv_p->vol_nm, vol_nm, LEN);
    *vol_pp = vol_p;
    return SIGMET_CB_SUCCESS;
}

/*
   Fetch a volume from the data base, loading it if necessary.
   User count for the volume's entry is incremented. Send error messages to
   err or i_err.
 */
enum Sigmet_CB_Return SigmetRaw_ReadVol(char *vol_nm, FILE *err, int i_err,
	struct Sigmet_Vol **vol_pp)
{
    dev_t st_dev;		/* Device where vol_nm resides */
    ino_t st_ino;		/* File system index number for vol_nm */
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    enum Sigmet_ReadStatus status; /* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return SIGMET_CB_INPUT_FAIL;
    }

    /*
       Find or make entry for the volume in vols. If entry already exists and
       has data, return.
     */
    if ( (sv_p = get_vol(st_dev, st_ino)) ) {
	vol_p = (struct Sigmet_Vol *)sv_p;
	if ( !vol_p->truncated ) {
	    sv_p->users++;
	    *vol_pp = vol_p;
	    return SIGMET_CB_SUCCESS;
	}
    } else if ( !(sv_p = new_vol(st_dev, st_ino)) ) {
	fprintf(err, "Volume table full. Could not (re)load %s\n", vol_nm);
	return SIGMET_CB_FAIL;
    }
    vol_p = (struct Sigmet_Vol *)sv_p;
    sv_p->users++;

    /* Read volume */
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err, err)) ) {
	    fprintf(err, "Could not open %s for input.\n", vol_nm);
	    unload(sv_p);
	    return SIGMET_CB_INPUT_FAIL;
	}
	switch (Sigmet_ReadVol(in, vol_p)) {
	    case SIGMET_VOL_READ_OK:
	    case SIGMET_VOL_INPUT_FAIL:
		/* Success or partial success. Break out. */
		loaded = 1;
		break;
	    case SIGMET_VOL_MEM_FAIL:
		/* Try to free some memory and try again */
		fprintf(err, "Read failed. Out of memory. %s "
			"Offloading unused volumes\n", Err_Get());
		SigmetRaw_Flush();
		break;
	    case SIGMET_VOL_BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fprintf(err, "Read failed, bad volume. %s\n", Err_Get());
		try = max_try;
		break;
	}
	while (fgetc(in) != EOF) {
	    continue;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    if ( !loaded ) {
	fprintf(err, "Could not read %s\n", vol_nm);
	sv_p->users--;
	unload(sv_p);
	switch (status) {
	    case SIGMET_VOL_READ_OK:
		/* Suppress compiler warning */
		break;
	    case SIGMET_VOL_MEM_FAIL:
		return SIGMET_CB_MEM_FAIL;
		break;
	    case SIGMET_VOL_INPUT_FAIL:
		return SIGMET_CB_INPUT_FAIL;
		break;
	    case SIGMET_VOL_BAD_VOL:
		return SIGMET_CB_FAIL;
		break;
	}
    }
    strncpy(sv_p->vol_nm, vol_nm, LEN);
    *vol_pp = vol_p;
    return SIGMET_CB_SUCCESS;
}

/* Fetch a volume from the data base.  Send error messages to err or i_err. */
enum Sigmet_CB_Return SigmetRaw_GetVol(char *vol_nm, FILE *err, int i_err,
	struct Sigmet_Vol **vol_pp)
{
    dev_t st_dev;		/* Device where vol_nm resides */
    ino_t st_ino;		/* File system index number for vol_nm */
    struct sig_vol *sv_p;	/* Member of vols */

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return SIGMET_CB_INPUT_FAIL;
    }
    if ( !(sv_p = get_vol(st_dev, st_ino)) || !sv_p->vol.has_headers) {
	fprintf(err, "%s not loaded. Please load with read command.\n", vol_nm);
	return SIGMET_CB_FAIL;
    }
    *vol_pp = (struct Sigmet_Vol *)sv_p;
    return SIGMET_CB_SUCCESS;
}

/* Print list of currently loaded volumes. */
void SigmetRaw_VolList(FILE *out)
{
    struct sig_vol *sv_p;
    struct Sigmet_Vol *vol_p;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	vol_p = (struct Sigmet_Vol *)sv_p;
	if ( sv_p->in_use ) {
	    /* File name. Number of users. Number of sweeps. */
	    fprintf(out, "%s users=%d. sweeps=%d.\n",
		    sv_p->vol_nm, sv_p->users, vol_p->num_sweeps_ax);
	}
    }
}

/*
   Clients use this function to indicate a volume is no longer needed.
   It decrements the volume's user count. If user count goes to zero,
   the volume is a candidate for deletion.
 */
enum Sigmet_CB_Return SigmetRaw_Release(char *vol_nm, FILE *err)
{
    dev_t st_dev;
    ino_t st_ino;
    struct sig_vol *sv_p;

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return SIGMET_CB_INPUT_FAIL;
    }
    if ( (sv_p = get_vol(st_dev, st_ino)) && sv_p->users > 0 ) {
	sv_p->users--;
    }
    return SIGMET_CB_SUCCESS;
}

/* Remove unused volumes */
void SigmetRaw_Flush(void)
{
    struct sig_vol *sv_p;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	if ( sv_p->in_use ) {
	    unload(sv_p);
	}
    }
}

/*
   Fetch device id and inode for the file named f, and store at d_p and i_p.
   Return true if successful, o/w false.
 */
static int file_id(char *vol_nm, dev_t *d_p, ino_t *i_p)
{
    struct stat sbuf;

    if ( stat(vol_nm, &sbuf) == -1 ) {
	return 0;
    }
    *d_p = sbuf.st_dev;
    *i_p = sbuf.st_ino;
    return 1;
}

/* Create an integer hash for file with index number st_ino on device st_dev. */
static int hash(dev_t st_dev, ino_t st_ino)
{
    return ((st_dev & 0x0f) << 4) | (st_ino & 0x0f);
}

/*
   Find or create an entry in vols for the file with index number st_ino on
   device st_dev.  Return the entry, or NULL if the file is not in vols and
   cannot be added.
 */
static struct sig_vol *new_vol(dev_t st_dev, ino_t st_ino)
{
    int h, i;			/* Index into vols */

    h = hash(st_dev, st_ino);

    /*
       Hash is not necessarily the index for the volume in vols array.
       Walk the array until we actually reach the entry for the file.
     */
    for (i = h; (i + 1) % N_VOLS != h; i = (i + 1) % N_VOLS) {
	if ( vols[i].in_use
		&& vols[i].st_dev == st_dev
		&& vols[i].st_ino == st_ino) {
	    return vols + i;
	}
    }

    /*
       There is no entry for the file. Try to create one.
     */
    for (i = h; (i + 1) % N_VOLS != h; i = (i + 1) % N_VOLS) {
	if ( !vols[i].in_use ) {
	    vols[i].in_use = 1;
	    vols[i].st_dev = st_dev;
	    vols[i].st_ino = st_ino;
	    return vols + i;
	}
    }

    return NULL;
}

/*
   Find an entry in vols for the file with index number st_ino on device st_dev.
   Return the index, or -1 if the file is not in vols.
 */
static struct sig_vol *get_vol(dev_t st_dev, ino_t st_ino)
{
    int h, i;			/* Index into vols */

    h = hash(st_dev, st_ino);

    /*
       Hash is not necessarily the index for the volume in vols array.
       Walk the array until we actually reach the entry for the file.
     */
    for (i = h; (i + 1) % N_VOLS != h; i = (i + 1) % N_VOLS) {
	if ( vols[i].in_use
		&& vols[i].st_dev == st_dev
		&& vols[i].st_ino == st_ino) {
	    return vols + i;
	}
    }
    return NULL;
}

/*
   Open volume file vol_nm.  If vol_nm suffix suggests a compressed file, open a
   pipe to a decompression process.
   Return file handle, or NULL if failure. Put id for decompress process at pid_p,
   or -1 if no such process (i.e. vol_nm is a plain file).
   Send error messages to err (in this function) or i_err (in child).
 */
static FILE *vol_open(const char *vol_nm, pid_t *pid_p, int i_err, FILE *err)
{
    FILE *in = NULL;		/* Return value */
    char *sfx;			/* Filename suffix */
    int pfd[2] = {-1};		/* Pipe for data */
    pid_t pid = getpid();	/* This process */
    pid_t ch_pid = -1;		/* Child process id */

    *pid_p = -1;
    sfx = strrchr(vol_nm, '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/* If filename ends with ".gz", read from gunzip pipe */
	if ( pipe(pfd) == -1 ) {
	    fprintf(err, "Could not create pipe for gzip\n%s\n", strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(err, "Could not spawn gzip\n");
		goto error;
	    case 0:
		/* Child process - gzip.  Send child stdout to pipe. */
		if ( setpgid(0, pid) == -1 ) {
		    fprintf(stderr, "Could not create process group.\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(err, "gzip process failed\n%s\n", strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    fprintf(err, "gzip process could not access error stream\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(err, "Could not read gzip process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/* If filename ends with ".bz2", read from bunzip2 pipe */
	if ( pipe(pfd) == -1 ) {
	    fprintf(err, "Could not create pipe for bzip2\n%s\n", strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(err, "Could not spawn bzip2\n");
		goto error;
	    case 0:
		/* Child process - bzip2.  Send child stdout to pipe. */
		if ( setpgid(0, pid) == -1 ) {
		    fprintf(stderr, "Could not create process group.\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1 ) {
		    fprintf(err, "could not set up bzip2 process");
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    fprintf(err, "bzip2 process could not access error "
			    "stream\n%s\n", strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/* This process.  Read output from bzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(err, "Could not read bzip2 process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	/* Uncompressed file */
	fprintf(err, "Could not open %s\n%s\n", vol_nm, strerror(errno));
	return NULL;
    }
    return in;

error:
	if ( ch_pid != -1 ) {
	    kill(ch_pid, SIGTERM);
	    ch_pid = -1;
	}
	if ( in ) {
	    fclose(in);
	    pfd[0] = -1;
	}
	if ( pfd[0] != -1 ) {
	    close(pfd[0]);
	}
	if ( pfd[1] != -1 ) {
	    close(pfd[1]);
	}
	return NULL;
}

/* Try to unload volume */
static void unload(struct sig_vol *sv_p)
{
    struct Sigmet_Vol *vol_p = (struct Sigmet_Vol *)sv_p;

    if ( !sv_p->in_use ) {
	return;
    }
    if ( sv_p->users > 0 ) {
	return;
    }
    if ( vol_p->has_headers ) {
	Sigmet_FreeVol(vol_p);
    }
    sv_p->in_use = 0;
    memset(sv_p->vol_nm, 0, LEN);
    sv_p->st_dev = 0;
    sv_p->st_ino = 0;
    sv_p->users = 0;
    return;
}
