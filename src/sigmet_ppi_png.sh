#!/bin/sh
#
# If CLSEM, SIGMET_RAW_VOL_SZ, SIGMET_SEM_MEM, and SIGMET_SEM_PROC
# are set in the environment, this script will use the clsem
# utility to announce its completion.
#
# Optional environment variables:
#	SIGMET_SEM_PROC			- value = identifier for a semaphore
#					  assumed to be decremented for this
#					  process. When this process terminates,
#					  the associated semaphore will be
#					  incremented with a call to clsem.
#	SIGMET_SEM_MEM			- value = identifier for a semaphore
#					  assumed to be decremented by the
#					  number of megabytes this process will
#					  need to store the volume. When this
#					  process terminates, the associated
#					  semaphore will be incremented with a
#					  call to clsem.
#	SIGMET_RAW_VOL_SZ		- value = estimated number of megabytes
#					  this process will need to store the
#					  volume, needed by clsem if
#					  SIGMET_SEM_MEM is set.

CLSEM=${CLSEM:-":"}

# Clean up on exit
cleanup() {
    if test -S sock
    then
	sigmet_raw unload sock
    fi
    $CLSEM 1 $SIGMET_SEM_PROC
    $CLSEM $SIGMET_RAW_VOL_SZ $SIGMET_SEM_MEM
}
trap " cleanup; " EXIT
trap " echo $0 exiting on TERM signal;exit 1; " TERM
trap " echo $0 exiting on KILL signal;exit 1; " KILL
trap " echo $0 exiting on QUIT signal;exit 1; " QUIT
trap " echo $0 exiting on INT signal;exit 1; " INT
trap " echo $0 exiting on HUP signal;exit 1; " HUP

if [ $# -ne 3 ]
then
    echo Usage: $0 data_types sweep_angles volume_file
    exit 1
fi
data_types=$1
sweep_angles="$2"
vol_fl="$3"

gunzip -c "$vol_fl" | sigmet_raw load - sock
for a in $sweep_angles
do
    for data_type in $data_types
    do
	s=`sigmet_raw near_sweep $a sock`
	if ! test $s
	then
	    echo "Could not determine sweep index" 1>&2
	    exit 1
	fi
	eval `sigmet_raw sweep_headers sock | awk '
	    /sweep +'$s' / {
		printf "tm=\"%s %s\"\na1=%f\n", $3, $4, $5
	    }'`
	img_nm=`printf '%s_%s_%05.1f.png' $base_nm $data_type $a1`
	sigmet_ppi_svg $data_type $s sock | rsvg-convert -o $img_nm
    done
done
sigmet_raw unload sock
exit 0
