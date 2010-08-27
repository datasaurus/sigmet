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
 .	$Revision: 1.2 $ $Date: 2010/08/27 13:55:27 $
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
static void flush(void);
static int unload(struct sig_vol *);

/* Sigmet_ReadHdr or Sigmet_ReadVol */
typedef enum Sigmet_ReadStatus (read_fn_t)(FILE *, struct Sigmet_Vol *);

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
	memset(sv_p, 0, sizeof(struct sig_vol));
    }
    init = 1;
}

/* Free memory and reinitialize this interface */
void SigmetRaw_VolFree(void)
{
    struct sig_vol *sv_p;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	Sigmet_FreeVol((struct Sigmet_Vol *)sv_p);
	memset(sv_p, 0, sizeof(struct sig_vol));
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
    if ( (sv_p = get_vol(st_dev, st_ino)) >= 0 && !sv_p->vol.truncated ) {
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
   Fetch a volume from the data base, loading it if necessary.  Caller only
   needs num_sweeps sweeps.  Send error messages to err or i_err.
 */
struct Sigmet_Vol *SigmetRaw_GetVol(char *vol_nm, unsigned num_sweeps, FILE *err,
	int i_err)
{
    dev_t st_dev;		/* Device where vol_nm resides */
    ino_t st_ino;		/* File system index number for vol_nm */
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *v_p;	/* Return value */
    unsigned s;			/* Sweep index */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */
    read_fn_t *read_fn;		/* Function to call to read volume */

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return 0;
    }

    if ( (sv_p = get_vol(st_dev, st_ino)) ) {
	/*
	   If volume is loaded and has at least num_sweeps, return it.
	   Otherwise, unload it, but keep the entry in the vols array.
	 */

	v_p = (struct Sigmet_Vol *)sv_p;
	if ( num_sweeps == 0 && v_p->has_headers ) {
	    sv_p->users++;
	    return v_p;
	}
	for (s = 0; s < v_p->ih.tc.tni.num_sweeps && v_p->sweep_ok[s]; s++) {
	    continue;
	}
	if ( s >= num_sweeps ) {
	    sv_p->users++;
	    return v_p;
	} else {
	    Sigmet_FreeVol(v_p);
	}
    } else if ( (sv_p = new_vol(st_dev, st_ino)) ) {
	fprintf(err, "Volume table full. Could not (re)load %s\n", vol_nm);
	return 0;
    }
    v_p = (struct Sigmet_Vol *)sv_p;

    /* Read volume */
    read_fn = (num_sweeps == 0) ? Sigmet_ReadHdr : Sigmet_ReadVol;
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err, err)) ) {
	    fprintf(err, "Could not open %s for input.\n", vol_nm);
	    unload(sv_p);
	    return 0;
	}
	switch (read_fn(in, v_p)) {
	    case READ_OK:
		/* Success. Break out. */
		loaded = 1;
		break;
	    case MEM_FAIL:
		/* Try to free some memory and try again */
		fprintf(err, "Out of memory. Offloading unused volumes\n");
		flush();
		break;
	    case INPUT_FAIL:
	    case BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fprintf(err, "%s\n", Err_Get());
		unload(sv_p);
		break;
	}
	fseek(in, 0, SEEK_END);
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
	unload(sv_p);
	return 0;
    }
    strncpy(sv_p->vol_nm, vol_nm, LEN);
    sv_p->users++;
    return v_p;
}

/* Print list of currently loaded volumes. */
int SigmetRaw_VolList(FILE *out)
{
    struct sig_vol *sv_p;
    struct Sigmet_Vol *v_p;
    int s;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	v_p = (struct Sigmet_Vol *)sv_p;
	if ( sv_p->in_use ) {
	    /* File name. Number of users. Number of sweeps. */
	    fprintf(out, "%s users=%d. ", sv_p->vol_nm, sv_p->users);
	    for (s = 0; s < v_p->ih.tc.tni.num_sweeps && v_p->sweep_ok[s]; s++) {
		continue;
	    }
	    fprintf(out, "sweeps=%d.\n ", s);
	}
    }
    return 1;
}

/*
   Clients use this function to indicate a volume is no longer needed.
   It decrements the volume's user count. If user count goes to zero,
   the volume is a candidate for deletion.
 */
int SigmetRaw_Release(char *vol_nm, FILE *err)
{
    dev_t st_dev;
    ino_t st_ino;
    struct sig_vol *sv_p;

    if ( !file_id(vol_nm, &st_dev, &st_ino) ) {
	fprintf(err, "Could not get information about %s\n%s\n",
		vol_nm, strerror(errno));
	return 0;
    }
    if ( (sv_p = get_vol(st_dev, st_ino)) && sv_p->users > 0 ) {
	sv_p->users--;
    }
    return 1;
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
    FILE *in = NULL;	/* Return value */
    char *sfx;		/* Filename suffix */
    int pfd[2] = {-1};	/* Pipe for data */
    pid_t ch_pid = -1;	/* Child process id */

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
static int unload(struct sig_vol *sv_p)
{
    if ( sv_p->users > 0 ) {
	return 0;
    }
    Sigmet_FreeVol((struct Sigmet_Vol *)sv_p);
    memset(sv_p, 0, sizeof(struct sig_vol));
    return 1;
}

/* Remove unused volumes */
static void flush(void)
{
    struct sig_vol *sv_p;

    for (sv_p = vols; sv_p < vols + N_VOLS; sv_p++) {
	unload(sv_p);
    }
}
