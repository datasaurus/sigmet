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
 .	$Revision: 1.32 $ $Date: 2010/11/08 16:40:30 $
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
#include "alloc.h"
#include "err_msg.h"
#include "sigmet.h"
#include "sigmet_raw.h"

#define LEN 1024

/* Maximum total size allowed for all volumes, in bytes */
static size_t max_size = 536870912;

/* A Sigmet volume struct and data to manage it. */
struct sig_vol {
    struct Sigmet_Vol vol;		/* Sigmet volume struct. See sigmet.h */
    char vol_nm[LEN];			/* file that provided the volume */
    dev_t st_dev;			/* Device that provided vol */
    ino_t st_ino;			/* I-number of file that provided vol */
    int h;				/* hash(vol_nm) */
    int keep;				/* If true, do not delete this volume */
    struct sig_vol *tprev, *tnext;	/* Previous and next volume in a linked
					   list (bucket) in vols */
    struct sig_vol *uprev, *unext;	/* Previous and next volumes in usage
					   linked list. When a volume is
					   accessed, it is placed at the tail of
					   this list. If the process needs
					   memory, it removes volumes from the
					   head of the usage list. */
};

/* Head and tail of the usage linked list */
static struct sig_vol *uhead, *utail;

/*
   Table of Sigmet volumes for the application. Each element is a linked list of
   volumes with a common hash (see the hash function defined below). The volumes
   are linked by their tprev and tnext members.
 */

#define N_VOLS 8
#define MASK 0x07
static struct sig_vol *vols[N_VOLS];

/* Local functions and variables */
static struct sig_vol *sig_vol_new(void);
static void sig_vol_free(struct sig_vol *);
static void tunlink(struct sig_vol *);
static void uunlink(struct sig_vol *sv_p);
static void uappend(struct sig_vol *sv_p);
static int hash(ino_t);
static int file_id(char *, dev_t *, ino_t *);
static struct sig_vol *sig_vol_get(char *vol_nm);
static void sig_vol_rm(char *vol_nm);
static FILE *vol_open(const char *, pid_t *, int, FILE *);
static size_t vol_size(void);

/* Initialize this interface */
void SigmetRaw_VolInit(void)
{
    int n;
    static int init;

    if ( init ) {
	return;
    }
    assert(N_VOLS == 8);
    for (n = 0; n < N_VOLS; n++) {
	vols[n] = NULL;
    }
    init = 1;
}

/* Free memory and reinitialize this interface */
void SigmetRaw_VolFree(void)
{
    struct sig_vol *sv_p, *tnext;
    int n;

    for (n = 0; n < N_VOLS; n++) {
	for (sv_p = tnext = vols[n]; sv_p; sv_p = tnext) {
	    tnext = sv_p->tnext;
	    sig_vol_free(sv_p);
	}
	vols[n] = NULL;
    }
}

/*
   Create a new Sigmet_Vol struct. Return its address.  If something goes
   wrong, store an error string with Err_Append and return NULL.
   Return value should eventually be freed with call to sig_vol_free.
 */
static struct sig_vol *sig_vol_new(void)
{
    struct sig_vol *sv_p;

    if ( !(sv_p = MALLOC(sizeof(struct sig_vol))) ) {
	Err_Append("Could not allocate memory for volume entry. ");
	return NULL;
    }
    Sigmet_InitVol(&sv_p->vol);
    memset(sv_p->vol_nm, '\0', LEN);
    sv_p->st_dev = 0;
    sv_p->st_ino = 0;
    sv_p->h = -1;
    sv_p->keep = 0;
    sv_p->tprev = sv_p->tnext = NULL;
    sv_p->uprev = sv_p->unext = NULL;
    return sv_p;
};

/* Free all memory associated with an addess returned by sig_vol_new. */
static void sig_vol_free(struct sig_vol *sv_p)
{
    if ( !sv_p ) {
	return;
    }
    Sigmet_FreeVol(&sv_p->vol);
    FREE(sv_p);
}

/* Unlink a volume from vols */
static void tunlink(struct sig_vol *sv_p)
{
    int h;

    if ( !sv_p ) {
	return;
    }
    if ( sv_p->tprev ) {
	sv_p->tprev->tnext = sv_p->tnext;
    }
    if ( sv_p->tnext ) {
	sv_p->tnext->tprev = sv_p->tprev;
    }
    h = sv_p->h;
    if ( sv_p == vols[h] ) {
	vols[h] = sv_p->tnext;
    }
    sv_p->tprev = sv_p->tnext = NULL;
}

/* Unlink a volume from the usage list */
static void uunlink(struct sig_vol *sv_p)
{
    if ( !sv_p ) {
	return;
    }
    if ( sv_p->uprev ) {
	sv_p->uprev->unext = sv_p->unext;
    }
    if ( sv_p->unext ) {
	sv_p->unext->uprev = sv_p->uprev;
    }
    if ( sv_p == uhead ) {
	uhead = sv_p->unext;
	if ( uhead ) {
	    uhead->uprev = NULL;
	}
    }
    if ( sv_p == utail ) {
	utail = sv_p->uprev;
    }
    sv_p->uprev = sv_p->unext = NULL;
}

/* Append a volume to the usage list */
static void uappend(struct sig_vol *sv_p)
{
    if ( !sv_p ) {
	return;
    }
    if ( utail ) {
	utail->unext = sv_p;
    }
    sv_p->uprev = utail;
    utail = sv_p;
    if ( !uhead ) {
	uhead = sv_p;
    }
}

/* Create an integer hash for file with index number st_ino. */
static int hash(ino_t st_ino)
{
    return (int)(st_ino & MASK);
}

/*
   Fetch device id and inode for the file named vol_nm, and store at d_p and i_p.
   Return true if successful. If failure, return false and store an error string
   with Err_Append.
 */
static int file_id(char *vol_nm, dev_t *d_p, ino_t *i_p)
{
    struct stat sbuf;

    if ( stat(vol_nm, &sbuf) == -1 ) {
	Err_Append("Could not get information about ");
	Err_Append(vol_nm);
	Err_Append(". ");
	Err_Append(strerror(errno));
	Err_Append(".\n");
	return 0;
    }
    *d_p = sbuf.st_dev;
    *i_p = sbuf.st_ino;
    return 1;
}

/*
   Find or create an entry for vol_nm in vols.  If a new sig_vol struct is
   created in vols, the vol member is initialized with a call to Sigmet_InitVol,
   but the volume is not read.  Return the (possibly new) entry. If something
   goes wrong, store an error string with Err_Append and return NULL.
 */
static struct sig_vol *sig_vol_get(char *vol_nm)
{
    dev_t d;			/* Id of device containing file named vol_nm */
    ino_t i;			/* Inode number for file named vol_nm*/
    int h;			/* Index into vols */
    struct sig_vol *sv_p;	/* Return value */

    if ( !file_id(vol_nm, &d, &i) ) {
	return NULL;
    }

    /*
       Search for the volume in vols. If present, move it to the end of the usage
       list and return its address. If absent, create an new entry in vols, append
       it to the end of the usage list, and return its address.
     */

    h = hash(i);
    for (sv_p = vols[h]; sv_p; sv_p = sv_p->tnext) {
	if ( (sv_p->st_dev == d) && (sv_p->st_ino == i) ) {
	    uunlink(sv_p);
	    uappend(sv_p);
	    return sv_p;
	}
    }
    if ( !(sv_p = sig_vol_new()) ) {
	return NULL;
    }
    strncpy(sv_p->vol_nm, vol_nm, LEN);
    sv_p->st_dev = d;
    sv_p->st_ino = i;
    sv_p->h = h;
    if ( vols[h] ) {
	vols[h]->tprev = sv_p;
    }
    sv_p->tnext = vols[h];
    vols[h] = sv_p;
    uappend(sv_p);
    return sv_p;
}

/*
   Deallocate a sig_vol struct and remove its entry from vols.  Quietly do nothing
   if there is no entry.
 */
static void sig_vol_rm(char *vol_nm)
{
    dev_t d;			/* Id of device containing file named vol_nm */
    ino_t i;			/* Inode number for file named vol_nm*/
    int h;			/* Index into vols */
    struct sig_vol *sv_p, *tnext;

    if ( !file_id(vol_nm, &d, &i) ) {
	return;
    }
    h = hash(i);
    for (sv_p = vols[h]; sv_p; sv_p = tnext) {
	tnext = sv_p->tnext;
	if ( (sv_p->st_dev == d) && (sv_p->st_ino == i) ) {
	    tunlink(sv_p);
	    uunlink(sv_p);
	    sig_vol_free(sv_p);
	    break;
	}
    }
}

/*
   Return true if vol_nm is a good (navigable) Sigmet volume.
   Print error messages to err.
 */
enum Sigmet_CB_Return SigmetRaw_GoodVol(char *vol_nm, int i_err, FILE *err)
{
    struct sig_vol *sv_p;
    FILE *in;
    int rslt;
    pid_t p;

    /* If volume is already loaded and not truncated, it is good */
    if ( (sv_p = sig_vol_get(vol_nm)) && !sv_p->vol.truncated ) {
	return SIGMET_CB_SUCCESS;
    }

    /* Volume not loaded. Inspect vol_nm with Sigmet_GoodVol. */
    if ( !(in = vol_open(vol_nm, &p, i_err, err)) ) {
	fprintf(err, "Could not open %s\n", vol_nm);
	return SIGMET_CB_INPUT_FAIL;
    }
    rslt = Sigmet_GoodVol(in) ? SIGMET_CB_SUCCESS : SIGMET_CB_FAIL;
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
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    enum Sigmet_ReadStatus status; /* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    if ( !(sv_p = sig_vol_get(vol_nm)) ) {
	fprintf(err, "No entry for %s in volume table, and unable to add it.\n%s\n",
		vol_nm, Err_Get());
	return SIGMET_CB_FAIL;
    }
    vol_p = (struct Sigmet_Vol *)sv_p;

    if ( vol_p->has_headers ) {
	*vol_pp = vol_p;
	return SIGMET_CB_SUCCESS;
    }

    /* Try up to max_try times to read volume headers */
    sv_p->keep = 1;
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err, err)) ) {
	    fprintf(err, "Could not open %s for input.\n", vol_nm);
	    sig_vol_rm(vol_nm);
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
		max_size = vol_size() * 3 / 4;
		if ( SigmetRaw_Flush() == 0 ) {
		    fprintf(err, "Could not offload any volumes\n");
		    try = max_try;
		}
		break;
	    case SIGMET_VOL_INPUT_FAIL:
	    case SIGMET_VOL_BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fprintf(err, "%s\n", Err_Get());
		try = max_try;
		break;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    sv_p->keep = 0;
    if ( !loaded ) {
	fprintf(err, "Could not read %s\n", vol_nm);
	sig_vol_rm(vol_nm);
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
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    enum Sigmet_ReadStatus status; /* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    if ( !(sv_p = sig_vol_get(vol_nm)) ) {
	fprintf(err, "No entry for %s in volume table, and unable to add it.\n%s\n",
		vol_nm, Err_Get());
	return SIGMET_CB_FAIL;
    }

    vol_p = (struct Sigmet_Vol *)sv_p;
    if ( !vol_p->truncated ) {
	*vol_pp = vol_p;
	return SIGMET_CB_SUCCESS;
    }
    Sigmet_FreeVol(vol_p);

    /* Try up to max_tries times to read volume */
    sv_p->keep = 1;
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err, err)) ) {
	    fprintf(err, "Could not open %s for input.\n", vol_nm);
	    sig_vol_rm(vol_nm);
	    return SIGMET_CB_INPUT_FAIL;
	}
	switch (status = Sigmet_ReadVol(in, vol_p)) {
	    case SIGMET_VOL_READ_OK:
	    case SIGMET_VOL_INPUT_FAIL:
		/* Success or partial success. Break out. */
		loaded = 1;
		break;
	    case SIGMET_VOL_MEM_FAIL:
		/* Try to free some memory and try again */
		fprintf(err, "Read failed. Out of memory. %s "
			"Offloading unused volumes\n", Err_Get());
		max_size = vol_size() * 3 / 4;
		if ( SigmetRaw_Flush() == 0 ) {
		    fprintf(err, "Could not offload any volumes\n");
		    try = max_try;
		}
		break;
	    case SIGMET_VOL_BAD_VOL:
		/* Read failed. Disable this slot and return failure. */
		fprintf(err, "Read failed, bad volume. %s\n", Err_Get());
		Sigmet_FreeVol(vol_p);
		try = max_try;
		break;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    sv_p->keep = 0;
    if ( !loaded ) {
	fprintf(err, "Could not read %s\n", vol_nm);
	sig_vol_rm(vol_nm);
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
    *vol_pp = vol_p;
    return SIGMET_CB_SUCCESS;
}

/* Print list of currently loaded volumes. */
void SigmetRaw_VolList(FILE *out)
{
    int n;
    struct sig_vol *sv_p;

    for (n = 0; n < N_VOLS; n++) {
	for (sv_p = vols[n]; sv_p; sv_p = sv_p->tnext) {
	    fprintf(out, "%s %s. %d out of %d sweeps. %8.4f MB.\n",
		    sv_p->vol_nm,
		    (sv_p->keep) ? "Keep" : "Free",
		    sv_p->vol.num_sweeps_ax,
		    sv_p->vol.ih.ic.num_sweeps,
		    sv_p->vol.size / 1048576.0);
	}
    }
}

/* Indicate that a volume no longer must be kept */
void SigmetRaw_Keep(char *vol_nm)
{
    struct sig_vol *sv_p;

    if ( (sv_p = sig_vol_get(vol_nm)) ) {
	sv_p->keep = 1;
    }
}

/* Indicate that a volume no longer must be kept */
void SigmetRaw_Release(char *vol_nm)
{
    struct sig_vol *sv_p;

    if ( (sv_p = sig_vol_get(vol_nm)) ) {
	sv_p->keep = 0;
    }
}

/* Remove unused volumes. Return number of volumes removed. */
int SigmetRaw_Flush(void)
{
    int c;
    struct sig_vol *sv_p, *unext;

    for (sv_p = uhead, c = 0; sv_p && vol_size() > max_size ; sv_p = unext) {
	unext = sv_p->unext;
	if ( !sv_p->keep ) {
	    tunlink(sv_p);
	    uunlink(sv_p);
	    sig_vol_free(sv_p);
	    c++;
	}
    }
    return c;
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
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
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
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
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

/* Compute total size of currently loaded sigmet volumes */
static size_t vol_size(void)
{
    size_t sz;
    struct sig_vol *sv_p;

    for (sz = 0, sv_p = uhead; sv_p; sv_p = sv_p->unext) {
	sz += sv_p->vol.size;
    }
    return sz;
}
