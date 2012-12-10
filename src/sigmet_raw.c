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
   .	$Revision: 1.112 $ $Date: 2012/11/10 16:35:02 $
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
#include "bisearch_lib.h"
#include "geog_lib.h"
#include "sigmet.h"

/*
   Maximum number of characters allowed in a color name.
   COLOR_NM_LEN_A = storage size
   COLOR_NM_LEN_S = maximum number of non-nul characters.
 */

#define COLOR_NM_LEN_A 64
#define COLOR_NM_LEN_S "63"

/*
   Name of transparent color
 */

#define TRANSPARENT "transparent"

/*
   Maximum number of characters in the string representation of a float value
   FLOAT_STR_LEN_A = storage size
   FLOAT_STR_LEN_S = maximum number of non-nul characters.
 */

#define FLOAT_STR_LEN_A 64
#define FLOAT_STR_LEN_S "63"

/*
   Function to convert between longitude-latitude coordinates and map
   coordinates.
 */

static int (*lonlat_to_xy)(double, double, double *, double *)
    = Sigmet_Proj_LonLatToXY;

/*
   Default static string length
 */

#define LEN 255

/*
   Key identifier for Sigmet volume in shared memory.  ftok retrieves key to
   shared memory given volume file name and shm_key_id.
 */

static char shm_key_id = 'm';

/*
   To prevent multiple daemons or clients from simultaneously accessing
   a Sigmet volume in shared memory, this application uses a two state
   semaphore.  It is initialized to 1 when the volume loads. Callbacks
   defined below must decrement the semaphore before accessing
   the shared memory. They must post it back to 1 when done. ftok retrieves
   the key to this semaphore given the volume file name and ax_key_id.
 */

static char ax_key_id = 'a';

/*
   When a deamon loads or attaches to a volume in shared memory, it
   increments a user count semaphore before spawning its child.
   The daemon decrements the user count semaphore after the child exits.
   If the user count goes to 0, the daemon unloads the volume. ftok retrieves
   the key to this semaphore given the volume file name and usr_key_id.
 */

static char usr_key_id = 'u';

/*
   semop arguments used in various places
 */

static struct sembuf post_sop_struct = {
    0, 1, SEM_UNDO
};
static struct sembuf *post_sop = &post_sop_struct;

static struct sembuf wait_sop_struct = {
    0, -1, SEM_UNDO
};
static struct sembuf *wait_sop = &wait_sop_struct;

static struct sembuf use_sop_struct = {
    0, 1, SEM_UNDO
};
static struct sembuf *use_sop = &use_sop_struct;

static struct sembuf release_sop_struct = {
    0, -1, SEM_UNDO
};
static struct sembuf *release_sop = &release_sop_struct;

/*
   Local functions
 */

static FILE *vol_open(const char *, pid_t *);
static struct Sigmet_Vol *client_attach(int *);
static void client_detach(struct Sigmet_Vol *, int);
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
static callback sweep_bnds_cb;
static callback radar_lon_cb;
static callback radar_lat_cb;
static callback shift_az_cb;
static callback outlines_cb;
static callback dorade_cb;

/*
   Convenience functions
 */

static int load_new(char *, int, int *, int *, struct Sigmet_Vol *);
static int deamon_attach(char *, int *, int *, int *);
static int set_proj(struct Sigmet_Vol *);

/*
   Subcommand names and associated callbacks. The hash function defined
   below returns the index from cmd1v or cb1v corresponding to string argv1.

   Array should be sized for perfect hashing. Parser does not search buckets.

   Hashing function from Kernighan, Brian W. and Rob Pike, The Practice of
   Programming, Reading, Massachusetts. 1999
 */

#define N_HASH_CMD 126
static char *cmd1v[N_HASH_CMD] = {
    "", "", "", "outlines", "radar_lon", "", "", "",
    "", "near_sweep", "", "", "", "", "", "",
    "", "", "", "volume_headers", "shift_az", "", "", "",
    "", "", "", "add", "", "", "", "",
    "", "", "", "", "", "", "", "sweep_headers",
    "", "", "", "set_field", "", "", "", "",
    "bin_outline", "", "load", "", "", "dorade", "", "",
    "", "", "", "div", "", "", "vol_hdr", "",
    "del_field", "", "incr_time", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "radar_lat", "", "", "", "sub", "", "", "",
    "", "", "", "", "", "", "", "new_field",
    "", "ray_headers", "data", "", "", "", "data_types", "",
    "", "", "", "size", "", "", "version", "",
    "bdata", "log10", "", "", "", "", "sweep_bnds", "",
    "", "", "", "", "mul", "",
};
static callback *cb1v[N_HASH_CMD] = {
    NULL, NULL, NULL, outlines_cb, radar_lon_cb, NULL, NULL, NULL,
    NULL, near_sweep_cb, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, volume_headers_cb, shift_az_cb, NULL, NULL, NULL,
    NULL, NULL, NULL, add_cb, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, sweep_headers_cb,
    NULL, NULL, NULL, set_field_cb, NULL, NULL, NULL, NULL,
    bin_outline_cb, NULL, load_cb, NULL, NULL, dorade_cb, NULL, NULL,
    NULL, NULL, NULL, div_cb, NULL, NULL, vol_hdr_cb, NULL,
    del_field_cb, NULL, incr_time_cb, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    radar_lat_cb, NULL, NULL, NULL, sub_cb, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, new_field_cb,
    NULL, ray_headers_cb, data_cb, NULL, NULL, NULL, data_types_cb, NULL,
    NULL, NULL, NULL, size_cb, NULL, NULL, version_cb, NULL,
    bdata_cb, log10_cb, NULL, NULL, NULL, NULL, sweep_bnds_cb, NULL,
    NULL, NULL, NULL, NULL, mul_cb, NULL,
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
   Names of environment variables
 */

#define SIGMET_VOL_SHMEM "SIGMET_VOL_SHMEM"
#define SIGMET_VOL_SEM "SIGMET_VOL_SEM"
#define SIGMET_GEOG_PROJ "SIGMET_GEOG_PROJ"

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
    int fn_stat = 1;			/* Return value from this function */
    enum SigmetStatus sig_stat;		/* Result of a Sigmet function */
    char *vol_fl_nm;			/* Name of file with Sigmet raw
					   volume */
    struct Sigmet_Vol vol_sav;		/* If this is a load deamon, make a
					   small local copy of volume. Daemon
					   uses this to free the volume instead
					   of the volume in shared memory, in
					   case a buggy client has corrupted
					   the shared memory copy. */
    int loader = 0;			/* If true, this deamon loaded the
					   volume */
    pid_t ch_pid;			/* Process id for child process
					   specified on command line  */
    int ch_stat;			/* Exit status of child specified
					   on command line */

    int flags;				/* IPC flags for shmget and semget */
    key_t shm_key;			/* IPC identifier for volume shared 
					   memory */
    int shm_id = -1;			/* Identifier for shared memory
					   which will store the Sigmet_Vol
					   structure */
    char shmid_s[LEN];			/* String representation of shm_id */
    int ax_sem_id = -1;			/* Semaphore identifier, from semget,
					   given to semctl and semop */
    char ax_sem_id_s[LEN];		/* String representation of ax_sem_id,
					   given to child process environment */
    int usr_sem_id = -1;		/* Semaphore identifier, from semget,
					   given to semctl and semop */
    FILE *msg_out = stdout;		/* Where to put final message */


    /*
       Parse command line.
     */

    if ( argc < 4 ) {
	fprintf(stderr, "Usage: %s %s sigmet_volume command [args ...]\n",
		argv0, argv1);
	return 0;
    }
    vol_fl_nm = argv[2];

    /*
       Get shared memory identifier for volume. If shm_id identifies new shared
       memory. Load new volume and make local copy. If shm_id identifies shared
       memory for previously loaded volume, attach to it.
     */

    if ( (shm_key = ftok(vol_fl_nm, shm_key_id)) == -1 ) {
	fprintf(stderr, "%s %s (%d): could not get memory key for volume %s.\n"
		"%s\n", argv0, argv1, getpid(), vol_fl_nm, strerror(errno));
	return 0;
    }
    flags = S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL;
    shm_id = shmget(shm_key, sizeof(struct Sigmet_Vol), flags);
    if ( shm_id >= 0 ) {
	printf("%s %s (%d): loading %s into shared memory\n",
		argv0, argv1, getpid(), vol_fl_nm);
	if ( !load_new(vol_fl_nm, shm_id, &ax_sem_id, &usr_sem_id, &vol_sav) ) {
	    fprintf(stderr, "%s %s (%d): failed to load volume from %s\n",
		    argv0, argv1, getpid(), vol_fl_nm);
	    return 0;
	}
	loader = 1;
    } else if ( shm_id == -1 && errno == EEXIST ) {
	printf("%s %s (%d): attaching to %s in shared memory\n",
		argv0, argv1, getpid(), vol_fl_nm);
	if ( !deamon_attach(vol_fl_nm, &shm_id, &ax_sem_id, &usr_sem_id) ) {
	    fprintf(stderr, "%s %s (%d): failed to attach to previously loaded "
		    "volume from %s\n", argv0, argv1, getpid(), vol_fl_nm);
	    return 0;
	}
    } else {
	fprintf(stderr, "%s %s (%d): could not allocate or identify volume in "
		"shared memory for volume file %s.\n%s\n", argv0, argv1,
		getpid(), vol_fl_nm, strerror(errno));
	return 0;
    }

    /*
       Update environment so child command can locate and use the volume in
       shared memory. Spawn the child command specified on the command line.
     */

    if ( snprintf(shmid_s, LEN, SIGMET_VOL_SHMEM "=%d", shm_id) > LEN ) {
	fprintf(stderr, "%s %s (%d): could not create environment variable for "
		"volume shared memory identifier.\n", argv0, argv1, getpid());
	goto error;
    }
    if ( putenv(shmid_s) != 0 ) {
	fprintf(stderr, "%s %s (%d): could not put shared memory identifier "
		"for volume into environment.\n%s\n",
		argv0, argv1, getpid(), strerror(errno));
	goto error;
    }
    if ( snprintf(ax_sem_id_s, LEN, SIGMET_VOL_SEM "=%d", ax_sem_id) > LEN ) {
	fprintf(stderr, "%s %s (%d): could not create environment variable for "
		"volume semaphore identifier.\n", argv0, argv1, getpid());
	goto error;
    }
    if ( putenv(ax_sem_id_s) != 0 ) {
	fprintf(stderr, "%s %s (%d): could not put semaphore identifier for "
		"volume into environment.\n%s\n",
		argv0, argv1, getpid(), strerror(errno));
	goto error;
    }
    printf("%s %s (%d): spawning: ", argv0, argv1, getpid());
    for (a = argv + 3; *a; a++) {
	printf("%s ", *a);
    }

    ch_pid = fork();
    if ( ch_pid == -1 ) {
	fprintf(stderr, "%s %s (%d): could not fork\n%s\n",
		argv0, argv1, getpid(), strerror(errno));
	goto error;
    } else if ( ch_pid == 0 ) {
	execvp(argv[3], argv + 3);
	fprintf(stderr, "%s %s (%d): failed to execute child process.\n%s\n",
		argv0, argv1, getpid(), strerror(errno));
	exit(EXIT_FAILURE);
    }
    printf(" (%d)\n", ch_pid);

    /*
       Wait for child to exit.
     */

    waitpid(ch_pid, &ch_stat, 0);
    if ( WIFEXITED(ch_stat) ) {
	fn_stat = (WEXITSTATUS(ch_stat) == EXIT_SUCCESS);
	msg_out = fn_stat ? stdout : stderr;
	fprintf(msg_out, "%s: ", argv0);
	for (a = argv + 3; *a; a++) {
	    fprintf(msg_out, "%s ", *a);
	}
	fprintf(msg_out, "exited with status %d\n", WEXITSTATUS(ch_stat));
    } else if ( WIFSIGNALED(ch_stat) ) {
	fprintf(stderr, "%s: child process exited on signal %d\n",
		argv0, WTERMSIG(ch_stat));
	fn_stat = 0;
    }

    /*
       Decrement user count. If this deamon loaded the volume, wait until
       it has no users, then unload it and free resources.

       Note that there is a possible race condition in this block. Another
       daemon might try to attach to the volume while it is being deleted.
     */

    if ( semop(usr_sem_id, release_sop, 1) == -1 ) {
	fprintf(stderr, "Could not decrement volume user count\n%s\n",
		strerror(errno));
	return 0;
    }
    if ( loader ) {
	printf("%s %s (%d): daemon waiting until volume no longer in "
		"use\n", argv0, argv1, getpid());
	while ( semctl(usr_sem_id, 0, GETVAL) > 0 ) {
	    sleep(1);
	}
	printf("%s %s (%d): volume no longer in use. Unloading.\n",
		argv0, argv1, getpid());
	if ( semctl(ax_sem_id, 0, IPC_RMID) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove access semaphore for "
		    "volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		    argv0, argv1, getpid(), strerror(errno), ax_sem_id);
	    fn_stat = 0;
	}
	if ( semctl(usr_sem_id, 0, IPC_RMID) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove user count semaphore "
		    "for volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		    argv0, argv1, getpid(), strerror(errno), usr_sem_id);
	    fn_stat = 0;
	}
	if ( (sig_stat = Sigmet_Vol_Free(&vol_sav)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s (%d): could not free memory for volume.\n"
		    "%s\n", argv0, argv1, getpid(), sigmet_err(sig_stat));
	    fn_stat = 0;
	}
	if ( shmctl(shm_id, IPC_RMID, NULL) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove shared memory for "
		    "volume.\n%s\nPlease use ipcrm command for id %d\n",
		    argv0, argv1, getpid(), strerror(errno), shm_id);
	    fn_stat = 0;
	}
    }
    printf("%s (%d): exiting.\n", argv0, getpid());
    return fn_stat;

error:
    fprintf(stderr, "%s %s (%d): failed to load or attach to volume.\n",
	    argv0, argv1, getpid());
    if ( loader ) {
	if ( Sigmet_Vol_Free(&vol_sav) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s (%d): could not free memory for "
		    "volume.\n", argv0, argv1, getpid());
	}
	if ( shm_id != -1 && shmctl(shm_id, IPC_RMID, NULL) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove shared memory for "
		    "volume.\n%s\nPlease remove it by calling \nipcrm -m %d\n",
		    argv0, argv1, getpid(), strerror(errno), shm_id);
	}
	if ( usr_sem_id != -1 && semctl(usr_sem_id, 0, IPC_RMID) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove user count semaphore "
		    "for volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		    argv0, argv1, getpid(), strerror(errno), usr_sem_id);
	}
	if ( ax_sem_id != -1 && semctl(ax_sem_id, 0, IPC_RMID) == -1 ) {
	    fprintf(stderr, "%s %s (%d): could not remove access semaphore for "
		    "volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		    argv0, argv1, getpid(), strerror(errno), ax_sem_id);
	}
    }
    return 0;
}

/*
   Load a Sigmet raw product volume from file at path vol_fl_nm into shared
   memory identified by shm_id. If successful, copy access semaphore identifier
   to ax_sem_id_p, user count semaphore identifier to usr_sem_id_p, and return
   true. Otherwise, return false.
 */

static int load_new(char *vol_fl_nm, int shm_id, int *ax_sem_id_p,
	int *usr_sem_id_p, struct Sigmet_Vol *vol_sav_p)
{
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    FILE *vol_fl;			/* Input stream associated with
					   vol_fl_nm */
    pid_t lpid;				/* Process id of read process */
    struct Sigmet_Vol *vol_p = (void *)-1;
    int flags;				/* IPC flags for shmget and semget */
    key_t ax_key;			/* Access semaphore key */
    int ax_sem_id = -1;			/* Access semaphore identifier */
    union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
    } arg;				/* Argument for semctl */
    struct semid_ds buf;		/* Buffer for semctl */
    key_t usr_key;			/* User count semaphore key */
    int usr_sem_id = -1;		/* User count semaphore identifier */

    assert(shm_id >= 0 );
    vol_p = shmat(shm_id, NULL, 0);
    if ( vol_p == (void *)-1 ) {
	fprintf(stderr, "could not attach to shared memory for volume.\n%s\n",
		strerror(errno));
	goto error;
    }
    Sigmet_Vol_Init(vol_p);
    vol_p->shm = 1;
    lpid = -1;
    if ( !(vol_fl = vol_open(vol_fl_nm, &lpid)) ) {
	fprintf(stderr, "Could not open file %s for reading.\n%s\n",
		vol_fl_nm, strerror(errno));
	goto error;
    }
    sig_stat = Sigmet_Vol_Read(vol_fl, vol_p);
    fclose(vol_fl);
    if ( lpid != -1 ) {
	waitpid(lpid, NULL, 0);
    }
    if ( sig_stat != SIGMET_OK ) {
	fprintf(stderr, "Could not read volume.\n%s\n", sigmet_err(sig_stat));
	goto error;
    }
    Sigmet_Vol_Init(vol_sav_p);
    Sigmet_Vol_LzCpy(vol_sav_p, vol_p);

    /*
       Create user count semaphore. Initialize to 1.
     */

    if ( (usr_key = ftok(vol_fl_nm, usr_key_id)) == -1 ) {
	fprintf(stderr, "Could not get memory key for volume %s.\n%s\n",
		vol_fl_nm, strerror(errno));
	goto error;
    }
    flags = S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL;
    if ( (usr_sem_id = semget(usr_key, 1, flags)) == -1 ) {
	fprintf(stderr, "Could not create user count semaphore for volume %s.\n"
		"%s\n", vol_fl_nm, strerror(errno));
	goto error;
    }
    arg.val = 1;
    if ( semctl(usr_sem_id, 0, SETVAL, arg) == -1 ) {
	fprintf(stderr, "Could not initialize access semaphore for volume %s.\n"
		"%s\n", vol_fl_nm, strerror(errno));
	goto error;
    }

    /*
       Create the semaphore that controls volume access.  Create with
       write access, but not read access. This will tell competing load
       processes to wait. Allow read access once semaphore is initialized.
     */

    if ( (ax_key = ftok(vol_fl_nm, ax_key_id)) == -1 ) {
	fprintf(stderr, "Could not get memory key for volume %s.\n%s\n",
		vol_fl_nm, strerror(errno));
	goto error;
    }
    flags = S_IWUSR | IPC_CREAT | IPC_EXCL;
    if ( (ax_sem_id = semget(ax_key, 1, flags)) == -1 ) {
	fprintf(stderr, "Could not create access semaphore for volume %s.\n"
		"%s\n", vol_fl_nm, strerror(errno));
	goto error;
    }
    arg.val = 1;
    if ( semctl(ax_sem_id, 0, SETVAL, arg) == -1 ) {
	fprintf(stderr, "could not initialize access semaphore for volume %s.\n"
		"%s\n", vol_fl_nm, strerror(errno));
	goto error;
    }
    buf.sem_perm.uid = getuid();
    buf.sem_perm.gid = getgid();
    buf.sem_perm.mode = S_IRUSR | S_IWUSR;
    arg.buf = &buf;
    if ( semctl(ax_sem_id, 0, IPC_SET, arg) == -1 ) {
	fprintf(stderr, "Could not set permissions for access "
		"semaphore for volume %s.\n%s\n", vol_fl_nm, strerror(errno));
	goto error;
    }
    *ax_sem_id_p = ax_sem_id;
    *usr_sem_id_p = usr_sem_id;
    return 1;

error:
    fprintf(stderr, "Failed to load volume.\n");
    if ( vol_p != (void *)-1 ) {
	if ( Sigmet_Vol_Free(vol_sav_p) != SIGMET_OK ) {
	    fprintf(stderr, "Could not free memory for volume.\n");
	}
	if ( shmdt(vol_p) == -1 ) {
	    fprintf(stderr, "Could not detach shared memory for volume.\n%s\n",
		    strerror(errno));
	}
    }
    if ( shmctl(shm_id, IPC_RMID, NULL) == -1 ) {
	fprintf(stderr, "Could not remove shared memory for "
		"volume.\n%s\nPlease remove it by calling \nipcrm -m %d\n",
		strerror(errno), shm_id);
    }
    if ( usr_sem_id != -1 && semctl(usr_sem_id, 0, IPC_RMID) == -1 ) {
	fprintf(stderr, "Could not remove user count semaphore "
		"for volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		strerror(errno), usr_sem_id);
    }
    if ( ax_sem_id != -1 && semctl(ax_sem_id, 0, IPC_RMID) == -1 ) {
	fprintf(stderr, "could not remove access semaphore for "
		"volume.\n%s\nPlease remove it with\nipcrm -s %d\n",
		strerror(errno), ax_sem_id);
    }
    return 0;
}

/*
   Attach to loaded volume, instead of reloading it.
 */

static int deamon_attach(char *vol_fl_nm, int *shm_id_p, int *ax_sem_id_p,
	int *usr_sem_id_p)
{
    key_t ax_key;			/* Volume access semaphore key */
    int ax_sem_id = -1;			/* Volume access semaphore identifier */
    key_t shm_key;			/* Volume shared memory key */
    int flags;				/* IPC flags for shmget and semget */
    int shm_id;				/* Volume shared memory identifier */
    key_t usr_key;			/* User count semaphore key */
    int usr_sem_id = -1;		/* User count semaphore identifier */
    int n, num_tries = 9;		/* Check on access semaphore this many
					   times */

    /*
       Attempt to use the volume access semaphore. If it exists but is not
       readable, then a load daemon is still loading the volume. Wait for the
       other daemon to finish intializing the volume access semaphore.
       The other daemon indicates this by making the access semaphore readable.
     */

    if ( (ax_key = ftok(vol_fl_nm, ax_key_id)) == -1 ) {
	fprintf(stderr, "Could not get shared memory key for %s.\n%s\n",
		vol_fl_nm, strerror(errno));
	fprintf(stderr, "Failed to attach to previously loaded volume.\n");
	return 0;
    }
    for (n = 0, ax_sem_id = -1; n < num_tries && ax_sem_id == -1; n++ ) {
	ax_sem_id = semget(ax_key, 1, S_IRUSR | S_IWUSR);
	if ( ax_sem_id == -1 ) {
	    if ( errno == ENOENT || errno == EACCES ) {
		printf("Waiting for load process to make volume available.\n");
		sleep(1);
	    } else {
		fprintf(stderr, "Could not get access semaphore for "
			"previously loaded volume %s.\n%s\n",
			vol_fl_nm, strerror(errno));
		return 0;
	    }
	}
    }
    if ( n == num_tries ) {
	fprintf(stderr, "Gave up waiting for load process to make volume "
		"available.\n");
	return 0;
    }

    /*
       Now that volume is loaded somewhere, get the shared memory identifier.
     */

    if ( (shm_key = ftok(vol_fl_nm, shm_key_id)) == -1 ) {
	fprintf(stderr, "Could not get memory key for volume %s.\n%s\n",
		vol_fl_nm, strerror(errno));
	return 0;
    }
    flags = S_IRUSR | S_IWUSR;
    if ( (shm_id = shmget(shm_key, sizeof(struct Sigmet_Vol), flags)) == -1) {
	fprintf(stderr, "Could not identify volume in shared memory for %s.\n"
		"%s\n", vol_fl_nm, strerror(errno));
	return 0;
    }

    /*
       Increment the volume user count.
     */

    if ( (usr_key = ftok(vol_fl_nm, usr_key_id)) == -1 ) {
	fprintf(stderr, "Could not get memory key for volume %s.\n%s\n",
		vol_fl_nm, strerror(errno));
	return 0;
    }
    if ( (usr_sem_id = semget(usr_key, 1, S_IRUSR | S_IWUSR)) == -1 ) {
	fprintf(stderr, "Could not find user count "
		"semaphore for volume %s.\n%s\n", vol_fl_nm, strerror(errno));
	return 0;
    }
    if ( semop(usr_sem_id, use_sop, 1) == -1 ) {
	fprintf(stderr, "could not increment volume user count %d\n%s\n",
		ax_sem_id, strerror(errno));
	return 0;
    }
    *ax_sem_id_p = ax_sem_id;
    *shm_id_p = shm_id;
    *usr_sem_id_p = usr_sem_id;
    return 1;
}

/*
   Return a pointer in current process address space to the Sigmet volume in
   shared memory provided by the parent daemon. Return NULL if something goes
   wrong. Copy volume semaphore identifier to ax_sem_id_p.
 */

static struct Sigmet_Vol *client_attach(int *sem_id_p)
{
    struct Sigmet_Vol *vol_p = NULL;
    int ax_sem_id, shm_id;		/* IPC semaphore and shared memory
					   identifiers */
    char *sem_id_s, *shm_id_s;		/* String representations of ax_sem_id
					   and shm_id */

    /*
       Decrement the semaphore
     */

    if ( !(sem_id_s = getenv(SIGMET_VOL_SEM))
	    || sscanf(sem_id_s, "%d", &ax_sem_id) != 1 ) {
	fprintf(stderr, "Could not identify volume semaphore from "
		SIGMET_VOL_SEM " environment variable.\n");
	return NULL;
    }
    if ( semop(ax_sem_id, wait_sop, 1) == -1 ) {
	fprintf(stderr, "Could not decrement volume access semaphore\n%s\n",
		strerror(errno));
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
    if ( Sigmet_ShMemAttach(vol_p) != SIGMET_OK ) {
	fprintf(stderr, "Could not attach to volume contents "
		"in shared memory.\n");
	goto error;
    }

    *sem_id_p = ax_sem_id;
    return vol_p;

error:
    if ( semop(ax_sem_id, post_sop, 1) == -1 ) {
	fprintf(stderr, "Could not increment volume access semaphore\n%s\n",
		strerror(errno));
    }
    if ( vol_p ) {
	if ( Sigmet_ShMemDetach(vol_p) != SIGMET_OK ) {
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

static void client_detach(struct Sigmet_Vol *vol_p, int ax_sem_id)
{
    if ( semop(ax_sem_id, post_sop, 1) == -1 ) {
	fprintf(stderr, "Could not increment volume access semaphore\n%s\n",
		strerror(errno));
    }
    if ( vol_p ) {
	if ( Sigmet_ShMemDetach(vol_p) != SIGMET_OK ) {
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
    char *data_type_s, *descr, *unit;

    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (y = 0; y < Sigmet_Vol_NumTypes(vol_p); y++) {
	Sigmet_Vol_DataTypeHdrs(vol_p, y, &data_type_s, &descr, &unit);
	printf("%s | %s | %s\n", data_type_s, descr, unit);
    }
    client_detach(vol_p, ax_sem_id);
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    Sigmet_Vol_PrintHdr(stdout, vol_p);
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int vol_hdr_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    Sigmet_Vol_PrintMinHdr(stdout, vol_p);
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int near_sweep_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    char *ang_s;
    double ang;
    int s;

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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    s = Sigmet_Vol_NearSweep(vol_p, ang);
    client_detach(vol_p, ax_sem_id);
    if ( s == -1 ) {
	fprintf(stderr, "%s %s: could not determine sweep with "
		"sweep angle nearest %s\n", argv0, argv1, ang_s);
	return 0;
    }
    printf("%d\n", s);
    return 1;
}

static int sweep_headers_cb(int argc, char *argv[])
{
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    int s;
    enum SigmetStatus sig_stat;
    int ok;
    double tm, ang;
    int yr, mon, da, hr, min, sec;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (s = 0; s < Sigmet_Vol_NumSweeps(vol_p); s++) {
	printf("sweep %2d ", s);
	sig_stat = Sigmet_Vol_SweepHdr(vol_p, s, &ok, &tm, &ang);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: %s\n", argv0, argv1, sigmet_err(sig_stat));
	}
	if ( ok ) {
	    if ( Tm_JulToCal(tm, &yr, &mon, &da, &hr, &min, &sec) ) {
		printf("%04d/%02d/%02d %02d:%02d:%02d ",
			yr, mon, da, hr, min, sec);
	    } else {
		printf("0000/00/00 00:00:00 ");
	    }
	    printf("%7.3f\n", ang * DEG_PER_RAD);
	} else {
	    printf("bad\n");
	}
    }
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int ray_headers_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int s, r;
    int ok;
    int num_bins;
    double tm, tilt0, tilt1, az0, az1;
    int yr, mon, da, hr, min, sec;
    enum SigmetStatus sig_stat;

    if ( argc != 2 ) {
	fprintf(stderr, "Usage: %s %s\n", argv0, argv1);
	return 0;
    }
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    for (s = 0; s < Sigmet_Vol_NumSweeps(vol_p); s++) {
	sig_stat = Sigmet_Vol_SweepHdr(vol_p, s, &ok, NULL, NULL);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: %s\n", argv0, argv1, sigmet_err(sig_stat));
	}
	if ( ok ) {
	    for (r = 0; r < Sigmet_Vol_NumRays(vol_p); r++) {
		sig_stat = Sigmet_Vol_RayHdr(vol_p, s, r, &ok, &tm, &num_bins,
			&tilt0, &tilt1, &az0, &az1);
		if ( sig_stat != SIGMET_OK ) {
		    fprintf(stderr, "%s %s: %s\n",
			    argv0, argv1, sigmet_err(sig_stat));
		}
		if ( !ok ) {
		    continue;
		}
		printf("sweep %3d ray %4d | ", s, r);
		if ( !Tm_JulToCal(tm, &yr, &mon, &da, &hr, &min, &sec) ) {
		    fprintf(stderr, "%s %s: bad ray time\n",
			    argv0, argv1);
		    client_detach(vol_p, ax_sem_id);
		    return 0;
		}
		printf("%04d/%02d/%02d %02d:%02d:%02d | ",
			yr, mon, da, hr, min, sec);
		printf("az %7.3f %7.3f | ",
			az0 * DEG_PER_RAD, az1 * DEG_PER_RAD);
		printf("tilt %6.3f %6.3f\n",
			tilt0 * DEG_PER_RAD, tilt1 * DEG_PER_RAD);
	    }
	} else {
	    printf("sweep %3d empty\n", s);
	}
    }
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int new_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p = NULL;
    int ax_sem_id;
    int a;
    char *data_type_s;			/* Data type abbreviation */
    char *val_s = NULL;			/* Initial value */
    double val;
    char *descr = NULL;			/* Descriptor for new field */
    char *unit = NULL;			/* Unit for new field */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    /*
       Identify data type from command line. Fail if volume already has
       this data type.
     */

    if ( argc < 3 || argc > 9 ) {
	fprintf(stderr, "Usage: %s %s data_type [-d description] [-u unit] "
		"[-v value]\n", argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];

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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    sig_stat = Sigmet_Vol_NewField(vol_p, data_type_s, descr, unit);
    if ( sig_stat != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add data type %s to volume\n%s\n",
		argv0, argv1, data_type_s, sigmet_err(sig_stat));
	goto error;
    }

    /*
       If given optional value, initialize new field with it.
     */

    if ( val_s ) {
	if ( sscanf(val_s, "%lf", &val) == 1 ) {
	    sig_stat = Sigmet_Vol_Fld_SetVal(vol_p, data_type_s, val);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, data_type_s, val, sigmet_err(sig_stat));
		goto error;
	    }
	} else if ( strcmp(val_s, "r_beam") == 0 ) {
	    sig_stat = Sigmet_Vol_Fld_SetRBeam(vol_p, data_type_s);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, data_type_s, val_s, sigmet_err(sig_stat));
		goto error;
	    }
	} else {
	    sig_stat = Sigmet_Vol_Fld_Copy(vol_p, data_type_s, val_s);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not set %s to %s in volume\n%s\n"
			"Field is retained in volume but values are garbage.\n",
			argv0, argv1, data_type_s, val_s, sigmet_err(sig_stat));
		goto error;
	    }
	}
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    if ( vol_p ) {
	client_detach(vol_p, ax_sem_id);
    }
    return 0;
}

static int del_field_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    char *data_type_s;			/* Data type abbreviation */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n", argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (sig_stat = Sigmet_Vol_DelField(vol_p, data_type_s)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not remove data type %s "
		"from volume\n%s\n", argv0, argv1, data_type_s,
		sigmet_err(sig_stat));
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    client_detach(vol_p, ax_sem_id);
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    printf("%lu\n", (unsigned long)Sigmet_Vol_MemSz(vol_p));
    client_detach(vol_p, ax_sem_id);
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *d_s;
    double d;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value\n", argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    d_s = argv[3];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
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
	sig_stat = Sigmet_Vol_Fld_SetRBeam(vol_p, data_type_s);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to beam range "
		    "in volume\n%s\n", argv0, argv1, data_type_s,
		    sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( sscanf(d_s, "%lf", &d) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_SetVal(vol_p, data_type_s, d);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not set %s to %lf in volume\n%s\n",
		    argv0, argv1, data_type_s, d, sigmet_err(sig_stat));
	    goto error;
	}
    } else {
	fprintf(stderr, "%s %s: field value must be a number or \"r_beam\"\n",
		argv0, argv1);
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to add */
    double a;				/* Scalar to add */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    a_s = argv[3];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_AddVal(vol_p, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not add %s to %lf in volume\n%s\n",
		    argv0, argv1, data_type_s, a, sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_AddFld(vol_p, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not add %s to %s in volume\n%s\n",
		argv0, argv1, data_type_s, a_s, sigmet_err(sig_stat));
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to subtract */
    double a;				/* Scalar to subtract */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    a_s = argv[3];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_SubVal(vol_p, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not subtract %lf from %s in "
		    "volume\n%s\n", argv0, argv1, a, data_type_s,
		    sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_SubFld(vol_p, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not subtract %s from %s in volume\n%s\n",
		argv0, argv1, a_s, data_type_s, sigmet_err(sig_stat));
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to multiply by */
    double a;				/* Scalar to multiply by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    a_s = argv[3];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_MulVal(vol_p, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not multiply %s by %lf in "
		    "volume\n%s\n", argv0, argv1, data_type_s, a,
		    sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_MulFld(vol_p, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not multiply %s by %s in volume\n%s\n",
		argv0, argv1, data_type_s, a_s, sigmet_err(sig_stat));
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */
    char *a_s;				/* What to divide by */
    double a;				/* Scalar to divide by */

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type value|field\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    a_s = argv[3];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( sscanf(a_s, "%lf", &a) == 1 ) {
	sig_stat = Sigmet_Vol_Fld_DivVal(vol_p, data_type_s, a);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not divide %s by %lf in volume\n%s\n",
		    argv0, argv1, data_type_s, a, sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( (sig_stat = Sigmet_Vol_Fld_DivFld(vol_p, data_type_s, a_s))
	    != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not divide %s by %s in volume\n%s\n",
		argv0, argv1, data_type_s, a_s, sigmet_err(sig_stat));
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *data_type_s;			/* Data type abbreviation */

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s data_type\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (sig_stat = Sigmet_Vol_Fld_Log10(vol_p, data_type_s)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not compute log10 of %s in volume\n%s\n",
		argv0, argv1, data_type_s, sigmet_err(sig_stat));
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int incr_time_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	goto error;
    }
    if ( (sig_stat = Sigmet_Vol_IncrTm(vol_p, dt / 86400.0)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: could not increment time in volume\n%s\n",
		argv0, argv1, sigmet_err(sig_stat));
	goto error;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
}

static int data_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int num_types, num_sweeps, num_rays, num_bins;
    int ax_sem_id;
    int s, y, r, b;
    char *data_type_s;
    int all = -1;

    /*
       Identify input and desired output
       Possible forms:
       sigmet_ray data			(argc = 3)
       sigmet_ray data data_type	(argc = 4)
       sigmet_ray data data_type s	(argc = 5)
       sigmet_ray data data_type s r	(argc = 6)
       sigmet_ray data data_type s r b	(argc = 7)
     */

    data_type_s = NULL;
    y = s = r = b = all;
    if ( argc >= 3 ) {
	data_type_s = argv[2];
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }

    /*
       Validate.
     */

    num_types = Sigmet_Vol_NumTypes(vol_p);
    if ( data_type_s
	    && (y = Sigmet_Vol_GetFld(vol_p, data_type_s, NULL)) == -1 ) {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, data_type_s);
	goto error;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(vol_p);
    if ( s != all && s >= num_sweeps ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	goto error;
    }
    num_rays = Sigmet_Vol_NumRays(vol_p);
    if ( r != all && r >= num_rays ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	goto error;
    }
    if ( b != all && b >= Sigmet_Vol_NumBins(vol_p, s, -1) ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	goto error;
    }

    /*
       Done parsing. Start writing.
     */

    if ( y == all && s == all && r == all && b == all ) {
	for (y = 0; y < num_types; y++) {
	    for (s = 0; s < num_sweeps; s++) {
		Sigmet_Vol_DataTypeHdrs(vol_p, y, &data_type_s, NULL, NULL);
		printf("%s. sweep %d\n", data_type_s, s);
		for (r = 0; r < num_rays; r++) {
		    if ( Sigmet_Vol_BadRay(vol_p, s, r) ) {
			continue;
		    }
		    printf("ray %d: ", r);
		    num_bins = Sigmet_Vol_NumBins(vol_p, s, r);
		    for (b = 0; b < num_bins; b++) {
			printf("%f ", Sigmet_Vol_GetDatum(vol_p, y, s, r, b));
		    }
		    printf("\n");
		}
	    }
	}
    } else if ( s == all && r == all && b == all ) {
	for (s = 0; s < num_sweeps; s++) {
	    printf("%s. sweep %d\n", data_type_s, s);
	    for (r = 0; r < num_rays; r++) {
		if ( Sigmet_Vol_BadRay(vol_p, s, r) ) {
		    continue;
		}
		printf("ray %d: ", r);
		num_bins = Sigmet_Vol_NumBins(vol_p, s, r);
		for (b = 0; b < num_bins; b++) {
		    printf("%f ", Sigmet_Vol_GetDatum(vol_p, y, s, r, b));
		}
		printf("\n");
	    }
	}
    } else if ( r == all && b == all ) {
	printf("%s. sweep %d\n", data_type_s, s);
	for (r = 0; r < num_rays; r++) {
	    if ( Sigmet_Vol_BadRay(vol_p, s, r) ) {
		continue;
	    }
	    printf("ray %d: ", r);
	    num_bins = Sigmet_Vol_NumBins(vol_p, s, r);
	    for (b = 0; b < num_bins; b++) {
		printf("%f ", Sigmet_Vol_GetDatum(vol_p, y, s, r, b));
	    }
	    printf("\n");
	}
    } else if ( b == all ) {
	printf("%s. sweep %d, ray %d: ", data_type_s, s, r);
	if ( !Sigmet_Vol_BadRay(vol_p, s, r) ) {
	    num_bins = Sigmet_Vol_NumBins(vol_p, s, r);
	    for (b = 0; b < num_bins; b++) {
		printf("%f ", Sigmet_Vol_GetDatum(vol_p, y, s, r, b));
	    }
	    printf("\n");
	}
    } else {
	if ( !Sigmet_Vol_BadRay(vol_p, s, r) ) {
	    printf("%s. sweep %d, ray %d, bin %d: ", data_type_s, s, r, b);
	    printf("%f ", Sigmet_Vol_GetDatum(vol_p, y, s, r, b));
	    printf("\n");
	}
    }
    client_detach(vol_p, ax_sem_id);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
}

/*
   Print sweep data as a binary stream.
   sigmet_ray bdata data_type s
   Each output ray will have num_output_bins floats.
   Missing values will be NAN.
 */

static int bdata_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int num_types, num_sweeps, num_bins;
    int ax_sem_id;
    int s, y, r, b;
    char *data_type_s;
    static float *ray_p;	/* Buffer to receive ray data */
    enum SigmetStatus sig_stat;

    if ( argc != 4 ) {
	fprintf(stderr, "Usage: %s %s data_type sweep_index\n",
		argv0, argv1);
	return 0;
    }
    data_type_s = argv[2];
    if ( sscanf(argv[3], "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, argv[3]);
	return 0;
    }
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    num_types = Sigmet_Vol_NumTypes(vol_p);
    if ( (y = Sigmet_Vol_GetFld(vol_p, data_type_s, NULL)) == -1 ) {
	fprintf(stderr, "%s %s: no data type named %s\n",
		argv0, argv1, data_type_s);
	goto error;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(vol_p);
    if ( s >= num_sweeps ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	goto error;
    }
    num_bins = Sigmet_Vol_NumBins(vol_p, s, -1);
    if ( num_bins == -1 ) {
	fprintf(stderr, "%s %s: could not get number of bins for sweep %d\n",
		argv0, argv1, s);
	goto error;
    }
    if ( !ray_p && !(ray_p = CALLOC(num_bins, sizeof(float))) ) {
	fprintf(stderr, "Could not allocate output buffer for ray.\n");
	goto error;
    }
    for (r = 0; r < Sigmet_Vol_NumRays(vol_p); r++) {
	num_bins = Sigmet_Vol_NumBins(vol_p, s, r);
	for (b = 0; b < num_bins; b++) {
	    ray_p[b] = NAN;
	}
	if ( !Sigmet_Vol_BadRay(vol_p, s, r) ) {
	    sig_stat = Sigmet_Vol_GetRayDat(vol_p, y, s, r, &ray_p);
	    if ( sig_stat != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not get ray data "
			"for data type %s, sweep index %d, ray %d.\n%s\n",
			argv0, argv1, data_type_s, s, r, sigmet_err(sig_stat));
		goto error;
	    }
	}
	if ( fwrite(ray_p, sizeof(float), num_bins, stdout) != num_bins ) {
	    fprintf(stderr, "%s %s: could not write ray data for data type %s, "
		    "sweep index %d, ray %d.\n%s\n", argv0, argv1,
		    data_type_s, s, r, strerror(errno));
	    goto error;
	}
    }
    client_detach(vol_p, ax_sem_id);
    return 1;
    
error:
    client_detach(vol_p, ax_sem_id);
    return 0;
}

static int bin_outline_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
    char *s_s, *r_s, *b_s;
    int s, r, b;
    double cnr[8];

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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( s >= Sigmet_Vol_NumSweeps(vol_p) ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	goto error;
    }
    if ( r >= Sigmet_Vol_NumRays(vol_p) ) {
	fprintf(stderr, "%s %s: ray index %d out of range for volume\n",
		argv0, argv1, r);
	goto error;
    }
    if ( b >= Sigmet_Vol_NumBins(vol_p, s, r) ) {
	fprintf(stderr, "%s %s: bin index %d out of range for volume\n",
		argv0, argv1, b);
	goto error;
    }
    if ( Sigmet_Vol_IsPPI(vol_p) ) {
	if ( !set_proj(vol_p) ) {
	    fprintf(stderr, "%s %s: could not set geographic projection.\n",
		    argv0, argv1);
	    goto error;
	}
	sig_stat = Sigmet_Vol_PPI_BinOutl(vol_p, s, r, b, lonlat_to_xy, cnr);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not compute bin outlines for bin "
		    "%d %d %d in volume\n%s\n", argv0, argv1, s, r, b,
		    sigmet_err(sig_stat));
	    goto error;
	}
    } else if ( Sigmet_Vol_IsRHI(vol_p) ) {
	sig_stat = Sigmet_Vol_RHI_BinOutl(vol_p, s, r, b, cnr);
	if ( sig_stat != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not compute bin outlines for bin "
		    "%d %d %d in volume\n%s\n", argv0, argv1, s, r, b,
		    sigmet_err(sig_stat));
	    goto error;
	}
    } else {
    }
    client_detach(vol_p, ax_sem_id);
    printf("%f %f %f %f %f %f %f %f\n",
	    cnr[0] * DEG_RAD, cnr[1] * DEG_RAD,
	    cnr[2] * DEG_RAD, cnr[3] * DEG_RAD,
	    cnr[4] * DEG_RAD, cnr[5] * DEG_RAD,
	    cnr[6] * DEG_RAD, cnr[7] * DEG_RAD);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
}

/*
   Set geographic projection.
 */

static int set_proj(struct Sigmet_Vol *vol_p)
{
    double lon, lat;			/* Radar location */
    char *proj_s;			/* Environment projection description */
    char dflt_proj_s[LEN];		/* Default projection description */

    if ( (proj_s = getenv(SIGMET_GEOG_PROJ)) ) {
	if ( !Sigmet_Proj_Set(proj_s) ) {
	    fprintf(stderr, "Could not set projection from "
		    SIGMET_GEOG_PROJ " environment variable.\n");
	    return 0;
	}
    } else {
	lon = Sigmet_Vol_RadarLon(vol_p, NULL);
	lat = Sigmet_Vol_RadarLat(vol_p, NULL);
	if ( snprintf(dflt_proj_s, LEN,
		    "CylEqDist %.9g %.9g", lon, lat) > LEN
		|| !Sigmet_Proj_Set(dflt_proj_s) ) {
	    fprintf(stderr, "Could not set default projection.\n");
	    return 0;
	}
    }
    return 1;
}

/*
   Print sweep limits. If ppi, print map coordinates. Map projection can be
   specified with SIGMET_GEOG_PROJ environment variable, otherwise projection
   defaults to cylindrical equidistant with no distortion at radar. If rhi,
   print distance down range and height above ground level.
 */

static int sweep_bnds_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    double x_min, x_max, y_min, y_max;
    char *sweep_s;
    int s;

    if ( argc != 3 ) {
	fprintf(stderr, "Usage: %s %s sweep\n", argv0, argv1);
	return 0;
    }
    sweep_s = argv[2];
    if ( sscanf(sweep_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s.\n",
		argv0, argv1, sweep_s);
    }
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	goto error;
    }
    if ( Sigmet_Vol_IsPPI(vol_p) ) {
	if ( !set_proj(vol_p) ) {
	    fprintf(stderr, "%s %s: could not set geographic projection.\n",
		    argv0, argv1);
	    goto error;
	}
	if ( Sigmet_Vol_PPI_Bnds(vol_p, s, lonlat_to_xy,
		    &x_min, &x_max, &y_min, &y_max) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not compute PPI boundaries.\n",
		    argv0, argv1);
	    goto error;
	}
    } else if ( Sigmet_Vol_IsRHI(vol_p) ) {
	x_min = y_min = 0.0;
	if ( Sigmet_Vol_RHI_Bnds(vol_p, s, &x_max, &y_max) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not compute PPI boundaries.\n",
		    argv0, argv1);
	    goto error;
	}
    }
    client_detach(vol_p, ax_sem_id);
    printf("x_min=%lf\nx_max=%lf\ny_min=%lf\ny_max=%lf\n",
	    x_min, x_max, y_min, y_max);
    return 1;

error:
    client_detach(vol_p, ax_sem_id);
    return 0;
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    lon = GeogLonR(lon * RAD_PER_DEG, M_PI);
    Sigmet_Vol_RadarLon(vol_p, &lon);
    client_detach(vol_p, ax_sem_id);
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    Sigmet_Vol_RadarLat(vol_p, &lat);
    client_detach(vol_p, ax_sem_id);
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
    enum SigmetStatus sig_stat;

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
    daz = GeogLonR(daz * RAD_PER_DEG, M_PI);
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    if ( (sig_stat = Sigmet_Vol_ShiftAz(vol_p, daz)) != SIGMET_OK ) {
	fprintf(stderr, "%s %s: failed to shift azimuths.\n%s\n",
		argv0, argv1, sigmet_err(sig_stat));
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    client_detach(vol_p, ax_sem_id);
    return 1;
}

static int outlines_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p = NULL;
    int num_rays, num_bins;
    int ppi;				/* If true, volume is ppi */
    int ax_sem_id;
    int fill;				/* If true, fill space between rays */
    char *data_type_s;			/* Data type abbreviation */
    char *clr_fl_nm;			/* Name of file with color specifiers
					   and bounds */
    FILE *clr_fl;			/* Input stream for color file */
    int num_colors;			/* Number of colors */
    int num_bnds;			/* Number of boundaries */
    char *s_s;				/* Sweep index, as a string */
    int s;				/* Sweep index */
    char (*colors)[COLOR_NM_LEN_A] = NULL; /* Color names, e.g. "#rrggbb" */
    char bnd[FLOAT_STR_LEN_A];		/* String representation of a boundary
					   value, .e.g "1.23", or "-INF" */
    float *bnds = NULL;			/* Bounds for each color */
    char *format;			/* Conversion specifier */
    float *dat = NULL, *d_p;		/* Sweep data, point into dat */
    int d;				/* Index in dat */
    int c;				/* Color index */
    int y, r, b;			/* Data type, ray, bin index */
    int *lists = NULL;			/* Linked lists of gate indeces */
    double cnr[8];			/* Corner coordinates for a bin */
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */

    if ( argc == 5 ) {
	fill = 0;
	data_type_s = argv[2];
	clr_fl_nm = argv[3];
	s_s = argv[4];
    } else if ( argc == 6 && strcmp(argv[2], "-f") == 0 ) {
	data_type_s = argv[3];
	clr_fl_nm = argv[4];
	s_s = argv[5];
	fill = 1;
    } else {
	fprintf(stderr, "Usage: %s %s [-f] data_type color_file sweep\n",
		argv0, argv1);
	return 0;
    }
    if ( sscanf(s_s, "%d", &s) != 1 ) {
	fprintf(stderr, "%s %s: expected integer for sweep index, got %s\n",
		argv0, argv1, s_s);
	return 0;
    }

    /*
       Get colors from color file.
       Format --
       number_of_colors bound color bound color ... color bound

       Number of colors must be a positive integer
       First bound must be "-INF" or a float value
       Last bound must be a float value or "INF"
       All other bounds must be float values.
       Colors are strings with up to COLOR_NM_LEN - 1 characters.
     */

    if ( !(clr_fl = fopen(clr_fl_nm, "r")) ) {
	fprintf(stderr, "%s: could not open %s for reading.\n%s\n",
		argv0, clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( fscanf(clr_fl, " %d", &num_colors) != 1 ) {
	fprintf(stderr, "%s: could not get color count from %s.\n%s\n",
		argv0, clr_fl_nm, strerror(errno));
	goto error;
    }
    if ( num_colors < 1) {
	fprintf(stderr, "%s: must have more than one color.\n%s\n",
		argv0, strerror(errno));
	goto error;
    }
    num_bnds = num_colors + 1;
    if ( !(colors = CALLOC((size_t)num_colors, COLOR_NM_LEN_A)) ) {
	fprintf(stderr, "%s %s: could not allocate %d colors.\n",
		argv0, argv1, num_colors);
	goto error;
    }
    if ( !(bnds = CALLOC((size_t)(num_bnds), sizeof(double))) ) {
	fprintf(stderr, "%s %s: could not allocate %d color table bounds.\n",
		argv0, argv1, num_bnds);
	goto error;
    }

    /*
       First bound and color.
     */

    format = " %" FLOAT_STR_LEN_S "s %" COLOR_NM_LEN_S "s";
    if ( fscanf(clr_fl, format, bnd, colors) == 2 ) {
	if ( strcmp(bnd, "-INF") == 0 ) {
	    bnds[0] = strtof("-INFINITY", NULL);
	} else if ( sscanf(bnd, "%f", bnds) == 1 ) {
	    ;
	} else {
	    fprintf(stderr, "%s: reading first color, expected number or "
		    "\"-INF\" for minimum value, got %s.\n", argv0, bnd);
	    goto error;
	}
    } else {
	fprintf(stderr, "%s: could not read first color and bound from %s.\n",
		argv0, clr_fl_nm);
	goto error;
    }

    /*
       Second through next to last bounds and colors.
     */

    format = " %f %" COLOR_NM_LEN_S "s";
    for (c = 1; c < num_colors; c++) {
	if ( fscanf(clr_fl, format, bnds + c, colors + c) != 2 ) {
	    fprintf(stderr, "%s: could not read color and bound at index %d "
		    "from %s.\n", argv0, c, clr_fl_nm);
	    goto error;
	}
    }

    /*
       Last bound.
     */

    format = " %" FLOAT_STR_LEN_S "s";
    if ( fscanf(clr_fl, format, bnd) == 1 ) {
	if ( sscanf(bnd, "%f", bnds + c) == 1 ) {
	    ;
	} else if ( strcmp(bnd, "INF") == 0 ) {
	    bnds[c] = strtof("INFINITY", NULL);
	} else {
	    fprintf(stderr, "%s: reading final color, expected number or "
		    "\"INF\" for boundary, got %s\n", argv0, bnd);
	    goto error;
	}
    } else {
	fprintf(stderr, "%s: could not read final bound from %s\n",
		argv0, clr_fl_nm);
	goto error;
    }

    /*
       Get sweep data
     */

    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	goto error;
    }
    ppi = Sigmet_Vol_IsPPI(vol_p);
    if ( ppi && !set_proj(vol_p) ) {
	fprintf(stderr, "%s %s: could not set geographic projection.\n",
		argv0, argv1);
	goto error;
    }
    num_rays = Sigmet_Vol_NumRays(vol_p);
    num_bins = Sigmet_Vol_NumBins(vol_p, s, -1);
    if ( num_bins == -1 ) {
	fprintf(stderr, "%s %s: could not get number of bins for sweep %d\n",
		argv0, argv1, s);
	goto error;
    }
    if ( !(dat = CALLOC(num_rays * num_bins, sizeof(float))) ) {
	fprintf(stderr, "%s %s: could not allocate memory for data for "
		"sweep with %d rays, %d bins.\n",
		argv0, argv1, num_rays, num_bins);
	goto error;
    }
    for (r = 0, d_p = dat; r < num_rays; r++, d_p += num_bins) {
	for (b = 0; b < num_bins; b++) {
	    d_p[b] = NAN;
	}
	if ( Sigmet_Vol_GetRayDat(vol_p, y, s, r, &d_p) ) {
	    fprintf(stderr, "%s %s: could not get data for ray %d.\n",
		    argv0, argv1, r);
	    goto error;
	}
    }
    lists = CALLOC((size_t)(num_bnds + num_rays * num_bins), sizeof(int));
    if ( !lists ) {
	fprintf(stderr, "%s: could not allocate color lists.\n", argv0);
	goto error;
    }

    /*
       Print outlines of gates for each color.
       Skip TRANSPARENT color.
     */

    BiSearch_FDataToList(dat, num_rays * num_bins, bnds, num_bnds, lists);
    for (c = 0; c < num_colors; c++) {
	if ( strcmp(colors[c], "none") != 0 ) {
	    printf("color %s\n", colors[c]);
	    for (d = BiSearch_1stIndex(lists, c);
		    d != -1;
		    d = BiSearch_NextIndex(lists, d)) {
		r = d / num_bins;
		b = d % num_bins;
		if ( ppi ) {
		    sig_stat = Sigmet_Vol_PPI_BinOutl(vol_p, s, r, b,
			    lonlat_to_xy, cnr);
		    if ( sig_stat != SIGMET_OK ) {
			fprintf(stderr, "%s %s: could not obtain outline for "
				"sweep %d, ray %d, bin %d.\n%s\n",
				argv0, argv1, s, r, b, sigmet_err(sig_stat));
		    }
		} else {
		    sig_stat = Sigmet_Vol_RHI_BinOutl(vol_p, s, r, b, cnr);
		    if ( sig_stat != SIGMET_OK ) {
			fprintf(stderr, "%s %s: could not obtain outline for "
				"sweep %d, ray %d, bin %d.\n%s\n",
				argv0, argv1, s, r, b, sigmet_err(sig_stat));
		    }
		}
		printf("poly %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n",
			cnr[0], cnr[1], cnr[2], cnr[3], cnr[4],
			cnr[5], cnr[6], cnr[7]);
	    }
	}
    }

    client_detach(vol_p, ax_sem_id);
    vol_p = NULL;
    FREE(colors);
    FREE(bnds);
    FREE(lists);
    FREE(dat);
    return 1;

error:
    if ( vol_p ) {
	client_detach(vol_p, ax_sem_id);
    }
    FREE(colors);
    FREE(bnds);
    FREE(lists);
    FREE(dat);
    return 0;
}

static int dorade_cb(int argc, char *argv[])
{
    char *argv0 = argv[0];
    char *argv1 = argv[1];
    struct Sigmet_Vol *vol_p;
    int ax_sem_id;
    int num_sweeps;
    int s;				/* Index of desired sweep,
					   or -1 for all */
    char *s_s;				/* String representation of s */
    int all = -1;
    enum SigmetStatus sig_stat;		/* Return from a Sigmet function */
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
    if ( !(vol_p = client_attach(&ax_sem_id)) ) {
	fprintf(stderr, "%s %s: could not attach to volume in shared "
		"memory.\n", argv0, argv1);
	return 0;
    }
    num_sweeps = Sigmet_Vol_NumSweeps(vol_p);
    if ( s >= num_sweeps ) {
	fprintf(stderr, "%s %s: sweep index %d out of range for volume\n",
		argv0, argv1, s);
	client_detach(vol_p, ax_sem_id);
	return 0;
    }
    if ( s == all ) {
	for (s = 0; s < num_sweeps; s++) {
	    Dorade_Sweep_Init(&swp);
	    if ( (sig_stat = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
		fprintf(stderr, "%s %s: could not translate sweep %d of volume "
			"to DORADE format\n%s\n", argv0, argv1, s,
			sigmet_err(sig_stat));
		goto error;
	    }
	    if ( !Dorade_Sweep_Write(&swp) ) {
		fprintf(stderr, "%s %s: could not write DORADE file for sweep "
			"%d of volume\n", argv0, argv1, s);
		goto error;
	    }
	    Dorade_Sweep_Free(&swp);
	}
	client_detach(vol_p, ax_sem_id);
    } else {
	Dorade_Sweep_Init(&swp);
	if ( (sig_stat = Sigmet_Vol_ToDorade(vol_p, s, &swp)) != SIGMET_OK ) {
	    fprintf(stderr, "%s %s: could not translate sweep %d of volume to "
		    "DORADE format\n%s\n", argv0, argv1, s,
		    sigmet_err(sig_stat));
	    goto error;
	}
	client_detach(vol_p, ax_sem_id);
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
	case SIGMET_MEM_FAIL:
	    return "Memory failure.";
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
