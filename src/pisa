#!/bin/sh
#
# This script creates an SVG document with a Cartesian plot, axes, labels,
# and optional elements.  See pisa (1).
#
# Copyright (c) 2013 Gordon D. Carrie
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# Please send feedback to dev0@trekix.net
#
# $Revision: 1.17 $ $Date: 2014/06/06 17:11:18 $
#
########################################################################

if test $DEBUG
then
    set -x
fi

# Check for numerical value
check_num() {
    name="$1"
    val="$2"
    if ! printf '%g' $val > /dev/null 2>&1
    then
	printf "$0: expected number for %s, got %s\n" "$name" "$val" 1>&2
	exit 1
    fi
}

# _dpy denotes display coordinates.
# Otherwise, variable specifies plot coordinates.

# Set some defaults
frag="document"
font_sz="9.0"	
left="`echo $font_sz \* 12 | bc -l`"
right="`echo $font_sz \* 6 | bc -l`"
top="`echo $font_sz \* 3 | bc -l`"
bottom="`echo $font_sz \* 6 | bc -l`"
doc_width="800"
doc_height=
x_prx="3"
y_prx="3"

# Parse command line
while getopts :d:l:r:t:b:w:h:f:m:n:X:Y:p:s:y: opt
do
    case $opt
    in
	d)
	    frag="$OPTARG"
	    ;;
	l)
	    left=$OPTARG
	    check_num "left margin" $left
	    ;;
	r)
	    right=$OPTARG
	    check_num "right margin" $right
	    ;;
	t)
	    top=$OPTARG
	    check_num "top margin" $top
	    ;;
	b)
	    bottom=$OPTARG
	    check_num "bottom margin" $bottom
	    ;;
	w)
	    doc_width=$OPTARG
	    check_num "document width" $doc_width
	    ;;
	h)
	    doc_height=$OPTARG
	    check_num "document height" $doc_height
	    ;;
	f)
	    font_sz=$OPTARG
	    check_num "font size" $font_sz
	    ;;
	m)
	    x_prx="$OPTARG"
	    ;;
	n)
	    y_prx="$OPTARG"
	    ;;
	X)
	    x_title="$OPTARG"
	    ;;
	Y)
	    y_title="$OPTARG"
	    ;;
	p)
	    # Separate paths with | since path names can contain whitespace.
	    prefixes=${prefixes}"${prefixes:+|}$OPTARG"
	    ;;
	s)
	    # Separate paths with | since path names can contain whitespace.
	    suffixes=${suffixes}"${suffixes:+|}$OPTARG"
	    ;;
	y)
	    style_sheets=${style_sheets}"${style_sheets:+|}$OPTARG"
	    ;;
	\?)
	    echo "$0: unknown option $OPTARG" 1>&2
	    exit 1
	    ;;
    esac
done
shift `expr $OPTIND - 1`
if [ $# -ne 4 ]
then
    {
	printf "Usage: $0 [-d fragment] [-l pixels] [-r pixels]"
	printf " [-t pixels] [-b pixels] [-w pixels] [-h pixels] "
	printf " [-f pixels] [-m precison] [-n precison]"
	printf " [-p prefix_file] [-s suffix_file] [-y style_sheet]"
	printf " x_left x_rght y_btm y_top\n"
    } 1>&2
    exit 1
fi

# Validate input
x_left=$1; shift
check_num "x coordinate at left side of plot" $x_left
x_rght=$1; shift
check_num "x coordinate at right side of plot" $x_rght
y_btm=$1; shift
check_num "y coordinate of bottom edge of plot" $y_btm
y_top=$1; shift
check_num "y coordinate of top edge of plot" $y_top

# Send information to pisa.awk
{
    {
	IFS=\|;
	for sheet in $style_sheets
	do
	    echo style="$sheet"
	done;
    }
    echo fragment="$frag"
    echo x_left=$x_left
    echo x_rght=$x_rght
    echo y_btm=$y_btm
    echo y_top=$y_top
    echo doc_width=$doc_width
    if test $doc_height
    then
	echo doc_height=$doc_height
    fi
    echo top=$top
    echo right=$right
    echo bottom=$bottom
    echo left=$left
    if test "$x_title"
    then
	echo x_title="$x_title"
    fi
    if test "$y_title"
    then
	echo y_title="$y_title"
    fi
    echo font_sz=$font_sz
    echo x_prx=$x_prx
    echo y_prx=$y_prx
    if test "$prefixes"
    then
	echo 'start_doc'
	{ IFS=\|; cat $prefixes; }
    fi
    echo 'start_plot'
    cat
    echo 'end_plot'
    if test "$suffixes"
    then
	{ IFS=\|; cat $suffixes; }
    fi
    echo 'end'
} | pisa.awk
exit $?

