#!/usr/bin/awk -f
#
#	pisa.awk --
#		Create a SVG document with a cartesian plot in it.
#
################################################################################
#
# Copyright (c) 2013, Gordon D. Carrie. All rights reserved.
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
#
# $Revision: 1.49 $ $Date: 2014/06/06 17:11:18 $
#
################################################################################

# Standard input must include:
#
#	x_left number
#	x_rght number
#	y_btm number
#	y_top number
#	doc_width number
#	top number
#	right number
#	bottom number
#	left number
#	font_sz number
#	start_plot
#	end_plot
#	end
#
# Standard input may also include:
#
#	doc_height number
#	x_title string
#	y_title string
#	start_doc
#
# where:
#	
#	x_left		Cartesian x coordinate of left edge of plot.
#	x_rght		Cartesian x coordinate of right edge of plot.
#	y_btm		Cartesian y coordinate of bottom edge of plot.
#	y_top		Cartesian y coordinate of top edge of plot.
#	doc_width	document width in pixels.
#	doc_height	(optional) document height, in pixels.
#			If absent, document height will be set so that ratio of
#			plot width to plot height in pixels equals ratio of plot
#			width to plot height in Cartesian coordinates.
#	top		size of the margin above the plot, in pixels.
#	right		size of the margin right of the plot, in pixels.
#	bottom		size of the margin below the plot, in pixels.
#	left		size of the margin left of the plot, in pixels.
#	font_sz		font size for labels, in pixels.
#	x_prx		number of significant figures in x axis labels.
#	y_prx		number of significant figures in y axis labels.
#	x_title		(optional) x axis label. Cannot contain "=".
#	y_title		(optional) y axis label. Cannot contain "=".
#	start_doc	(optional) indicates no more parameters. The process
#			will now just pass input to output. Subsequent input
#			should be SVG code for items under the plot. Coordinates
#			for these items must be in pixels.
#	start_plot	indicates no more parameters. The process will print
#			SVG code setting up the Cartesian coordinate system and
#			then continue passing input to output. Subsequent input
#			should be SVG code for items in the plot. Coordinates
#			for these items must be Cartesian.
#	end_plot	indicates no more plot input. The process prints SVG
#			code to terminate transformation to Cartesian
#			coordinates and then it finishes plot. Then it resumes
#			passing input to output, allowing additional SVG
#			elements on top of the plot. Coordinates for these
#			additional elements, e.g. extra labels, color tables,
#			markers, etc. must be pixels.
#	end		indicates no more input. The process prints
#			SVG code to finish the document, and exits.
#
################################################################################

# Initialize parameters with bogus values or reasonable defaults
BEGIN {
    FS = "=";
    title = "";
    num_sheets = 0;
    printing = 0;
    have_header = 0;
    doc_width = 800.0;
    doc_height = "nan";
    top = 0.0;
    right = 0.0;
    bottom = 0.0;
    left = 0.0;
    x_left = "nan";
    x_rght = "nan";
    y_btm = "nan";
    y_top = "nan";
    x_prx = "3";
    y_prx = "3";
    x_title = "";
    y_title = "";
    font_sz = 12.0;
    tick_len = 0.5 * font_sz
    pad = 0.5 * font_sz;		# Separator between axis elements
    err = "/dev/stderr";
}

# Set parameters from standard input
/^\s*title\s*=/ {
    title = $2;
}
/^\s*style\s*=/ {
    sheet[num_sheets] = $2;
    num_sheets++;
}
/^\s*doc_width\s*=\s*[0-9.Ee-]+\s*$/ {
    doc_width = $2 + 0.0;
    if ( doc_width <= 0.0 ) {
	printf "document width must be positive\n" > err;
	exit 1;
    }
}
/^\s*doc_height\s*=\s*[0-9.Ee-]+\s*$/ {
    doc_height = $2 + 0.0;
    if ( doc_height <= 0.0 ) {
	printf "document height must be positive\n" > err;
	exit 1;
    }
}
/^\s*top\s*=\s*[0-9.Ee-]+\s*$/ {
    top = $2 + 0.0;
    if ( top < 0.0 ) {
	printf "top margin cannot be negative" > err;
	exit 1;
    }
}
/^\s*right\s*=\s*[0-9.Ee-]+\s*$/ {
    right = $2 + 0.0;
    if ( right < 0.0 ) {
	printf "right margin cannot be negative" > err;
	exit 1;
    }
}
/^\s*bottom\s*=\s*[0-9.Ee-]+\s*$/ {
    bottom = $2 + 0.0;
    if ( bottom < 0.0 ) {
	printf "bottom margin cannot be negative" > err;
	exit 1;
    }
}
/^\s*left\s*=\s*[0-9.Ee-]+\s*$/ {
    left = $2 + 0.0;
    if ( left < 0.0 ) {
	printf "left margin cannot be negative" > err;
	exit 1;
    }
}
/^\s*x_left\s*=\s*[0-9.Ee-]+\s*$/ {
    x_left = $2 + 0.0;
}
/^\s*x_rght\s*=\s*[0-9.Ee-]+\s*$/ {
    x_rght = $2 + 0.0;
}
/^\s*y_btm\s*=\s*[0-9.Ee-]+\s*$/ {
    y_btm = $2 + 0.0;
}
/^\s*y_top\s*=\s*[0-9.Ee-]+\s*$/ {
    y_top = $2 + 0.0;
}
/^\s*font_sz\s*=\s*[0-9.Ee-]+\s*$/ {
    font_sz = $2 + 0.0;
    if ( font_sz <= 0.0 ) {
	printf "font size must be positive\n" > err;
	exit 1;
    }
}
/^\s*x_title\s*=/ {
    x_title = $2;
}
/^\s*y_title\s*=/ {
    y_title = $2;
}
/^\s*x_prx\s*=\s*[0-9.Ee-]+\s*$/ {
    x_prx = $2;
}
/^\s*y_prx\s*=\s*[0-9.Ee-]+\s*$/ {
    y_prx = $2;
}

# This function returns the next power of 10 greater than or equal to the
# magnitude of x.
function pow10(x)
{
    if (x == 0.0) {
	return 1.0e-100;
    } else if (x > 0.0) {
	n = int(log(x) / log(10) + 0.5);
	return exp(n * log(10.0));
    } else {
	n = int(log(-x) / log(10) + 0.5);
	return -exp(n * log(10.0));
    }
}

# Copy src array to dest
function copy_arr(dest, src)
{
    for (i in dest) {
	delete dest[i];
    }
    for (i in src) {
	dest[i] = src[i];
    }
}

# axis_lbl --
#	This function determines axis label locations.
#	x_lo	(in)	start of axis.
#	x_hi	(in)	end of axis.
#	prx	(in) 	number of significant digits in each label.
#	n_max	(in)	number of characters allowed for all labels.
#	orient  (in)	orientation, "h" or "v" for horizontal or vertical.
#	labels	(out)	Label coordinates and strings are returned in labels.
#			Each index is an x coordinate. Corresponding value is
#			the string to print there.
#	l0, l1, dx, t	local variables
#
function axis_lbl(x_lo, x_hi, prx, n_max, orient, labels,
	l0, l1, dx, t)
{
    if ( x_hi < x_lo ) {
	t = x_hi;
	x_hi = x_lo;
	x_lo = t;
    }

#   Put a tentative number of labels into l0.
#   Put more labels into l1. If l1 would need more than n_max
#   characters, return l0. Otherwise, copy l1 to l0 and try
#   a more populated l1.
    fmt="%."prx"g";
    l0[x_lo] = sprintf(fmt, x_lo);
    if ( x_lo == x_hi || length(sprintf(fmt, x_lo)) > n_max ) {
	copy_arr(labels, l0);
	return;
    }
    l0[x_hi] = sprintf(fmt, x_hi);
    if ( length(sprintf(fmt " " fmt, x_lo, x_hi)) > n_max ) {
	copy_arr(labels, l0);
	return;
    }

#   Initialize the interval dx to a power of 10 larger than the interval
#   from x_hi - x_lo, then try smaller steps until all of the labels with
#   a space character between them fit into n_max characters. The interval
#   will be a multiple of 10, 5, or 2 times some power of 10.
    dx = pow10(x_hi - x_lo);
    while (1) {
	if ( mk_lbl(x_lo, x_hi, dx, fmt, orient, l1) > n_max ) {
	    copy_arr(labels, l0);
	    return;
	} else {
	    copy_arr(l0, l1);
	}
	dx *= 0.5;
	if ( mk_lbl(x_lo, x_hi, dx, fmt, orient, l1) > n_max ) {
	    copy_arr(labels, l0);
	    return;
	} else {
	    copy_arr(l0, l1);
	}
	dx *= 0.4;
	if ( mk_lbl(x_lo, x_hi, dx, fmt, orient, l1) > n_max ) {
	    copy_arr(labels, l0);
	    return;
	} else {
	    copy_arr(l0, l1);
	}
	dx *= 0.5;
    }
}

# Functions for floor and ceiling
function floor(x)
{
    return (x > 0) ? int(x) : int(x) - 1;
}
function ceil(x) {
    return (x > 0) ? int(x) + 1 : int(x);
}

# Print labels from x_lo to x_hi with increment dx and print format fmt
# to a string. Assign the label coordinates and label strings to the labels
# array.  Each index in labels array will be an x coordinate. Array value will
# be the label to print there. If orient is "h", return the length of the
# string containing all labels. Otherwise, assume the axis is vertical and
# return the number of labels.

function mk_lbl(x_lo, x_hi, dx, fmt, orient, labels, l, x0, n, n_tot)
{
    for (l in labels) {
	delete labels[l];
    }
    x0 = floor(x_lo / dx) * dx;
    x_lo -= dx / 4;
    x_hi += dx / 4;
    for (n = n_tot = 0; n <= ceil((x_hi - x_lo) / dx); n++) {
	x = x0 + n * dx;
	if ( x >= x_lo && x <= x_hi ) {
	    labels[x] = sprintf(fmt, x);
	    if ( orient == "h" ) {
		n_tot += length(labels[x]);
	    } else {
		n_tot++;
	    }
	}
    }
    return n_tot;
}

# fabs (3)
function fabs(x)
{
    return (x > 0.0) ? x : -x;
}

# Validate document and plot dimensions. Print document header.
function print_header()
{
    if ( have_header ) {
	return;
    }   
    if ( x_left == "nan" ) {
	printf "x_left not set\n" > err;
	exit 1;
    }
    if ( x_rght == "nan" ) {
	printf "x_rght not set\n" > err;
	exit 1;
    }
    if ( y_btm == "nan" ) {
	printf "y_btm not set\n" > err;
	exit 1;
    }
    if ( y_top == "nan" ) {
	printf "y_top not set\n" > err;
	exit 1;
    }
    if ( x_rght == x_left ) {
	printf "Left and right sides cannot have same x coordinate.\n" > err;
	exit 1;
    }
    if ( y_top == y_btm ) {
	printf "Top and bottom cannot have same y coordinate.\n" > err;
	exit 1;
    }

#   Determine space requirements for axis titles
    if ( length(x_title) > 0 ) {
	x_title_ht = pad + font_sz;
    } else {
	x_title_ht = 0.0;
    }
    if ( length(y_title) > 0 ) {
	y_title_width = font_sz + pad;
    } else {
	y_title_width = 0.0;
    }

#   Space below plot will have tick mark, padding, label, and possibly title.
    x_axis_ht_px = tick_len + pad + font_sz;
    below_plot = x_axis_ht_px + x_title_ht + bottom;

#   Make first guess at plot width and height
    plot_x_px = left;
    plot_width_px = doc_width - left - right;
    if ( plot_width_px <= 0 ) {
	printf "Negative plot width.\n" > err;
	exit 1;
    }
    if ( doc_height == "nan" ) {
	r = fabs((y_top - y_btm) / (x_rght - x_left));
	plot_height_px = plot_width_px * r;
    } else {
	plot_height_px = doc_height - top - below_plot;
	if ( plot_height_px <= 0 ) {
	    printf "Negative plot height.\n" > err;
	    exit 1;
	}
    }

#   Create a first guess set of labels for the y axis. From this set, determine
#   width needed for y axis labels, tick marks, and title.
    n_max = plot_height_px / font_sz / 2;
    axis_lbl(y_btm, y_top, y_prx, n_max, "v", y_labels);
    max_len = 0.0;
    for (y in y_labels) {
	len = length(y_labels[y]);
	if ( len > max_len ) {
	    max_len = len;
	}
    }
    y_axis_extra = 2.0 * font_sz + pad;
    y_axis_y_px = top - y_axis_extra / 2.0;
    y_axis_width_px = font_sz * max_len + tick_len;
    y_axis_x_px = left + y_title_width;

#   Adjust left margin so that it includes user specified margin plus space
#   needed for y axis element. Recompute plot width and height for the new
#   left margin. Recompute labels for the new plot height. Assume, perhaps
#   naively, that space needs for the y axis do not change. This could be a bug.
    plot_x_px = left + y_title_width + y_axis_width_px;
    plot_width_px = doc_width - plot_x_px - right;
    if ( plot_width_px <= 0 ) {
	printf "Negative plot width.\n" > err;
	exit 1;
    }
    if ( doc_height == "nan" ) {
	r = fabs((y_top - y_btm) / (x_rght - x_left));
	plot_height_px = plot_width_px * r;
	doc_height = plot_height_px + top + below_plot;
    } else {
	plot_height_px = doc_height - top - below_plot;
	if ( plot_height_px <= 0 ) {
	    printf "Negative plot height.\n" > err;
	    exit 1;
	}
    }
    y_axis_height_px = plot_height_px + y_axis_extra;
    n_max = 0.5 * plot_height_px / font_sz;
    axis_lbl(y_btm, y_top, y_prx, n_max, "v", y_labels);

#   Create a set of labels for the x axis;
    px_per_x = plot_width_px / (x_rght - x_left);
    n_max = 0.5 * plot_width_px / font_sz;
    axis_lbl(x_left, x_rght, x_prx, n_max, "h", x_labels);

#   Compute geometry for x axis. Add space at ends so that labels can
#   extend beyond plot as needed.
    max_len = 0.0;
    for (x in x_labels) {
	len = length(x_labels[x]);
	if ( len > max_len ) {
	    max_len = len;
	}
    }
    x_axis_extra = max_len * font_sz;
    x_axis_x_px = plot_x_px - x_axis_extra / 2;
    x_axis_width_px = plot_width_px + x_axis_extra;
    x_axis_y_px = top + plot_height_px;

#   Compute transform matrix for plot area. This will convert Cartesian
#   coordinates to pixels.
#        x_px = a * x_cart + e
#        y_px = d * y_cart + f
#   Ref: http://www.w3.org/TR/SVG11/coords.html#TransformMatrixDefined
    a = plot_width_px / (x_rght - x_left);
    c = 0.0;
    e = -plot_width_px * x_left / (x_rght - x_left);
    b = 0.0;
    d = -plot_height_px / (y_top - y_btm);
    f = plot_height_px * y_top / (y_top - y_btm);

#   Initialize the SVG document
    printf "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    for (s = 0; s < num_sheets; s++) {
	printf "<?xml-stylesheet href=\"%s\" type=\"text/css\"?>\n", sheet[s];
    }
    printf "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\"\n";
    printf "   \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n";
    printf "<svg\n";
    printf "    width=\"%f\"\n", doc_width;
    printf "    height=\"%f\"\n", doc_height;
    printf "    xmlns=\"http://www.w3.org/2000/svg\"\n";
    printf "    xmlns:xlink=\"http://www.w3.org/1999/xlink\"\n";
    printf "    id=\"outermost\">\n";
    if ( length(title) > 0 ) {
	printf "  <title>%s</title>\n", title;
    }
    have_header = 1;
}

# Start printing document. Print header.
# Print SVG elements that should precede, or go under, plot.
/^\s*start_doc\s*$/ {
    print_header();
    $0 = "";
    printing = 1;
}

# Print header if necessary. Start printing plot.
/^\s*start_plot\s*$/ {
    print_header();
    $0 = "";
    printing = 1;

#   Define plot area rectangle
    printf "\n<defs>\n";
    printf "  <!-- Plot area rectangle, for clipping and boundary -->\n";
    printf "  <!-- Use a separate rectangle for background, which will be\n";
    printf "       dragged in the plot area coordinate system, not SVG \n";
    printf "       document coordinates. -->\n";
    printf "  <rect\n";
    printf "      id=\"PlotRect\"\n";
    printf "      width=\"%f\"\n", plot_width_px;
    printf "      height=\"%f\" />\n", plot_height_px;
    printf "</defs>\n";

#   Define plot area clip path
    printf "<defs>\n";
    printf "  <!-- Clip path for plot area -->\n";
    printf "  <clipPath id=\"PlotArea\">\n";
    printf "    <use\n";
    printf "        xlink:href=\"#PlotRect\"\n";
    printf "        x=\"%f\"\n", plot_x_px;
    printf "        y=\"%f\" />\n", top;
    printf "  </clipPath>\n";

#   X axis geometry and clip path.
    printf "  <!-- Clip path for x axis labels -->\n";
    printf "  <clipPath\n";
    printf "    id=\"xAxisClip\">\n";
    printf "    <rect\n";
    printf "        id=\"xAxisClipRect\"\n";
    printf "        x=\"%f\"\n", x_axis_x_px;
    printf "        y=\"%f\"\n", x_axis_y_px;
    printf "        width=\"%f\"\n", x_axis_width_px;
    printf "        height=\"%f\"/>\n", x_axis_ht_px;
    printf "  </clipPath>\n";

#   Y axis geometry and clip path.
    printf "  <!-- Clip path for y axis labels -->\n";
    printf "  <clipPath\n";
    printf "    id=\"yAxisClip\">\n";
    printf "    <rect\n";
    printf "        id=\"yAxisClipRect\"\n";
    printf "        x=\"%f\"\n", y_axis_x_px;
    printf "        y=\"%f\"\n", y_axis_y_px;
    printf "        width=\"%f\"\n", y_axis_width_px;
    printf "        height=\"%f\" />\n", y_axis_height_px;
    printf "  </clipPath>\n";
    printf "</defs>\n";

#   Create plot area.
    printf "<!-- Clip path and SVG element for plot area -->\n";
    printf "<g clip-path=\"url(#PlotArea)\">\n";
    printf "  <svg\n";
    printf "      id=\"plot\"\n";
    printf "      x=\"%f\"\n", plot_x_px;
    printf "      y=\"%f\"\n", top;
    printf "      width=\"%f\"\n", plot_width_px;
    printf "      height=\"%f\">\n", plot_height_px;
    printf "\n";
    printf "<!-- Set user space for Cartesian coordinates -->\n";
    printf "<g id=\"cartG\" transform=\"matrix(%f %f %f %f %f %f)\">\n", \
	a, b, c, d, e, f;
    printf "\n";
    printf "    <!-- Fill in plot area background -->\n";
    printf "    <rect\n";
    printf "        id=\"plotBackground\"\n";
    printf "        x=\"%f\"\n", (x_rght > x_left) ? x_left : x_rght;
    printf "        y=\"%f\"\n", (y_top > y_btm) ? y_btm : y_top;
    printf "        width=\"%f\"\n", fabs(x_rght - x_left);
    printf "        height=\"%f\"\n", fabs(y_top - y_btm);
    printf "        fill=\"white\" />\n";
    printf "\n"
    printf "<!-- Define elements in plot area -->\n";
}

# When done plotting, terminate plot area. Draw axes and labels.
# Printing will continue, but subsequent elements will not use
# plot coordinates.
/^\s*end_plot\s*$/ {
    printf "<!-- Terminate transform to Cartesian coordinates-->\n"
    printf "</g>\n";
    printf "\n";
    printf "<!-- Done defining elements in plot area -->\n\n"
    printf "\n"
    printf "  <!-- Terminate SVG element for plot area -->\n"
    printf "  </svg>\n";
    printf "\n";
    printf "<!-- Terminate clipping for plot area -->\n"
    printf "</g>\n";
    printf "\n";
    printf "<!-- Draw boundary around plot area -->\n";
    printf "<use\n";
    printf "    id=\"PlotBoundary\"\n";
    printf "    xlink:href=\"#PlotRect\"\n";
    printf "    x=\"%f\"\n", plot_x_px;
    printf "    y=\"%f\"\n", top;
    printf "    fill=\"none\"\n";
    printf "    stroke=\"black\">\n";
    printf "</use>\n";
    printf "\n";

#   Draw and label x axis
    printf "<!-- Clip area and svg element for x axis and labels -->\n";
    printf "<g clip-path=\"url(#xAxisClip)\">\n";
    printf "  <svg\n";
    printf "      id=\"xAxis\"\n";
    printf "      x=\"%f\"\n", x_axis_x_px;
    printf "      y=\"%f\"\n", x_axis_y_px;
    printf "      width=\"%f\"\n", x_axis_width_px;
    printf "      height=\"%f\"\n", x_axis_ht_px;
    printf "      viewBox=\"%f %f %f %f\" >\n",
	   x_axis_x_px, x_axis_y_px, x_axis_width_px, x_axis_ht_px;
    for (x in x_labels) {
	x_px = plot_x_px + (x - x_left) * px_per_x;
	printf "  <line\n";
	printf "      class=\"x axis tick\"\n";
	printf "      x1=\"%f\"\n", x_px;
	printf "      x2=\"%f\"\n", x_px;
	printf "      y1=\"%f\"\n", x_axis_y_px;
	printf "      y2=\"%f\"\n", x_axis_y_px + tick_len;
	printf "      stroke=\"black\"\n"
	printf "      stroke-width=\"1\" />\n"
	printf "  <text\n";
	printf "      class=\"x axis label\"\n";
	printf "      x=\"%f\"\n", x_px;
	printf "      y=\"%f\"\n", x_axis_y_px + tick_len + pad + font_sz;
	printf "      font-size=\"%.1f\"\n", font_sz;
	printf "      text-anchor=\"middle\">\n";
	printf "%s", x_labels[x];
	printf "</text>\n";
    }
    printf "  </svg>\n";
    printf "</g>\n";
    printf "\n";
    if ( x_title_ht > 0.0 ) {
	printf "<text\n";
	printf "    id=\"xTitle\"\n";
	printf "    class=\"x axis label\"\n";
	printf "    x=\"%f\"\n", x_axis_x_px + x_axis_width_px / 2.0;
	printf "    y=\"%f\"\n", x_axis_y_px + x_axis_ht_px + pad + font_sz;
	printf "    font-size=\"%.1f\"\n", font_sz;
	printf "    text-anchor=\"middle\">";
	printf "%s", x_title;
	printf "</text>\n";
    }

#   Draw and label y axis
    printf "<!-- Clip area and svg element for y axis and labels -->\n";
    printf "<g\n";
    printf "    clip-path=\"url(#yAxisClip)\">\n";
    printf "  <svg\n";
    printf "    id=\"yAxis\"\n";
    printf "    x=\"%f\"\n", y_axis_x_px;
    printf "    y=\"%f\"\n", y_axis_y_px;
    printf "    width=\"%f\"\n", y_axis_width_px;
    printf "    height=\"%f\"\n", y_axis_height_px;
    printf "    viewBox=\"%f %f %f %f\">\n",
	   y_axis_x_px, y_axis_y_px, y_axis_width_px, y_axis_height_px;
    x_px = y_axis_x_px + y_axis_width_px - tick_len;
    px_per_y = plot_height_px / (y_top - y_btm);
    for (y in y_labels) {
	y_px = top + (y_top - y) * px_per_y;
	printf "  <line\n";
	printf "      class=\"y axis tick\"\n";
	printf "      x1=\"%f\"\n", x_px;
	printf "      x2=\"%f\"\n", x_px + tick_len;
	printf "      y1=\"%f\"\n", y_px;
	printf "      y2=\"%f\"\n", y_px;
	printf "      stroke=\"black\"\n"
	printf "      stroke-width=\"1\" />\n"
	printf "  <text\n";
	printf "      class=\"y axis label\"\n";
	printf "      x=\"%f\"\n", x_px;
	printf "      y=\"%f\"\n", y_px;
	printf "      font-size=\"%.1f\"\n", font_sz;
	printf "      text-anchor=\"end\"\n";
	printf "      dominant-baseline=\"mathematical\">";
	printf "%s", y_labels[y];
	printf "</text>\n";
    }
    printf "  </svg>\n";
    printf "</g>\n";
    printf "\n";
    if ( y_title_width > 0.0 ) {
	x = y_axis_x_px - pad;
	y = y_axis_y_px + y_axis_height_px / 2.0;
	printf "<g\n";
	printf "    id=\"yTitleTransform\"\n";
	printf "    transform=\"matrix(0.0, -1.0, 1.0, 0.0, %.1f, %.1f)\">\n",
	       x, y;
	printf "<text\n";
	printf "    id=\"yTitle\"\n";
	printf "    class=\"y axis label\"\n";
	printf "    x=\"0.0\"\n";
	printf "    y=\"0.0\"";
	printf "    font-size=\"%.1f\"\n", font_sz;
	printf "    text-anchor=\"middle\">";
	printf "%s", y_title;
	printf "</text>\n</g>\n";
    }
}

/^\s*end\s*$/ {
    printing = 0;
}

# If printing pass input SVG to output.
{
    if ( printing ) {
	print;
    }
}

END {
    printf "</svg>\n";
}
