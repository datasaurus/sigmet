#!/bin/sh
#
# Usage:
#	sigmet_svg [-f] [-b bounds]
#		[-w document_width] [-z font_size] [-l legend_width]
#		[-m margins] [-o image_format]
#		data_types sweep_index volume_file
# where:
#	-f		- fill gaps between adjacent rays.
#	-b		- Limits of plot in plot coordinates (not display
#			  coordinates). bounds must be a string of form
#			  x_min=value,x_max=value,y_min=value,y_max=value
#			  Does not have to give all values. For values not
#			  given, default is to use sweep bounds.
#	-w		- document width, pixels
#	-z		- font size, pixels
#	-l		- width of color cells in legend, pixels
#	-m		- margins around plot area, given as
#			  top=value,left=value. Right and bottom margins
#			  are determined from font and legend sizes.
#	-o		- image format, svg, tiff, or png.
#	data_types	- data type or types (e.g. DB_DBZ, 'DB_DBZ,DB_ZDR').
#	sweep_angle	- desired tilt or azimuth. Image will show sweep
#			  nearest sweep_angle.
#	volume_file	- Sigmet raw product file
#
# For each data type in data_types, a color table will be sought from a file
# named ${data_type}.clrs or ${SIGMET_COLOR_DIR}/${data_type}.clrs.
#
# Copyright (c) 2012, Gordon D. Carrie. All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
#     * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Please send feedback to dev0@trekix.net

# This function checks whether a value is a number
check_num() {
    name="$1"
    val="$2"
    if ! test $val
    then
	printf "%s not set\n" "$name" 1>&2
	exit 1
    fi
    if ! printf '%g' $val > /dev/null 2>&1
    then
	printf "$0: expected number for %s, got %s\n" "$name" "$val" 1>&2
	exit 1
    fi
}

# Defaults
fill=
x_min=
x_max=
y_min=
y_max=
doc_width=1400
font_sz=18
legend_width=64
top=64
left=198
output=svg

# Parse command line
while getopts :fb:w:z:l:m:o: opt
do
    case "$opt"
    in
	f)
	    fill="-f"
	    ;;
	b)
	    eval `echo "$OPTARG" | sed 's/,/;/g'`
	    ;;
	w)
	    doc_width=$OPTARG
	    ;;
	z)
	    font_sz=$OPTARG
	    ;;
	l)
	    legend_width=$OPTARG
	    ;;
	m)
	    eval `echo "$OPTARG" | sed 's/,/;/g'`
	    ;;
	o)
	    output=$OPTARG
	    ;;
	\?)
	    echo "$0: unknown option $OPTARG" 1>&2
	    exit 1
	    ;;
    esac
done

if test $x_min; then check_num "x_min" $x_min; fi
if test $x_max; then check_num "x_max" $x_max; fi
if test $y_min; then check_num "y_min" $y_min; fi
if test $y_max; then check_num "y_max" $y_max; fi
check_num "display width" $doc_width
check_num "font size" $font_sz
check_num "legend width" $legend_width
check_num "top margin" $top
check_num "left margin" $left

shift `expr $OPTIND - 1`
if [ $# -ne 3 ]
then
    {
	printf "Usage: $0 [-f] [-b bounds] [-w document_width] [-z font_size]"
	printf " [-l legend_width] [-m margins] [-o image_format] -- "
	printf " data_type[,data_type,...] sweep_angle volume_file\n"
    } 1>&2
    exit 1
fi
data_types=`echo "$1" | sed 's/,/ /g'`
sweep_angle="$2"
vol_path="$3"

# Arrange for sigmet_raw termination and clean up on any exit.
# sigmet_raw is started below.
cleanup() {
    echo > $out &
    cat $out > /dev/null
    echo exit > $in
    rm -f $in $out
}
trap cleanup EXIT
trap cleanup QUIT
trap cleanup INT
trap cleanup KILL

# Load the sigmet volume into memory by running sigmet_raw in daemon mode.
# Commands that need information from the daemon will write requests to the
# "in" fifo, and read results from "out" fifo using the sig_raw function.
vol_fl=`basename $vol_path`
vol=`echo $vol_fl | sed -e 's/\.gz$//' -e 's/\.bz2$//'`
in=.${vol}.$$.in
out=.${vol}.$$.out
mkfifo $in $out
sigmet_raw ${vol_path} $in &

# Store volume headers
sig_raw $in $out vol_hdr
eval `cat $out`

# Assign sweep bounds to x_min, x_max, y_min, and y_max if not set from
# command line.
sweep_index=`sig_raw $in $out near_sweep $sweep_angle`
eval `sig_raw $in $out sweep_bnds $sweep_index | awk '
    /x_min/ {
	x_min = $2;
    }
    /x_max/ {
	x_max = $2;
    }
    /y_min/ {
	y_min = $2;
    }
    /y_max/ {
	y_max = $2;
    }
    END {
	printf "if ! test $x_min;then x_min=%s;fi; ", x_min;
	printf "if ! test $x_max;then x_max=%s;fi; ", x_max;
	printf "if ! test $y_min;then y_min=%s;fi; ", y_min;
	printf "if ! test $y_max;then y_max=%s;fi;\n", y_max;
    }'`
check_num "sweep x min" $x_min
check_num "sweep x max" $x_max
check_num "sweep y min" $y_min
check_num "sweep y max" $y_max
dx=`echo "$x_max - $x_min" | bc -l`
dy=`echo "$y_max - $y_min" | bc -l`

# Determine actually sweep angle.
eval `sig_raw $in $out sweep_headers | awk '/sweep +'$sweep_index' / {
	    printf "sweep_time=\"%s %s\";sweep_angle=%s\n", $3, $4, $5
	}'`

# Determine remaining dimensions of document and plot
if ! test $right
then
    right=`echo "$legend_width + 12 * $font_sz" | bc -l`
fi
if ! test $bottom
then
    bottom=`echo "12 * $font_sz" | bc -l`
fi
plot_width=`echo "$doc_width - $left - $right" | bc -l`
plot_height=`echo "$plot_width * $dy / $dx" | bc -l`
doc_height=`echo "$plot_height + $top + $bottom" | bc -l`
legend_height=`echo "$plot_height / 2" | bc -l`

# Axis labels, depend on scan type
if [ "$scan_mode" = "rhi" ]
then
    x_label='Distance down range (m)'
else
    x_label='East-west (m)'
fi
if [ "$scan_mode" = "rhi" ]
then
    y_label='Height (m)'
else
    y_label='North-south (m)'
fi

# Convenience functions
cat_svg() { cat > $1; }
rsvg_convert() { rsvg-convert /dev/stdin > $1; }

# Send SVG plot parameters and data to the xyplot.awk command.
for data_type in $data_types
do
    vol=`basename "$vol_path"`
    caption="$site_name $data_type at $sweep_time $sweep_angle degrees"
    out_fl=`printf "%s_%s_%.1f.%s" $vol $data_type $sweep_angle $output`
    case $output in
	svg)
	    out_cmd="cat_svg $out_fl"
	    ;;
	tiff)
	    out_cmd="convert - $out_fl"
	    ;;
	png)
	    out_cmd="rsvg_convert $out_fl"
	    ;;
	*)
	    echo "Output format must be one of svg, tiff, or png" 1>&2
	    exit 1
	    ;;
    esac
    color_fl=${data_type}_CLRS

    # Find color table file
    if test -f ${data_type}.clrs
    then
	color_fl=${data_type}.clrs
    elif test -f ${SIGMET_COLOR_DIR}/${data_type}.clrs
    then
	color_fl=${SIGMET_COLOR_DIR}/${data_type}.clrs
    else
	echo "$0: could not find color file for $data_type" 1>&2
	exit 1
    fi
    
    {
	printf 'x0=%f\n'	$x_min
	printf 'y0=%f\n'	$y_min
	printf 'dx=%f\n'	$dx
	printf 'dy=%f\n'	$dy
	printf 'doc_width=%d\n' $doc_width
	printf 'top=%d\n'	$top
	printf 'right=%d\n'	$right
	printf 'bottom=%d\n' $bottom
	printf 'left=%d\n'	$left
	printf 'font_sz=%d\n' $font_sz

	echo start_plot

	sig_raw $in $out outlines $fill $data_type $color_fl $sweep_index \
	| sigmet_svg.awk

	echo end_plot

	# Caption for x axis
	x_px=`echo "$left + $plot_width / 2" | bc -l`
	y_px=`echo "$top + $plot_height + 5 * $font_sz" | bc -l`
	printf '<text\n'
	printf '    x="%.1f" y="%.1f"\n' $x_px $y_px
	printf '    font-size="%.0f"\n' $font_sz
	printf '    text-anchor="middle">'
	printf '%s' "$x_label"
	printf '</text>\n'

	# Label y axis
	x_px=`echo "$left - 8 * $font_sz" | bc -l`
	y_px=`echo "$top + $plot_height / 2" | bc -l`
	printf '<g transform="matrix(0.0, -1.0, 1.0, 0.0,'
	printf ' %.1f, %.1f)">\n' $x_px $y_px
	printf '    <text\n'
	printf '        x="0.0" y="0.0"\n'
	printf '        font-size="%.0f"\n' $font_sz
	printf '        text-anchor="middle">'
	printf '%s' "$y_label"
	printf '</text>\n'
	printf '</g>\n'

	# Figure caption
	x_px=`echo "$left + $plot_width / 2" | bc -l`
	y_px=`echo "$top + $plot_height + 8.5 * $font_sz" | bc -l`
	printf '<text\n'
	printf '    x="%.1f" y="%.1f"\n' $x_px $y_px
	printf '    font-size="%.0f"\n' $font_sz
	printf '    text-anchor="middle">'
	printf '%s' "$caption"
	printf '</text>\n'

	# Color legend
	x_px=`echo "$left + $plot_width + 18" | bc -l`
	y_px=`echo "$top + 9" | bc -l`
	printf '<g transform="translate(%.1f %.1f)">\n' $x_px $y_px
	color_legend $legend_width $legend_height $font_sz < $color_fl
	printf '</g>\n'

	echo end

    } | xyplot.awk | $out_cmd
    echo $out_fl
done

exit 0
