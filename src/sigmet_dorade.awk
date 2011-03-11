#!/usr/bin/awk -f
#
# Read volume identifiers and corrections from standard input.
# Print commands to read each volume, apply corrections, and
# convert to sweep files.
#
# Volumes must be in SIGMET_RAW_DIR
# Sweep directories will be made in SIGMET_SWEEP_DIR
#
# sigmet_dorade and sigmet_raw must be installed and in current path.

BEGIN {
    vol_dir = ENVIRON["SIGMET_RAW_DIR"]
    sweep_dir = ENVIRON["SIGMET_SWEEP_DIR"]
    printf "echo Sigmet raw volumes are in %s.\n", vol_dir
    printf "echo Sweep directories will be made in %s\n", sweep_dir
    have_daz = 0
    have_lat_lon = 0
}

/^[0-9]+ [0-9]+\/[0-9]+\/[0-9]+ [0-9]+:[0-9]+:[0-9]+ [^[:space:]]+ START DAZ/ {
    # Start applying an azimuth correction
    daz = $7
    have_daz = 1
    printf "export SIGMET_DAZ=%f\n", daz
}

/^[0-9]+ [0-9]+\/[0-9]+\/[0-9]+ [0-9]+:[0-9]+:[0-9]+ [^[:space:]]+ END   DAZ/ {
    # Stop applying an azimuth correction
    if ( !have_daz || $7 != daz ) {
	printf("Found end of azimuth correction without corresponding start.\n")
	printf("Last line of input was\n%s\n", $0)
	exit 1
    }
    have_daz = 0
    print "unset SIGMET_DAZ"
}

/^[0-9]+ [0-9]+\/[0-9]+\/[0-9]+ [0-9]+:[0-9]+:[0-9]+ [^[:space:]]+$/ {
    # Convert a volume
    vol = $4
    printf "echo Making sweep files for %s\n", vol
    printf "bkdir=`pwd`\n"
    d = sweep_dir "/" vol
    printf "mkdir -p %s\n", d
    printf "cd %s\n", d
    printf "if test $SIGMET_DAZ;"
    printf "then echo az shift $SIGMET_DAZ >> corrections;"
    printf "fi\n"
    printf "sigmet_dorade %s\n", vol_dir "/" vol
    printf "cd $bkdir\n"
}
