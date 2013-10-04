#!/bin/sh
#
# Create a directory for a Sigmet volume with sweep files for it.
#
# Usage:
#	sigmet_dorade.sh sigmet_volume
#
# sigmet_volume must be absolute path of a Sigmet raw product file.
# PATH must contain sigmet_dorade and sigmet_raw

if [ $# -ne 1 ]
then
    echo "Usage: $0 path" 1>&2
    exit 1
fi
raw_fl=$1
dir="`basename $raw_fl`"
mkdir -p $dir
(
    cd $dir
    sigmet_dorade $raw_fl
)
