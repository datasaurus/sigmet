/*
   -	sigmet_raw.c --
   -		Command line access to sigmet raw product volumes.
   -		See sigmet_raw (1).
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
   .	$Revision: 1.107 $ $Date: 2012/11/02 20:47:45 $
 */

#include "unix_defs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "alloc.h"
#include "tm_calc_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/*
   Local functions
 */

static FILE *vol_open(const char *, pid_t *);
static struct Sigmet_Vol *vol_attach(int *);
static void vol_detach(struct Sigmet_Vol *, int);
static int handle_signals(void);
static void handler(int signum);
static char *sigmet_err(enum SigmetStatus);

/*
   Callbacks for the subcommands.
 */

typedef int (callback)(int , char **);
static callback version_cb;
static callback load_cb;
static callback data_types_cb;
static callback volume_headers_cb;
static callback vol_hdr_cb;
static callback near_sweep_cb;
static callback sweep_headers_cb;
static callback ray_headers_cb;
static callback new_field_cb;
static callback del_field_cb;
static callback size_cb;
static callback set_field_cb;
static callback add_cb;
static callback sub_cb;
static callback mul_cb;
static callback div_cb;
static callback log10_cb;
static callback incr_time_cb;
static callback data_cb;
static callback bdata_cb;
static callback bin_outline_cb;
static callback radar_lon_cb;
static callback radar_lat_cb;
static callback shift_az_cb;
static callback outlines_cb;
static callback dorade_cb;

/*
   Subcommand names and associated callbacks. The hash function defined
   below returns the index from cmd1v or cb1v corresponding to string argv1.

   Array should be sized for perfect hashing. Parser does not search buckets.

   Hashing function from Kernighan, Brian W. and Rob Pike, The Practice of
   Programming, Reading, Massachusetts. 1999
 */

#define N_HASH_CMD 126
static char *cmd1v[N_HASH_CMD] = {
    "", "", "", "outlines", "radar_lon", "", "", "", "",
    "near_sweep", "", "", "", "", "", "", "", "", "",
    "volume_headers", "shift_az", "", "", "", "", "", "",
    "add", "", "", "", "", "", "", "", "", "", "",
    "", "sweep_headers", "", "", "", "set_field", "", "",
    "", "", "bin_outline", "", "load", "", "", "dorade", "",
    "", "", "", "", "div", "", "", "vol_hdr", "",
    "del_field", "", "incr_time", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "radar_lat", "", "",
    "", "sub", "", "", "", "", "", "", "", "", "",
    "", "new_field", "", "ray_headers", "data", "", "", "",
    "data_types", "", "", "", "", "size", "", "", "version",
    "", "bdata", "log10", "", "", "", "", "", "", "",
    "", "", "", "mul", ""
};
static callback *cb1v[N_HASH_CMD] = {
    NULL, NULL, NULL, outlines_cb, radar_lon_cb, NULL, NULL, NULL, NULL,
    near_sweep_cb, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    volume_headers_cb, shift_az_cb, NULL, NULL, NULL, NULL, NULL, NULL,
    add_cb, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, sweep_headers_cb, NULL, NULL, NULL, set_field_cb, NULL, NULL,
    NULL, NULL, bin_outline_cb, NULL, load_cb, NULL, NULL, dorade_cb, NULL,
    NULL, NULL, NULL, NULL, div_cb, NULL, NULL, vol_hdr_cb, NULL,
    del_field_cb, NULL, incr_time_cb, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, radar_lat_cb, NULL, NULL,
    NULL, sub_cb, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, new_field_cb, NULL, ray_headers_cb, data_cb, NULL, NULL, NULL,
    data_types_cb, NULL, NULL, NULL, NULL, size_cb, NULL, NULL, version_cb,
    NULL, bdata_cb, log10_cb, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, mul_cb, NULL
};
#define HASH_X 31
static int hash(const char *);
static int hash(const char *argv1)
{
    unsigned h;

    for (h = 0 ; *argv1 != '\0'; argv1++) {
	h = HASH_X * h + (unsigned)*argv1;
    }
    return h % N_HASH_CMD;
}

/*
   Default static string length
 */

#define LEN 255

/*
   Names of environment variables
 */

#define SIGMET_VOL_SHMEM "SIGMET_VOL_SHMEM"
#define SIGMET_VOL_SEM "SIGMET_VOL_SEM"

/*
   main function. See sigmet_raw (1)
 */

int main(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1;
    int n;

    if ( !handle_signals() ) {
	fprintf(stderr, "%s (%d): could not set up signal management.",
		argv0, getpid());
	exit(EXIT_FAILURE);
    }
    if ( argc < 2 ) {
	fprintf(stderr, "Usage: %s command\n", argv0);
	exit(EXIT_FAILURE);
    }
    argv1 = argv[1];
    n = hash(argv1);
    if ( strcmp(argv1, cmd1v[n]) == 0 ) {
	exit( cb1v[n](argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE );
    } else {
	fprintf(stderr, "%s: unknown subcommand %s. Subcommand must be one of ",
		argv0, argv1);
	for (n = 0; n < N_HASH_CMD; n++) {
	    if ( strlen(cmd1v[n]) > 0 ) {
		fprintf(stderr, " %s", cmd1v[n]);
	    }
	}
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
    }
    return EXIT_FAILURE;
}

static int version_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    printf("%s version %s\nCopyright (c) 2011, Gordon D. Carrie.\n"
	    "All rights reserved.\n", argv[0], SIGMET_VERSION);
    return 1;
}

/*
   Load a Sigmet raw product volume into shared memory and spawn a given
   command. If volume is already loaded, attach to loaded volume, instead
   of reloading it, and spawn the command.
 */

static int load_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char **a;				/* Member of argv */
    int status;				/* Return value from various functions
					 */
    char *vol_fl_nm;			/* Name of file with Sigmet raw
					   volume */
    FILE *vol_fl;			/* Input stream associated with
					   vol_fl_nm */
    pid_t lpid;				/* Process id of read process */
    struct Sigmet_Vol *vol_p = NULL;	/* Volume to load */
    pid_t ch_pid;			/* Process id for child process
					   specified on command line  */
    int ch_stat;			/* Exit status of child specified
					   on command line */

    int flags;				/* IPC flags for shmget and semget */

    /*
       Volume data is stored in shared memory controlled by System V
       IPC calls. The child process environment will contain identifiers
       to the IPC resources needed to access the volume.
     */

    key_t mem_key;			/* IPC identifier for volume shared 
					   memory */
    char mem_key_id = 'm';		/* Key identifier for shared memory */
    int shm_id = -1;			/* Identifier for shared memory
					   which will store the Sigmet_Vol
					   structure */
    char shmid_s[LEN];			/* String representation of shm_id */

    /*
       This semaphore controls access to the shared memory.
       It is initialized to 1 when the volume loads. Callbacks
       defined below must decrement the semaphore before accessing
       the shared memory. They must post it back to 1 when done.
     */

    key_t ax_key;			/* IPC identifier for volume access
					   semaphore */
    char ax_key_id = 'a';		/* Key identifier for access semaphore
					 */
    int ax_sem_id = -1;			/* Semaphore to control access to
					   volume */
    char ax_sem_id_s[LEN];		/* String representation of ax_sem_id */
    union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
    } arg;				/* Argument for semctl */
    struct semid_ds buf;		/* Buffer to receive semaphore status */
    struct sembuf sop;			/* Argument for semop */
    int ncnt;				/* Number of processes waiting for
					   value of access semaphore to
					   increase */


    /*
       Parse command line.
     */

    if ( argc < 4 ) {
	fprintf(stderr, "Usage: %s %s sigmet_volume command [args ...]\n",
		argv0, argv1);
	return 0;
    }
    vol_fl_nm = argv[2];

    if ( (mem_key = ftok(vol_fl_nm, mem_key_id)) == -1 ) {
	fprintf(stderr, "%s %s: could not get memory key for volume %s.\n%s\n",
		argv0, argv1, vol_fl_nm, strerror(errno));
	goto error;
    }
    flags = S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL;
    shm_id = shmget(mem_key, sizeof(struct Sigmet_Vol), flags);
    if ( shm_id >= 0 ) {
	/*
	   shmget has returned a new shared memory identifier. Volume
	   has not yet been loaded. Load the volume and initialize
	   resources.
	 */

	vol_p = shmat(shm_id, NULL, 0);
	if ( vol_p == (void *)-1 ) {
	    fprintf(stderr, "%s %s: could not attach to shared memory for "
		    "volume.\n%s\n", argv0, argv1, strerror(errno));
	    goto error;
	}
	Sigmet_Vol_Init(vol_p);
	vol_p->shm = 1;
	lpid = -1;
	if ( !(vol_fl = vol_open(vol_fl_nm, &lpid)) ) {
	    fprintf(stderr, "%s %s: could not open file %s for reading.\n%s\n",
		    argv0, argv1, vol_fl_nm, strerror(errno));
	    goto error;
	}
	status = Sigmet_Vol_Read(vol_fl, vol_p);
	fclose(vol_fl);
	if ( lpid != -1 ) {
	    waitpid(lpid, NULL, 0);
	}
	if ( status != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not read volume.\n%s\n",
		    argv0, argv1, sigmet_err(status));
	    goto error;
	}
	vol_p->num_users = 1;
	printf("%s %s: done reading. Sigmet volume in memory "
		"for process %d.\n", argv0, argv1, getpid());

	/*
	   Create the semaphore that controls volume access.  Create with
	   write access, but not read access. This will tell competing load
	   processes to wait (see the "else if" block below). Allow read
	   access once semaphore is fully initialized.
	 */

	if ( (ax_key = ftok(vol_fl_nm, ax_key_id)) == -1 ) {
	    fprintf(stderr, "%s %s: could not get memory key for volume %s.\n"
		    "%s\n", argv0, argv1, vol_fl_nm, strerror(errno));
	    goto error;
	}
	flags = S_IWUSR | IPC_CREAT | IPC_EXCL;
	if ( (ax_sem_id = semget(ax_key, 1, flags)) == -1 ) {
	    fprintf(stderr, "%s %s: could not create access semaphore for "
		    "volume %s.\n%s\n", argv0, argv1, vol_fl_nm,
		    strerror(errno));
	    goto error;
	}
	arg.val = 1;
	if ( semctl(ax_sem_id, 0, SETVAL, arg) == -1 ) {
	    fprintf(stderr, "%s %s: could not initialize access semaphore for "
		    "volume %s.\n%s\n", argv0, argv1, vol_fl_nm,
		    strerror(errno));
	    goto error;
	}
	buf.sem_perm.uid = getuid();
	buf.sem_perm.gid = getgid();
	buf.sem_perm.mode = S_IRUSR | S_IWUSR;
	arg.buf = &buf;
	if ( semctl(ax_sem_id, 0, IPC_SET, arg) == -1 ) {
	    fprintf(stderr, "%s %s: could not set permissions for access "
		    "semaphore for volume %s.\n%s\n", argv0, argv1, vol_fl_nm,
		    strerror(errno));
	    goto error;
	}
    } else if ( shm_id == -1 && errno == EEXIST ) {
	/*
	   shmget has returned an identifier for previously allocated shared
	   memory. Assume the volume and associated data have been or are being
	   loaded by another daemon.  Wait for the other daemon to finish
	   creating and intializing the access semaphore. The other daemon
	   indicates this by making the access semaphore readable.
	 */

	printf("%s %s: attaching to %s in shared memory.\n",
		argv0, argv1, vol_fl_nm);

	if ( (ax_key = ftok(vol_fl_nm, ax_key_id)) == -1 ) {
	    fprintf(stderr, "%s %s: could not get memory key for previously "
		    "loaded volume %s.\n%s\n", argv0, argv1, vol_fl_nm,
		    strerror(errno));
	    goto error;
	}
	flags = S_IRUSR | S_IWUSR;
	while ( (ax_sem_id = semget(ax_key, 1, flags)) == -1 ) {
	    if ( errno == ENOENT || errno == EACCES ) {
		printf("Waiting for semaphore.\n");
		sleep(1);
	    } else {
		fprintf(stderr, "%s %s: could not get access semaphore "
			"for previously loaded volume %s.\n%s\n",
			argv0, argv1, vol_fl_nm, strerror(errno));
		goto error;
	    }
	}

	/*
	   Increment the volume user count. Stay attached to the
	   volume's shared memory. Will detach after child exits.
	 */

	sop.sem_num = 0;
	sop.sem_op = -1;
	sop.sem_flg = 0;
	if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	    fprintf(stderr, "Could not adjust volume semaphore %d by -1\n"
		    "%s\n", ax_sem_id, strerror(errno));
	    goto error;
	}
	flags = S_IRUSR | S_IWUSR;
	shm_id = shmget(mem_key, sizeof(struct Sigmet_Vol), flags);
	if ( shm_id == -1 ) {
	    fprintf(stderr, "%s %s: could not attach to volume %s in shared "
		    "memory.\n%s\n", argv0, argv1, vol_fl_nm, strerror(errno));
	    goto error;
	}
	vol_p = shmat(shm_id, NULL, 0);
	if ( vol_p == (void *)-1) {
	    fprintf(stderr, "Could not attach to volume in shared "
		    "memory.\n%s\n", strerror(errno));
	    goto error;
	}
	vol_p->num_users++;
	sop.sem_op = 1;
	if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	    fprintf(stderr, "Could not restore volume semaphore %d by 1\n"
		    "%s\n", ax_sem_id, strerror(errno));
	}

    } else {
	fprintf(stderr, "%s %s: could not allocate or identify volume in "
		"shared memory.\n" "%s\n", argv0, argv1, strerror(errno));
	goto error;
    }

    /*
       Update environment so child command can locate and use the volume in
       shared memory. Spawn the child command specified on the command line.
     */

    if ( snprintf(shmid_s, LEN, SIGMET_VOL_SHMEM "=%d", shm_id) > LEN ) {
	fprintf(stderr, "%s %s: could not create environment variable for "
		"volume shared memory identifier.\n", argv0, argv1);
	goto error;
    }
    if ( putenv(shmid_s) != 0 ) {
	fprintf(stderr, "%s %s: could not put shared memory identifier for "
		"volume into environment.\n%s\n",
		argv0, argv1, strerror(errno));
	goto error;
    }
    if ( snprintf(ax_sem_id_s, LEN, SIGMET_VOL_SEM "=%d", ax_sem_id) > LEN ) {
	fprintf(stderr, "%s %s: could not create environment variable for "
		"volume semaphore identifier.\n", argv0, argv1);
	goto error;
    }
    if ( putenv(ax_sem_id_s) != 0 ) {
	fprintf(stderr, "%s %s: could not put semaphore identifier for "
		"volume into environment.\n%s\n",
		argv0, argv1, strerror(errno));
	goto error;
    }
    fprintf(stderr, "%s %s: spawning: ", argv0, argv1);
    for (a = argv + 3; *a; a++) {
	fprintf(stderr, "%s ", *a);
    }
    fprintf(stderr, "\n");

    ch_pid = fork();
    if ( ch_pid == -1 ) {
	fprintf(stderr, "%s %s: could not fork\n%s\n",
		argv0, argv1, strerror(errno));
	goto error;
    } else if ( ch_pid == 0 ) {
	execvp(argv[3], argv + 3);
	fprintf(stderr, "%s %s: failed to execute child process.\n%s\n",
		argv0, argv1, strerror(errno));
	exit(EXIT_FAILURE);
    }

    /*
       Wait for child to exit.
     */

    waitpid(ch_pid, &ch_stat, 0);
    if ( WIFEXITED(ch_stat) ) {
	FILE *msg_out;			/* Where to put final message */

	status = WEXITSTATUS(ch_stat);
	msg_out = (status == EXIT_SUCCESS) ? stdout : stderr;
	fprintf(msg_out, "%s: ", argv0);
	for (a = argv + 3; *a; a++) {
	    fprintf(msg_out, "%s ", *a);
	}
	fprintf(msg_out, "exited with status %d\n", status);
	fprintf(msg_out, "%s: exiting.\n", argv0);
    } else if ( WIFSIGNALED(ch_stat) ) {
	fprintf(stderr, "%s: child process exited on signal %d\n",
		argv0, WTERMSIG(ch_stat));
	fprintf(stderr, "%s: exiting.\n", argv0);
	status = EXIT_FAILURE;
    }

    /*
       Decrement user count. If volume has no users, unload it.
     */

    sop.sem_num = 0;
    sop.sem_op = -1;
    sop.sem_flg = 0;
    if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	fprintf(stderr, "Could not adjust volume semaphore %d by -1\n"
		"%s\n", ax_sem_id, strerror(errno));
	goto error;
    }
    vol_p->num_users--;
    if ( (ncnt = semctl(ax_sem_id, 0, GETNCNT)) == -1 ) {
	fprintf(stderr, "%s %s: could not determine number of processes "
		"waiting for access to semaphore %d for volume %s. Unable "
		"to free volume semaphores and shared memory. Please "
		"check semaphores and shared memory with ipcs and ipcrm.\n%s\n",
		argv0, argv1, ax_sem_id, vol_fl_nm, strerror(errno));
	goto error;
    }
    if ( vol_p->num_users == 0 && ncnt == 0 ) {
	printf("%s %s: volume no longer in use. Unloading.\n", argv0, argv1);
	if ( semctl(ax_sem_id, 0, IPC_RMID) == -1 ) {
	    fprintf(stderr, "%s %s: could not remove semaphore for "
		    "volume.\n%s\nPlease use ipcrm command for id %d\n",
		    argv0, argv1, strerror(errno), ax_sem_id);
	    status = 0;
	}
	if ( !Sigmet_Vol_Free(vol_p) ) {
	    fprintf(stderr, "%s %s: could not free memory for volume.\n",
		    argv0, argv1);
	    status = 0;
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "%s %s: could not detach shared memory for "
		    "volume.\n%s\n", argv0, argv1, strerror(errno));
	    status = 0;
	}
	if ( shmctl(shm_id, IPC_RMID, NULL) == -1 ) {
	    fprintf(stderr, "%s %s: could not remove shared memory for "
		    "volume.\n%s\nPlease use ipcrm command for id %d\n",
		    argv0, argv1, strerror(errno), shm_id);
	    status = 0;
	}
	return status;
    } else {
	printf("%s %s: volume still has %d user%s. Leaving volume loaded "
		"in shared memory.\n", argv0, argv1, vol_p->num_users,
		(vol_p->num_users > 1) ? "s" : "");
    }
    if ( shmdt(vol_p) == -1 ) {
	fprintf(stderr, "%s %s: could not detach shared memory for "
		"volume.\n%s\n", argv0, argv1, strerror(errno));
	status = 0;
    }
    sop.sem_op = 1;
    if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	fprintf(stderr, "Could not restore volume semaphore %d by 1\n"
		"%s\n", ax_sem_id, strerror(errno));
    }

    return status;

error:
    if ( vol_p && vol_p != (void *)-1 ) {
	if ( !Sigmet_Vol_Free(vol_p) ) {
	    fprintf(stderr, "%s %s: could not free memory for volume.\n",
		    argv0, argv1);
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "%s %s: could not detach shared memory for "
		    "volume.\n%s\n", argv0, argv1, strerror(errno));
	}
    }
    if ( shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1 ) {
	fprintf(stderr, "%s %s: could not remove shared memory for "
		"volume.\n%s\nPlease use ipcrm command for id %d\n",
		argv0, argv1, strerror(errno), shm_id);
    }
    if ( ax_sem_id != -1 && semctl(ax_sem_id, 0, IPC_RMID) == -1 ) {
	fprintf(stderr, "%s %s: could not remove semaphore for "
		"volume.\n%s\nPlease use ipcrm command for id %d\n",
		argv0, argv1, strerror(errno), ax_sem_id);
    }
    return 0;
}

/*
   Return a pointer in current process address space to the Sigmet volume in
   shared memory provided by the parent daemon. Return NULL if something goes
   wrong. Copy volume semaphore identifier to ax_sem_id_p.
 */

static struct Sigmet_Vol *vol_attach(int *sem_id_p)
{
    struct Sigmet_Vol *vol_p = NULL;
    int ax_sem_id, shm_id;		/* IPC semaphore and shared memory
					   identifiers */
    char *sem_id_s, *shm_id_s;		/* String representations of ax_sem_id
					   and shm_id */
    struct sembuf sop;			/* Semaphore operation */

    /*
       Decrement the semaphore
     */

    if ( !(sem_id_s = getenv(SIGMET_VOL_SEM))
	    || sscanf(sem_id_s, "%d", &ax_sem_id) != 1 ) {
	fprintf(stderr, "Could not identify volume semaphore from "
		SIGMET_VOL_SEM " environment variable.\n");
	return NULL;
    }
    sop.sem_num = 0;
    sop.sem_op = -1;
    sop.sem_flg = SEM_UNDO;
    if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	fprintf(stderr, "Could not adjust volume semaphore %d by -1\n"
		"%s\n", ax_sem_id, strerror(errno));
	goto error;
    }

    /*
       Locate volume in shared memory and attach to it.
     */

    if ( !(shm_id_s = getenv(SIGMET_VOL_SHMEM))
	    || sscanf(shm_id_s, "%d", &shm_id) != 1 ) {
	fprintf(stderr, "Could not identify volume shared memory identifier "
		"from " SIGMET_VOL_SEM " environment variable.\n");
	goto error;
    }
    vol_p = shmat(shm_id, NULL, 0);
    if ( vol_p == (void *)-1) {
	fprintf(stderr, "Could not attach to volume in shared memory.\n%s\n",
		strerror(errno));
	goto error;
    }
    if ( !Sigmet_ShMemAttach(vol_p) ) {
	fprintf(stderr, "Could not attach to volume contents "
		"in shared memory.\n");
	goto error;
    }

    *sem_id_p = ax_sem_id;
    return vol_p;

error:
    sop.sem_num = 0;
    sop.sem_op = 1;
    sop.sem_flg = SEM_UNDO;
    if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	fprintf(stderr, "Could not adjust volume semaphore %d by 1\n"
		"%s\n", ax_sem_id, strerror(errno));
    }
    if ( vol_p ) {
	if ( !Sigmet_ShMemDetach(vol_p) ) {
	    fprintf(stderr, "Could not detach from volume contents "
		    "in shared memory.\n");
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "Could not detach from volume in shared memory.\n"
		    "%s\n", strerror(errno));
	}
    }
    return NULL;
}

static void vol_detach(struct Sigmet_Vol *vol_p, int ax_sem_id)
{
    struct sembuf sop;

    sop.sem_num = 0;
    sop.sem_op = 1;
    sop.sem_flg = SEM_UNDO;
    if ( semop(ax_sem_id, &sop, 1) == -1 ) {
	fprintf(stderr, "Could not restore volume semaphore %d by 1\n"
		"%s\n", ax_sem_id, strerror(errno));
    }
    if ( vol_p ) {
	if ( !Sigmet_ShMemDetach(vol_p) ) {
	    fprintf(stderr, "Could not detach from volume contents "
		    "in shared memory.\n");
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "Could not detach from volume in shared memory.\n"
		    "%s\n", strerror(errno));
	}
    }
}

static int data_types_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;

    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (y = 0; y < vol_p->num_types; y++) {
	if ( strlen(vol_p->dat[y].abbrv) > 0 ) {
	    printf("%s | %s | %s\n", vol_p->dat[y].abbrv,
		    vol_p->dat[y].descr, vol_p->dat[y].unit);
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int volume_headers_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    Sigmet_Vol_PrintHdr(stdout, vol_p);
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int vol_hdr_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int y;
    double wavlen, prf, vel_ua;
    enum Sigmet_Multi_PRF mp;
    char *mp_s = "unknown";
    double l;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    printf("site_name=\"%s\"\n", vol_p->ih.ic.su_site_name);
    l = GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.longitude), 0.0) * DEG_PER_RAD;
    printf("radar_lon=%.4lf\n", l);
    l = GeogLonR(Sigmet_Bin4Rad(vol_p->ih.ic.latitude), 0.0) * DEG_PER_RAD;
    printf("radar_lat=%.4lf\n", l);
    switch (vol_p->ih.tc.tni.scan_mode) {
	case PPI_S:
	    printf("scan_mode=\"ppi sector\"\n");
	    break;
	case RHI:
	    printf("scan_mode=rhi\n");
	    break;
	case MAN_SCAN:
	    printf("scan_mode=manual\n");
	    break;
	case PPI_C:
	    printf("scan_mode=\"ppi continuous\"\n");
	    break;
	case FILE_SCAN:
	    printf("scan_mode=file\n");
	    break;
    }
    printf("task_name=\"%s\"\n", vol_p->ph.pc.task_name);
    printf("types=\"");
    printf("%s", vol_p->dat[0].abbrv);
    for (y = 1; y < vol_p->num_types; y++) {
	printf(" %s", vol_p->dat[y].abbrv);
    }
    printf("\"\n");
    printf("num_sweeps=%d\n", vol_p->ih.ic.num_sweeps);
    printf("num_rays=%d\n", vol_p->ih.ic.num_rays);
    printf("num_bins=%d\n", vol_p->ih.tc.tri.num_bins_out);
    printf("range_bin0=%d\n", vol_p->ih.tc.tri.rng_1st_bin);
    printf("bin_step=%d\n", vol_p->ih.tc.tri.step_out);
    wavlen = 0.01 * 0.01 * vol_p->ih.tc.tmi.wave_len; 	/* convert -> cm > m */
    prf = vol_p->ih.tc.tdi.prf;
    mp = vol_p->ih.tc.tdi.m_prf_mode;
    vol_detach(vol_p, ax_sem_id);
    vel_ua = -1.0;
    switch (mp) {
	case ONE_ONE:
	    mp_s = "1:1";
	    vel_ua = 0.25 * wavlen * prf;
	    break;
	case TWO_THREE:
	    mp_s = "2:3";
	    vel_ua = 2 * 0.25 * wavlen * prf;
	    break;
	case THREE_FOUR:
	    mp_s = "3:4";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
	case FOUR_FIVE:
	    mp_s = "4:5";
	    vel_ua = 3 * 0.25 * wavlen * prf;
	    break;
    }
    printf("prf=%.2lf\n", prf);
    printf("prf_mode=%s\n", mp_s);
    printf("vel_ua=%.3lf\n", vel_ua);
    return 1;
}

static int near_sweep_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;		/* Sweep angle, degrees */
    double ang;			/* Angle from command line */
    double da_min;		/* Angle difference, smallest difference */
    int s, nrst;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s angle\n", argv0, argv1);
	return 0;
    }
    ang_s = argv[2];
    if ( sscanf(ang_s, "%lf", &ang) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point for sweep angle,"
		" got %s\n", argv0, argv1, ang_s);
	return 0;
    }
    ang *= RAD_PER_DEG;
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( !vol_p->sweep_hdr ) {
	fprintf(stderr, "%s %s: sweep headers not loaded. "
		"Is volume truncated?.\n", argv0, argv1);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    nrst = -1;
    for (da_min = DBL_MAX, s = 0; s < vol_p->num_sweeps_ax; s++) {
	double swang, da;	/* Sweep angle, angle difference */

	swang = GeogLonR(vol_p->sweep_hdr[s].angle, ang);
	da = fabs(swang - ang);
	if ( da < da_min ) {
	    da_min = da;
	    nrst = s;
	}
    }
    vol_detach(vol_p, ax_sem_id);
    printf("%d\n", nrst);
    return 1;
}

static int sweep_headers_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (s = 0; s < vol_p->ih.tc.tni.num_sweeps; s++) {
	printf("sweep %2d ", s);
	if ( !vol_p->sweep_hdr[s].ok ) {
	    printf("bad\n");
	} else {
	    int yr, mon, da, hr, min, sec;

	    if ( Tm_JulToCal(vol_p->sweep_hdr[s].time,
			&yr, &mon, &da, &hr, &min, &sec) ) {
		printf("%04d/%02d/%02d %02d:%02d:%02d ",
			yr, mon, da, hr, min, sec);
	    } else {
		printf("0000/00/00 00:00:00 ");
	    }
	    printf("%7.3f\n", vol_p->sweep_hdr[s].angle * DEG_PER_RAD);
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int ray_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int s, r;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	if ( vol_p->sweep_hdr[s].ok ) {
	    for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		int yr, mon, da, hr, min, sec;

		if ( !vol_p->ray_hdr[s][r].ok ) {
		    continue;
		}
		printf("sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(vol_p->ray_hdr[s][r].time,
			    &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(stderr, "%s %s: bad ray time\n",
			    argv0, argv1);
		    vol_detach(vol_p, ax_sem_id);
		    return 0;
		}
		printf("%04d/%02d/%02d %02d:%02d:%02d | ",
			yr, mon, da, hr, min, sec);
		printf("az %7.3f %7.3f | ",
			vol_p->ray_hdr[s][r].az0 * DEG_PER_RAD,
			vol_p->ray_hdr[s][r].az1 * DEG_PER_RAD);
		printf("tilt %6.3f %6.3f\n",
			vol_p->ray_hdr[s][r].tilt0 * DEG_PER_RAD,
			vol_p->ray_hdr[s][r].tilt1 * DEG_PER_RAD);
	    }
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int new_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int a;
    char *abbrv;			/* Data type abbreviation */
    char *val_s = NULL;			/* Initial value */
    double val;
    char *descr = NULL;			/* Descriptor for new field */
    char *unit = NULL;			/* Unit for new field */
    int status;				/* Result of a function */

    /*
       Identify data type from command line. Fail if volume already has
       this data type.
     */

    if ( argc < 3 || argc > 9 ) {
	fprintf(stderr, "Usage: %s %s data_type [-d description] [-u unit] "
		"[-v value]\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];

    /*
       Obtain optional descriptor, units, and initial value, or use defaults.
     */

    for (a = 3; a < argc; a++) {
	if (strcmp(argv[a], "-d") == 0) {
	    descr = argv[++a];
	} else if (strcmp(argv[a], "-u") == 0) {
	    unit = argv[++a];
	} else if (strcmp(argv[a], "-v") == 0) {
	    val_s = argv[++a];
	} else {
	    fprintf(stderr, "%s %s: unknown option %s.\n",
		    argv0, argv1, argv[a]);
	    return 0;
	}
    }
    if ( !descr || strlen(descr) == 0 ) {
	descr = "No description";
    }
    if ( !unit || strlen(unit) == 0 ) {
	unit = "Dimensionless";
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (status = Sigmet_Vol_NewField(vol_p, abbrv, descr, unit))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add data type %s to volume\n%s\n",
		argv0, argv1, abbrv, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }

    /*
       If given optional value, initialize new field with it.
     */

    if ( val_s ) {
	if ( sscanf(val_s, "%lf", &val) == 1 ) {
	    status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, val);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val, sigmet_err(status));
		vol_detach(vol_p, ax_sem_id);
		return 0;
	    }
	} else if ( strcmp(val_s, "r_beam") == 0 ) {
	    status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val_s, sigmet_err(status));
		vol_detach(vol_p, ax_sem_id);
		return 0;
	    }
	} else {
	    status = Sigmet_Vol_Fld_Copy(vol_p, abbrv, val_s);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, abbrv, val_s, sigmet_err(status));
		vol_detach(vol_p, ax_sem_id);
		return 0;
	    }
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int del_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *abbrv;			/* Data type abbreviation */
    int status;				/* Result of a function */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (status = Sigmet_Vol_DelField(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not remove data type %s "
		"from volume\n%s\n", argv0, argv1, abbrv, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Print volume memory usage.
 */

static int size_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    printf("%lu\n", (unsigned long)vol_p->size);
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Set value for a field.
 */

static int set_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value\n", argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    d_s = argv[3];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }

    /*
       Parse value and set in data array.
       "r_beam" => set bin value to distance along bin, in meters.
       Otherwise, value must be a floating point number.
     */

    if ( strcmp("r_beam", d_s) == 0 ) {
	if ( (status = Sigmet_Vol_Fld_SetRBeam(vol_p, abbrv)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to beam range "
		    "in volume\n%s\n", argv0, argv1, abbrv, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SetVal(vol_p, abbrv, d)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n",
		    argv0, argv1, abbrv, d, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else {
	fprintf(stderr, "%s %s: field value must be a number or \"r_beam\"\n",
		argv0, argv1);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Add a scalar or another field to a field.
 */

static int add_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_AddVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not add %s to %lf in volume\n%s\n",
		    argv0, argv1, abbrv, a, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else if ( (status = Sigmet_Vol_Fld_AddFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add %s to %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Subtract a scalar or another field from a field.
 */

static int sub_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_SubVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not subtract %lf from %s in "
		    "volume\n%s\n", argv0, argv1, a, abbrv, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else if ( (status = Sigmet_Vol_Fld_SubFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not subtract %s from %s in volume\n%s\n",
		argv0, argv1, a_s, abbrv, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Multiply a field by a scalar or another field
 */

static int mul_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s type value|field\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_MulVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not multiply %s by %lf in "
		    "volume\n%s\n", argv0, argv1, abbrv, a, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else if ( (status = Sigmet_Vol_Fld_MulFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not multiply %s by %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Divide a field by a scalar or another field
 */

static int div_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    a_s = argv[3];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	if ( (status = Sigmet_Vol_Fld_DivVal(vol_p, abbrv, a)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not divide %s by %lf in volume\n%s\n",
		    argv0, argv1, abbrv, a, sigmet_err(status));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    } else if ( (status = Sigmet_Vol_Fld_DivFld(vol_p, abbrv, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not divide %s by %s in volume\n%s\n",
		argv0, argv1, abbrv, a_s, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Replace a field with it's log10.
 */

static int log10_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *abbrv;			/* Data type abbreviation */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (status = Sigmet_Vol_Fld_Log10(vol_p, abbrv)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute log10 of %s in volume\n%s\n",
		argv0, argv1, abbrv, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int incr_time_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int status;
    int ax_sem_id;
    char *dt_s;
    double dt;				/* Time increment, seconds */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dt\n", argv0, argv1);
	return 0;
    }
    dt_s = argv[2];
    if ( sscanf(dt_s, "%lf", &dt) != 1) {
	fprintf(stderr, "%s %s: expected float value for time increment, got "
		"%s\n", argv0, argv1, dt_s);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( (status = Sigmet_Vol_IncrTm(vol_p, dt / 86400.0)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not increment time in volume\n%s\n",
		argv0, argv1, sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int data_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int s, y, r, b;
    char *abbrv;
    double d;
    int all = -1;

    /*
       Identify input and desired output
       Possible forms:
       sigmet_ray data			(argc = 3)
       sigmet_ray data data_type		(argc = 4)
       sigmet_ray data data_type s		(argc = 5)
       sigmet_ray data data_type s r	(argc = 6)
       sigmet_ray data data_type s r b	(argc = 7)
     */

    abbrv = NULL;
    y = s = r = b = all;
    if ( argc >= 3 ) {
	abbrv = argv[2];
    }
    if ( argc >= 4 && sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return 0;
    }
    if ( argc >= 5 && sscanf(argv[4], "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, argv[4]);
	return 0;
    }
    if ( argc >= 6 && sscanf(argv[5], "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, argv[5]);
	return 0;
    }
    if ( argc >= 7 ) {
	fprintf(stderr, "Usage: %s %s [[[[data_type] sweep] ray] bin]\n",
		argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }

    /*
       Validate.
     */

    if ( abbrv ) {
	for (y = 0;
		y < vol_p->num_types && strcmp(vol_p->dat[y].abbrv, abbrv) != 0;
		y++) {
	}
	if ( y == vol_p->num_types ) {
	    fprintf(stderr, "%s %s: no data type named %s\n",
		    argv0, argv1, abbrv);
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    }
    if ( s != all && s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( r != all && r >= (int)vol_p->ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( b != all && b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < vol_p->num_types; y++) {
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		printf("%s. sweep %d\n", vol_p->dat[y].abbrv, s);
		for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
		    if ( !vol_p->ray_hdr[s][r].ok ) {
			continue;
		    }
		    printf("ray %d: ", r);
		    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
			d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
			if ( Sigmet_IsData(d) ) {
			    printf("%f ", d);
			} else {
			    printf("nodat ");
			}
		    }
		    printf("\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    printf("%s. sweep %d\n", abbrv, s);
	    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
		if ( !vol_p->ray_hdr[s][r].ok ) {
		    continue;
		}
		printf("ray %d: ", r);
		for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		    if ( Sigmet_IsData(d) ) {
			printf("%f ", d);
		    } else {
			printf("nodat ");
		    }
		}
		printf("\n");
	    }
	}
    } else if ( r == all && b == all ) {
	printf("%s. sweep %d\n", abbrv, s);
	for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	    if ( !vol_p->ray_hdr[s][r].ok ) {
		continue;
	    }
	    printf("ray %d: ", r);
	    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else if ( b == all ) {
	if ( vol_p->ray_hdr[s][r].ok ) {
	    printf("%s. sweep %d, ray %d: ", abbrv, s, r);
	    for (b = 0; b < vol_p->ray_hdr[s][r].num_bins; b++) {
		d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
		if ( Sigmet_IsData(d) ) {
		    printf("%f ", d);
		} else {
		    printf("nodat ");
		}
	    }
	    printf("\n");
	}
    } else {
	if ( vol_p->ray_hdr[s][r].ok ) {
	    printf("%s. sweep %d, ray %d, bin %d: ", abbrv, s, r, b);
	    d = Sigmet_Vol_GetDat(vol_p, y, s, r, b);
	    if ( Sigmet_IsData(d) ) {
		printf("%f ", d);
	    } else {
		printf("nodat ");
	    }
	    printf("\n");
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

/*
   Print sweep data as a binary stream.
   sigmet_ray bdata data_type s
   Each output ray will have num_output_bins floats.
   Missing values will be Sigmet_NoData().
 */

static int bdata_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int s, y, r, b;
    char *abbrv;
    static float *ray_p;	/* Buffer to receive ray data */
    int num_bins_out;
    int status, n;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type sweep_index\n",
		argv0, argv1);
	return 0;
    }
    abbrv = argv[2];
    if ( sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (y = 0;
	    y < vol_p->num_types && strcmp(vol_p->dat[y].abbrv, abbrv) != 0;
	    y++) {
    }
    if ( y == vol_p->num_types ) {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, abbrv);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    n = num_bins_out = vol_p->ih.tc.tri.num_bins_out;
    if ( !ray_p && !(ray_p = CALLOC(num_bins_out, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate output buffer for ray.\n");
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    for (r = 0; r < vol_p->ih.ic.num_rays; r++) {
	for (b = 0; b < num_bins_out; b++) {
	    ray_p[b] = Sigmet_NoData();
	}
	if ( vol_p->ray_hdr[s][r].ok ) {
	    status = Sigmet_Vol_GetRayDat(vol_p, y, s, r, &ray_p, &n);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "Could not get ray data for data type %s, "
			"sweep index %d, ray %d.\n%s\n", abbrv, s, r,
			sigmet_err(status));
		vol_detach(vol_p, ax_sem_id);
		return 0;
	    }
	    if ( n > num_bins_out ) {
		fprintf(stderr, "Ray %d or sweep %d, data type %s has "
			"unexpected number of bins - %d instead of %d.\n",
			r, s, abbrv, n, num_bins_out);
		vol_detach(vol_p, ax_sem_id);
		return 0;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins_out, stdout)
		!= num_bins_out ) {
	    fprintf(stderr, "Could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n",
		    abbrv, s, r, strerror(errno));
	    vol_detach(vol_p, ax_sem_id);
	    return 0;
	}
    }
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int bin_outline_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Result of a function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double corners[8];

    if ( argc != 5 ) {
	fprintf(stderr, "Usage: %s %s sweep ray bin\n", argv0, argv1);
	return 0;
    }
    s_s = argv[2];
    r_s = argv[3];
    b_s = argv[4];

    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    if ( sscanf(r_s, "%d", &r) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for ray index, got %s\n",
		argv0, argv1, r_s);
	return 0;
    }
    if ( sscanf(b_s, "%d", &b) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for bin index, got %s\n",
		argv0, argv1, b_s);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( r >= vol_p->ih.ic.num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( b >= vol_p->ih.tc.tri.num_bins_out ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( (status = Sigmet_Vol_BinOutl(vol_p, s, r, b, corners)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute bin outlines for bin "
		"%d %d %d in volume\n%s\n", argv0, argv1, s, r, b,
		sigmet_err(status));
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    vol_detach(vol_p, ax_sem_id);
    printf("%f %f %f %f %f %f %f %f\n",
	    corners[0] * DEG_RAD, corners[1] * DEG_RAD,
	    corners[2] * DEG_RAD, corners[3] * DEG_RAD,
	    corners[4] * DEG_RAD, corners[5] * DEG_RAD,
	    corners[6] * DEG_RAD, corners[7] * DEG_RAD);

    return 1;
}

static int radar_lon_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *lon_s;			/* New longitude, degrees, in argv */
    double lon;				/* New longitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lon\n", argv0, argv1);
	return 0;
    }
    lon_s = argv[2];
    if ( sscanf(lon_s, "%lf", &lon) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for "
		"new longitude, got %s\n", argv0, argv1, lon_s);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.longitude = Sigmet_RadBin4(lon);
    vol_p->mod = 1;
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int radar_lat_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *lat_s;			/* New latitude, degrees, in argv */
    double lat;				/* New latitude, degrees */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s new_lat\n", argv0, argv1);
	return 0;
    }
    lat_s = argv[2];
    if ( sscanf(lat_s, "%lf", &lat) != 1 ) {
	fprintf(stderr, "%s %s: expected floating point value for "
		"new latitude, got %s\n", argv0, argv1, lat_s);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    lat = GeogLonR(lat * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    vol_p->ih.ic.latitude = Sigmet_RadBin4(lat);
    vol_p->mod = 1;
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int shift_az_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *daz_s;			/* Degrees to add to each azimuth */
    double daz;				/* Radians to add to each azimuth */
    unsigned long idaz;			/* Binary angle to add to each
					   azimuth */
    int s, r;				/* Loop indeces */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s dz\n", argv0, argv1);
	return 0;
    }
    daz_s = argv[2];
    if ( sscanf(daz_s, "%lf", &daz) != 1 ) {
	fprintf(stderr, "%s %s: expected float value for azimuth shift, "
		"got %s\n", argv0, argv1, daz_s);
	return 0;
    }
    daz = GeogLonR(daz * RAD_PER_DEG, 180.0 * RAD_PER_DEG);
    idaz = Sigmet_RadBin4(daz);
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.rhi_info.az[s] += idaz;
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
		vol_p->ih.tc.tni.scan_info.ppi_info.left_az += idaz;
		vol_p->ih.tc.tni.scan_info.ppi_info.right_az += idaz;
	    }
	    break;
	case FILE_SCAN:
	    vol_p->ih.tc.tni.scan_info.file_info.az0 += idaz;
	case MAN_SCAN:
	    break;
    }
    for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	for (r = 0; r < (int)vol_p->ih.ic.num_rays; r++) {
	    vol_p->ray_hdr[s][r].az0
		= GeogLonR(vol_p->ray_hdr[s][r].az0 + daz, 180.0 * RAD_PER_DEG);
	    vol_p->ray_hdr[s][r].az1
		= GeogLonR(vol_p->ray_hdr[s][r].az1 + daz, 180.0 * RAD_PER_DEG);
	}
    }
    vol_p->mod = 1;
    vol_detach(vol_p, ax_sem_id);
    return 1;
}

static int outlines_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int status;				/* Return value of this function */
    int bnr;				/* If true, send raw binary output */
    int fill;				/* If true, fill space between rays */
    char *s_s;				/* Sweep index, as a string */
    char *abbrv;			/* Data type abbreviation */
    char *min_s, *max_s;		/* Bounds of data interval of interest
					 */
    char *outlnFlNm;			/* Name of output file */
    FILE *outlnFl;			/* Output file */
    int s;				/* Sweep index */
    double min, max;			/* Bounds of data interval of interest
					 */
    int c;				/* Return value from getopt */
    extern char *optarg;
    extern int opterr, optind, optopt;	/* See getopt (3) */

    if ( argc < 7 ) {
	fprintf(stderr, "Usage: %s %s [-f] [-b] data_type sweep min max "
		"out_file\n", argv0, argv1);
	return 0;
    }
    for (bnr = fill = 0, opterr = 0, optind = 1;
	    (c = getopt(argc - 1, argv + 1, "bf")) != -1; ) {
	switch(c) {
	    case 'b':
		bnr = 1;
		break;
	    case 'f':
		fill = 1;
		break;
	    case '?':
		fprintf(stderr, "%s %s: unknown option \"-%c\"\n",
			argv0, argv1, optopt);
		return 0;
	}
    }
    abbrv = argv[argc - 5];
    s_s = argv[argc - 4];
    min_s = argv[argc - 3];
    max_s = argv[argc - 2];
    outlnFlNm = argv[argc - 1];
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }
    if ( strcmp(min_s, "-inf") == 0 || strcmp(min_s, "-INF") == 0 ) {
	min = -DBL_MAX;
    } else if ( sscanf(min_s, "%lf", &min) != 1 ) {
	fprintf(stderr, "%s %s: expected float value or -INF for data min,"
		" got %s\n", argv0, argv1, min_s);
	return 0;
    }
    if ( strcmp(max_s, "inf") == 0 || strcmp(max_s, "INF") == 0 ) {
	max = DBL_MAX;
    } else if ( sscanf(max_s, "%lf", &max) != 1 ) {
	fprintf(stderr, "%s %s: expected float value or INF for data max,"
		" got %s\n", argv0, argv1, max_s);
	return 0;
    }
    if ( !(min < max)) {
	fprintf(stderr, "%s %s: minimum (%s) must be less than maximum (%s)\n",
		argv0, argv1, min_s, max_s);
	return 0;
    }
    if ( strcmp(outlnFlNm, "-") == 0 ) {
	outlnFl = stdout;
    } else if ( !(outlnFl = fopen(outlnFlNm, "w")) ) {
	fprintf(stderr, "%s %s: could not open %s for output.\n%s\n",
		argv0, argv1, outlnFlNm, strerror(errno));
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    switch (vol_p->ih.tc.tni.scan_mode) {
	case RHI:
	    status = Sigmet_Vol_RHI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    fill, outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n%s\n",
			argv0, argv1, abbrv, s, sigmet_err(status));
	    }
	    break;
	case PPI_S:
	case PPI_C:
	    status = Sigmet_Vol_PPI_Outlns(vol_p, abbrv, s, min, max, bnr,
		    outlnFl);
	    if ( status != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not print outlines for "
			"data type %s, sweep %d.\n%s\n",
			argv0, argv1, abbrv, s, sigmet_err(status));
	    }
	    break;
	case FILE_SCAN:
	case MAN_SCAN:
	    fprintf(stderr, "Can only print outlines for RHI and PPI.\n");
	    status = SIGMET_BAD_ARG;
	    break;
    }
    vol_detach(vol_p, ax_sem_id);
    if ( outlnFl != stdout ) {
	fclose(outlnFl);
    }
    return status == SIGMET_OK;
}

static int dorade_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int s;				/* Index of desired sweep,
					   or -1 for all */
    char *s_s;				/* String representation of s */
    int all = -1;
    int status;				/* Result of a function */
    struct Dorade_Sweep swp;

    if ( argc == 2 ) {
	s = all;
    } else if ( argc == 3 ) {
	s_s = argv[2];
	if ( strcmp(s_s, "all") == 0 ) {
	    s = all;
	} else if ( sscanf(s_s, "%d", &s) != 1 ) {
	    fprintf(stderr, "%s %s: expected integer for sweep index, got \"%s"
		    "\"\n", argv0, argv1, s_s);
	    return 0;
	}
    } else {
	fprintf(stderr, "Usage: %s %s [s]\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = vol_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( s >= vol_p->num_sweeps_ax ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	vol_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( s == all ) {
	for (s = 0; s < vol_p->num_sweeps_ax; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not translate sweep %d of volume "
			"to DORADE format\n%s\n", argv0, argv1, s,
			sigmet_err(status));
		goto error;
	    }
	    vol_detach(vol_p, ax_sem_id);
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(stderr, "%s %s: could not write DORADE file for sweep "
			"%d of volume\n%s\n", argv0, argv1, s,
			sigmet_err(status));
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (status = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not translate sweep %d of volume to "
		    "DORADE format\n%s\n", argv0, argv1, s, sigmet_err(status));
	    goto error;
	}
	vol_detach(vol_p, ax_sem_id);
	if ( !Dorade_Sweep_Write(&swp) ) {
	    fprintf(stderr, "%s %s: could not write DORADE file for sweep "
		    "%d of volume\n", argv0, argv1, s);
	    goto error;
	}
	Dorade_Sweep_Free(&swp);
    }

    return 1;

error:
    Dorade_Sweep_Free(&swp);
    return 0;
}

static char *sigmet_err(enum SigmetStatus s)
{
    switch (s) {
	case SIGMET_OK:
	    return "Success.";
	    break;
	case SIGMET_IO_FAIL:
	    return "Input/output failure.";
	    break;
	case SIGMET_BAD_FILE:
	    return "Bad file.";
	    break;
	case SIGMET_BAD_VOL:
	    return "Bad volume.";
	    break;
	case SIGMET_ALLOC_FAIL:
	    return "Allocation failure.";
	    break;
	case SIGMET_BAD_ARG:
	    return "Bad argument.";
	    break;
	case SIGMET_RNG_ERR:
	    return "Value out of range.";
	    break;
	case SIGMET_BAD_TIME:
	    return "Bad time.";
	    break;
	case SIGMET_HELPER_FAIL:
	    return "Helper application failed.";
	    break;
    }
    return "Unknown error";
}

/*
   Open volume file vol_nm.  If vol_nm suffix indicates a compressed file, open
   a pipe to a decompression process.  Return a file handle to the file or
   decompression process, or NULL if failure. If return value is output from a
   decompression process, put the process id at pid_p. Set *pid_p to -1 if
   there is no decompression process (i.e. vol_nm is a plain file).
 */

static FILE *vol_open(const char *vol_nm, pid_t *pid_p)
{
    FILE *in = NULL;		/* Return value */
    char *sfx;			/* Filename suffix */
    int pfd[2] = {-1};		/* Pipe for data */
    pid_t ch_pid = -1;		/* Child process id */

    *pid_p = -1;
    if ( strcmp(vol_nm, "-") == 0 ) {
	return stdin;
    }
    sfx = strrchr(vol_nm, '.');
    if ( sfx && sfx == vol_nm + strlen(vol_nm) - strlen(".gz")
	    && strcmp(sfx, ".gz") == 0 ) {
	if ( pipe(pfd) == -1 ) {
	    fprintf(stderr, "Could not create pipe for gzip\n%s\n",
		    strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(stderr, "Could not spawn gzip\n");
		goto error;
	    case 0: /* Child process - gzip */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    fprintf(stderr, "gzip process failed\n%s\n",
			    strerror(errno));
		    _exit(EXIT_FAILURE);
		}
		execlp("gunzip", "gunzip", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default: /* This process.  Read output from gzip. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(stderr, "Could not read gzip process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( sfx && sfx == vol_nm + strlen(vol_nm) - strlen(".bz2")
	    && strcmp(sfx, ".bz2") == 0 ) {
	if ( pipe(pfd) == -1 ) {
	    fprintf(stderr, "Could not create pipe for bzip2\n%s\n",
		    strerror(errno));
	    goto error;
	}
	ch_pid = fork();
	switch (ch_pid) {
	    case -1:
		fprintf(stderr, "Could not spawn bzip2\n");
		goto error;
	    case 0: /* Child process - bzip2 */
		if ( dup2(pfd[1], STDOUT_FILENO) == -1 || close(pfd[1]) == -1
			|| close(pfd[0]) == -1 ) {
		    fprintf(stderr, "could not set up bzip2 process");
		    _exit(EXIT_FAILURE);
		}
		execlp("bunzip2", "bunzip2", "-c", vol_nm, (char *)NULL);
		_exit(EXIT_FAILURE);
	    default: /* This process.  Read output from bzip2. */
		if ( close(pfd[1]) == -1 || !(in = fdopen(pfd[0], "r"))) {
		    fprintf(stderr, "Could not read bzip2 process\n%s\n",
			    strerror(errno));
		    goto error;
		} else {
		    *pid_p = ch_pid;
		    return in;
		}
	}
    } else if ( !(in = fopen(vol_nm, "r")) ) {
	fprintf(stderr, "Could not open %s\n%s\n", vol_nm, strerror(errno));
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

/*
   Basic signal management.

   Reference --
   Rochkind, Marc J., "Advanced UNIX Programming, Second Edition",
   2004, Addison-Wesley, Boston.
 */

int handle_signals(void)
{
    sigset_t set;
    struct sigaction act;

    if ( sigfillset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    memset(&act, 0, sizeof(struct sigaction));
    if ( sigfillset(&act.sa_mask) == -1 ) {
	perror(NULL);
	return 0;
    }

    /*
       Signals to ignore
     */

    act.sa_handler = SIG_IGN;
    if ( sigaction(SIGHUP, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGINT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGQUIT, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGPIPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    /*
       Generic action for termination signals
     */

    act.sa_handler = handler;
    if ( sigaction(SIGTERM, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGFPE, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGSYS, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXCPU, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigaction(SIGXFSZ, &act, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigemptyset(&set) == -1 ) {
	perror(NULL);
	return 0;
    }
    if ( sigprocmask(SIG_SETMASK, &set, NULL) == -1 ) {
	perror(NULL);
	return 0;
    }

    return 1;
}

/*
   For exit signals, print an error message if possible.
 */

void handler(int signum)
{
    char *msg;
    int status = EXIT_FAILURE;

    msg = "sigmet_raw command exiting                          \n";
    switch (signum) {
	case SIGQUIT:
	    msg = "sigmet_raw command exiting on quit signal           \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGTERM:
	    msg = "sigmet_raw command exiting on termination signal    \n";
	    status = EXIT_SUCCESS;
	    break;
	case SIGFPE:
	    msg = "sigmet_raw command exiting arithmetic exception     \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGSYS:
	    msg = "sigmet_raw command exiting on bad system call       \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXCPU:
	    msg = "sigmet_raw command exiting: CPU time limit exceeded \n";
	    status = EXIT_FAILURE;
	    break;
	case SIGXFSZ:
	    msg = "sigmet_raw command exiting: file size limit exceeded\n";
	    status = EXIT_FAILURE;
	    break;
    }
    _exit(write(STDERR_FILENO, msg, 53) == 53 ?  status : EXIT_FAILURE);
}
