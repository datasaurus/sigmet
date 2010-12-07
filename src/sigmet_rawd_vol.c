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
 .	$Revision: 1.53 $ $Date: 2010/12/07 19:22:29 $
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

/*
   Structures of this type store a Sigmet volume with additonal data
   that sigmet_rawd needs to manage it.
 */

struct sig_vol {
    struct Sigmet_Vol vol;		/* Sigmet volume struct. See sigmet.h */
    char *vol_nm;			/* file that provided the volume */
    dev_t st_dev;			/* Device that provided vol */
    ino_t st_ino;			/* I-number of file that provided vol */
    int h;				/* hash for vol_nm */
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

/*
   max_vol_size stores the maximum total size allowed for all volumes,
   in bytes. vol_size() computes the current size of all volumes. If
   vol_size() > max_vol_size, it is time to offload expendable volumes.
 */

static size_t max_vol_size = 402653184;
static size_t vol_size(void);

/*
   Head and tail of the usage linked list. All volumes are in this list.
   When a volume is accessed, it is moved to the tail. When sigmet_rawd
   offloads volumes, it removes uhead until vol_size <= max_vol_size.
   This approach assumes that volumes that have not been accessed recently
   are more expendable.
 */

static struct sig_vol *uhead, *utail;

/*
   vols is a hash table of currently loaded Sigmet volumes. Each element is
   a linked list of volumes with a common hash (see the hash function defined
   below). The volumes in each bucket are linked by their tprev and tnext
   members.  The hash function applies MASK to the volume file's index number
   to obtain an index into vols. N_VOLS must be a power of two for this to
   work.
 */

#define N_VOLS 1024
#define MASK 0x3ff
static struct sig_vol *vols[N_VOLS];

/*
   Other functions.
 */

static struct sig_vol *sig_vol_get(char *vol_nm);
static void sig_vol_free(struct sig_vol *);
static void tunlink(struct sig_vol *);
static void uunlink(struct sig_vol *sv_p);
static void uappend(struct sig_vol *sv_p);
static int hash(char *, dev_t *, ino_t *, int *);
static FILE *vol_open(const char *, pid_t *, int);

/*
   Initialize this interface
 */

void SigmetRaw_VolInit(void)
{
    int n;
    static int init;

    if ( init ) {
	return;
    }
    assert(N_VOLS == MASK + 1);
    for (n = 0; n < N_VOLS; n++) {
	vols[n] = NULL;
    }
    init = 1;
}

/*
   Free memory and reinitialize this interface.
 */

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
   Free all memory associated with an addess returned by sig_vol_get.
 */

static void sig_vol_free(struct sig_vol *sv_p)
{
    if ( !sv_p ) {
	return;
    }
    Sigmet_Vol_Free(&sv_p->vol);
    FREE(sv_p->vol_nm);
    FREE(sv_p);
}

/*
   Unlink a volume from vols
 */

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

/*
   Unlink a volume from the usage list
 */

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

/*
   Append a volume to the usage list
 */

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

/*
   Fetch device id and inode for the file named vol_nm, and store at d_p and
   i_p.  Store a hash for the file at h_p.  Return true if successful. If
   something goes wrong, return false and store an error string with Err_Append.
 */

static int hash(char *vol_nm, dev_t *d_p, ino_t *i_p, int *h_p)
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
    *h_p =  (int)(sbuf.st_ino & MASK);
    return 1;
}

/*
   Find or create an entry for vol_nm in vols.  If a new sig_vol struct is
   created in vols, the vol member is initialized with a call to Sigmet_Vol_Init,
   but the volume is not read.  Return the (possibly new) entry. Memory
   associated with the return value should eventually be freed with a call to
   sig_vol_free.  If something goes wrong, store an error string with
   Err_Append and return NULL.
 */

static struct sig_vol *sig_vol_get(char *vol_nm)
{
    dev_t d;			/* Id of device containing file named vol_nm */
    ino_t i;			/* Inode number for file named vol_nm*/
    int h;			/* Index into vols */
    struct sig_vol *sv_p;	/* Return value */


    /*
       Search for the volume in vols. If present, move it to the end of the
       usage list and return its address. If absent, create an new entry in
       vols, append it to the end of the usage list, and return its address.
     */

    if ( !hash(vol_nm, &d, &i, &h) ) {
	return NULL;
    }
    for (sv_p = vols[h]; sv_p; sv_p = sv_p->tnext) {
	if ( (sv_p->st_dev == d) && (sv_p->st_ino == i) ) {
	    uunlink(sv_p);
	    uappend(sv_p);
	    return sv_p;
	}
    }
    if ( !(sv_p = MALLOC(sizeof(struct sig_vol))) ) {
	Err_Append("Could not allocate memory for volume entry. ");
	return NULL;
    }
    if ( !(sv_p->vol_nm = MALLOC(strlen(vol_nm) + 1)) ) {
	Err_Append("Could not allocate memory for volume entry. ");
	FREE(sv_p);
	return NULL;
    }
    Sigmet_Vol_Init(&sv_p->vol);
    strcpy(sv_p->vol_nm, vol_nm);
    sv_p->st_dev = d;
    sv_p->st_ino = i;
    sv_p->h = h;
    sv_p->keep = 0;
    sv_p->tprev = NULL;
    sv_p->tnext = vols[h];
    if ( vols[h] ) {
	vols[h]->tprev = sv_p;
    }
    vols[h] = sv_p;
    sv_p->uprev = sv_p->unext = NULL;
    uappend(sv_p);
    return sv_p;
}

/*
   Return true if vol_nm is a good (navigable) Sigmet volume.
   Propagate error messages with Err_Append.
 */

int SigmetRaw_GoodVol(char *vol_nm, int i_err)
{
    struct sig_vol *sv_p;
    FILE *in;
    int status;
    pid_t p;

    /* If volume is already loaded and not truncated, it is good */
    if ( (sv_p = sig_vol_get(vol_nm)) && !sv_p->vol.truncated ) {
	return SIGMET_OK;
    }

    /* Volume not loaded. Inspect vol_nm with Sigmet_Vol_Good. */
    if ( !(in = vol_open(vol_nm, &p, i_err)) ) {
	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(". ");
	return SIGMET_IO_FAIL;
    }
    status = Sigmet_Vol_Good(in) ? SIGMET_OK : SIGMET_BAD_FILE;
    fclose(in);
    if ( p != -1 ) {
	waitpid(p, NULL, 0);
    }
    return status;
}

/*
   Fetch headers for the volume stored in Sigmet raw product file vol_nm.
   If vol_nm is absent from vols, or not loaded, this function loads
   the volume headers, but not data.  Propagate error messages with Err_Append.
 */

int SigmetRaw_ReadHdr(char *vol_nm, int i_err, struct Sigmet_Vol ** vol_pp)
{
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    int status;			/* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    /*
       Find or create and entry for vol_nm in vols. If there is an entry
       and the vol member has headers, return.
     */

    if ( !(sv_p = sig_vol_get(vol_nm)) ) {
	Err_Append("No entry for ");
	Err_Append(vol_nm);
	Err_Append(" in volume table and unable to add it. ");
	return SIGMET_BAD_ARG;
    }
    vol_p = (struct Sigmet_Vol *)sv_p;
    if ( vol_p->has_headers ) {
	*vol_pp = vol_p;
	return SIGMET_OK;
    }

    /*
       There is an entry for vol_nm in vols, but the vol member does not have
       headers.  Try up to max_try times to read volume headers. If there
       is an allocation failure while reading, reduce max_vol_size and
       offload expendable volumes with a call to SigmetRaw_Flush. Temporarily
       setting the "keep" member keeps this volume from being flushed while
       Sigmet_Vol_ReadHdr is using it.
     */

    sv_p->keep = 1;
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err)) ) {
	    Err_Append("Could not open ");
	    Err_Append(vol_nm);
	    Err_Append(". ");
	    SigmetRaw_Delete(vol_nm);
	    return SIGMET_IO_FAIL;
	}
	switch (status = Sigmet_Vol_ReadHdr(in, vol_p)) {
	    case SIGMET_OK:
		loaded = 1;
		break;
	    case SIGMET_IO_FAIL:
		Err_Append("Input error while reading headers. ");
		break;
	    case SIGMET_BAD_FILE:
		Err_Append("Raw product file is corrupt. ");
		try = max_try;
		break;
	    case SIGMET_ALLOC_FAIL:
		Err_Append("Out of memory. Offloading unused volumes. ");
		max_vol_size = vol_size() * 3 / 4;
		if ( (status = SigmetRaw_Flush()) != SIGMET_OK ) {
		    Err_Append("Could not offload sufficient volumes ");
		    try = max_try;
		}
		break;
	    case SIGMET_BAD_ARG:
		Err_Append("Internal failure while reading volume headers. ");
		try = max_try;
		break;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    if ( !loaded ) {
	Err_Append("Could not read headers from ");
	Err_Append(vol_nm);
	Err_Append(". ");
	SigmetRaw_Delete(vol_nm);
	return status;
    }
    if ( vol_size() > max_vol_size
	    && (status = SigmetRaw_Flush()) != SIGMET_OK ) {
	Err_Append("Reading ");
	Err_Append(vol_nm);
	Err_Append(" puts memory use beyond limit and unable to flush. ");
	SigmetRaw_Delete(vol_nm);
	return status;
    }
    sv_p->keep = 0;
    *vol_pp = vol_p;
    return SIGMET_OK;
}

/*
   Fetch headers and data for the volume stored in Sigmet raw product file
   vol_nm.  If vol_nm is absent from vols, or not loaded, this function loads
   the volume headers and data.  Propagate error messages with Err_Append.
 */

int SigmetRaw_ReadVol(char *vol_nm, int i_err, struct Sigmet_Vol **vol_pp)
{
    struct sig_vol *sv_p;	/* Member of vols */
    struct Sigmet_Vol *vol_p;	/* Return value */
    int loaded;			/* If true, volume is loaded */
    int try;			/* Number of attempts to read volume */
    int max_try = 3;		/* Maximum tries */
    int status;			/* Result of a read call */
    FILE *in;			/* Stream from Sigmet raw file */
    pid_t in_pid = -1;		/* Process providing in, if any */

    /*
       Find or create and entry for vol_nm in vols. If there is an entry
       and the vol member has headers and data (is not truncated), return.
     */

    if ( !(sv_p = sig_vol_get(vol_nm)) ) {
	Err_Append("No entry for ");
	Err_Append(vol_nm);
	Err_Append(" in volume table and unable to add it. ");
	return SIGMET_BAD_ARG;
    }

    vol_p = (struct Sigmet_Vol *)sv_p;
    if ( !vol_p->truncated ) {
	*vol_pp = vol_p;
	return SIGMET_OK;
    }
    Sigmet_Vol_Free(vol_p);

    /*
       There is an entry for vol_nm in vols, but the vol member does not have
       data.  Try up to max_try times to read the volume. If there
       is an allocation failure while reading, reduce max_vol_size and
       offload expendable volumes with a call to SigmetRaw_Flush. Temporarily
       setting the "keep" member keeps this volume from being flushed while
       Sigmet_Vol_Read is using it.
     */

    sv_p->keep = 1;
    for (try = 0, loaded = 0; !loaded && try < max_try; try++) {
	in_pid = -1;
	if ( !(in = vol_open(vol_nm, &in_pid, i_err)) ) {
	    Err_Append("Could not open ");
	    Err_Append(vol_nm);
	    Err_Append(". ");
	    SigmetRaw_Delete(vol_nm);
	    return SIGMET_IO_FAIL;
	}
	switch (status = Sigmet_Vol_Read(in, vol_p)) {
	    case SIGMET_OK:
	    case SIGMET_IO_FAIL:
		loaded = 1;
		break;
	    case SIGMET_ALLOC_FAIL:
		max_vol_size = vol_size() * 3 / 4;
		if ( (status = SigmetRaw_Flush()) != SIGMET_OK ) {
		    Err_Append("Allocation failed while reading volume, "
			    "and could not offload sufficient volumes to "
			    "continue. ");
		    try = max_try;
		}
		break;
	    case SIGMET_BAD_FILE:
		Err_Append("Raw product file is corrupt. ");
		try = max_try;
		break;
	    case SIGMET_BAD_ARG:
		Err_Append("Internal failure while reading volume headers. ");
		try = max_try;
		break;
	}
	fclose(in);
	if (in_pid != -1) {
	    waitpid(in_pid, NULL, 0);
	}
    }
    if ( !loaded ) {
	Err_Append("Could not read ");
	Err_Append(vol_nm);
	Err_Append(". ");
	SigmetRaw_Delete(vol_nm);
	return status;
    }
    if ( vol_size() > max_vol_size
	    && (status = SigmetRaw_Flush()) != SIGMET_OK ) {
	Err_Append("Reading ");
	Err_Append(vol_nm);
	Err_Append(" puts memory use beyond limit and unable to flush. ");
	SigmetRaw_Delete(vol_nm);
	return SIGMET_FLUSH_FAIL;
    }
    sv_p->keep = 0;
    *vol_pp = vol_p;
    return SIGMET_OK;
}

/*
   Print list of currently loaded volumes.
 */

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

/*
   Indicate that a volume no longer must be kept
 */

void SigmetRaw_Keep(char *vol_nm)
{
    struct sig_vol *sv_p;

    if ( (sv_p = sig_vol_get(vol_nm)) ) {
	sv_p->keep = 1;
    }
}

/*
   Indicate that a volume no longer must be kept
 */

void SigmetRaw_Release(char *vol_nm)
{
    struct sig_vol *sv_p;

    if ( (sv_p = sig_vol_get(vol_nm)) ) {
	sv_p->keep = 0;
    }
}

/*
   Deallocate the sig_vol struct associated with vol_nm and remove its entry
   from vols.  Return true if an entry for vol_nm is found and deleted,
   otherwise return false.
 */

int SigmetRaw_Delete(char *vol_nm)
{
    dev_t d;			/* Id of device containing file named vol_nm */
    ino_t i;			/* Inode number for file named vol_nm*/
    int h;			/* Index into vols */
    struct sig_vol *sv_p;	/* Volume to delete */

    if ( !hash(vol_nm, &d, &i, &h) ) {
	return SIGMET_BAD_ARG;
    }
    for (sv_p = vols[h]; sv_p; sv_p = sv_p->tnext) {
	if ( (sv_p->st_dev == d) && (sv_p->st_ino == i) ) {
	    uunlink(sv_p);
	    tunlink(sv_p);
	    sig_vol_free(sv_p);
	    return SIGMET_OK;
	}
    }
    return SIGMET_BAD_ARG;
}

/*
   Remove expendable volumes. A volume is expendable if it's "keep" member is
   not set.
 */

int SigmetRaw_Flush(void)
{
    struct sig_vol *sv_p, *unext;

    /*
       Remove volumes from the head of the usage list until
       vol_size <= max_vol_size. When volumes are accessed,
       they are moved to the tail, so the most recently
       accessed volumes are the least likely to be deleted.
     */

    for (sv_p = uhead; sv_p && vol_size() > max_vol_size; sv_p = unext) {
	unext = sv_p->unext;
	if ( !sv_p->keep ) {
	    tunlink(sv_p);
	    uunlink(sv_p);
	    sig_vol_free(sv_p);
	}
    }
    return (vol_size() <= max_vol_size) ?  SIGMET_OK : SIGMET_FLUSH_FAIL;
}

/*
   Compute total size of currently loaded sigmet volumes
 */

static size_t vol_size(void)
{
    size_t sz;
    struct sig_vol *sv_p;

    for (sz = 0, sv_p = uhead; sv_p; sv_p = sv_p->unext) {
	sz += sv_p->vol.size;
    }
    return sz;
}

/*
   Get or set max_vol_size
 */

size_t SigmetRaw_MaxSize(size_t sz)
{
    if ( sz > 0 ) {
	max_vol_size = sz;
    }
    return max_vol_size;
}

/*
   Open volume file vol_nm.  If vol_nm suffix indicates a compressed file, open
   a pipe to a decompression process.  Return a file handle to the file or
   decompression process, or NULL if failure. If return value is output from a
   decompression process, put the process id at pid_p. Set *pid_p to -1 if
   there is no decompression process (i.e. vol_nm is a plain file).
   Propagate error messages with Err_Append.  Have child send its error messages
   to i_err.
 */

static FILE *vol_open(const char *vol_nm, pid_t *pid_p, int i_err)
{
    FILE *in = NULL;		/* Return value */
    char *sfx;			/* Filename suffix */
    int pfd[2] = {-1};		/* Pipe for data */
    pid_t ch_pid = -1;		/* Child process id */

    *pid_p = -1;
    sfx = strrchr(vol_nm, '.');
    if ( sfx && strcmp(sfx, ".gz") == 0 ) {
	/*
	   If filename ends with ".gz", read from gunzip pipe
	 */

	if ( pipe(pfd) == -1 ) {
	    Err_Append("Could not create pipe for gzip\n");
	    Err_Append(strerror(errno));
	    Err_Append("\n");
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		Err_Append("Could not spawn gzip. ");
		goto error;
	    case 0:
		/*
		   Child process - gzip.  Send child stdout to pipe and child
		   stderr to i_err.
		 */

		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/*
		   This process.  Read output from gzip.
		 */

		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    Err_Append("Could not read gzip process\n%s\n");
		    Err_Append(strerror(errno));
		    Err_Append("\n");
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( sfx && strcmp(sfx, ".bz2") == 0 ) {
	/*
	   If filename ends with ".bz2", read from bunzip2 pipe
	 */

	if ( pipe(pfd) == -1 ) {
	    Err_Append("Could not create pipe for bzip2\n");
	    Err_Append(strerror(errno));
	    Err_Append("\n");
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		Err_Append("Could not spawn bzip2. ");
		goto error;
	    case 0:
		/*
		   Child process - bzip2.  Send child stdout to pipe and child
		   stderr to i_err.
		 */

		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		if ( dup2(i_err, STDERR_FILENO) == -1 || close(i_err) == -1 ) {
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default:
		/*
		   This process.  Read output from bzip2.
		 */

		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	/*
	   Uncompressed file
	 */

	Err_Append("Could not open ");
	Err_Append(vol_nm);
	Err_Append(" ");
	Err_Append(strerror(errno));
	Err_Append("\n");
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
