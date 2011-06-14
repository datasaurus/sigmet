#!/bin/sh
#
# sigmet_imgs.sh --
#	Run sigmet_img for list of volumes from command line
#
# Usage:
#	sigmet_imgs.sh data_types sweep_angles raw_fl raw_fl raw_fl ...

if [ $# -ne 2 ]
then
    echo 'Usage: echo raw_fl raw_fl ... | $0 data_types sweep_angles raw_fl'
    exit 1
fi
data_types=$1
shift
sweep_angles=$1
shift

export SIGMET_RAW_IMG_APP=$HOME/bin/gdpoly
top=${HOME}/trekix/devel/project/sigmet/libexec
export SIGMET_RAW_COLORS_DB_DBT=${top}/zcolors
export SIGMET_RAW_COLORS_DB_DBZ=${top}/zcolors
export SIGMET_RAW_COLORS_DB_VEL=${top}/vcolors
export SIGMET_RAW_COLORS_DB_WIDTH=${top}/wcolors
export SIGMET_RAW_IMG_SZ=600
export SIGMET_RAW_ALPHA=1.0
export SIGMET_RAW_PROJ="proj -b +proj=utm +zone=14 +ellps=WGS84 +datum=WGS84 \
			+units=m +no_defs"

while read raw_fl
do
    raw_fl_id=`basename $raw_fl`
    echo Processing $raw_fl
    echo Processing $raw_fl > ${raw_fl_id}.out
    sigmet_img "$data_types" "$sweep_angles" $raw_fl >> ${raw_fl_id}.out 2>&1
done
