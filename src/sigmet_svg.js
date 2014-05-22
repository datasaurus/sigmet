/*
   -	sigmet_svg.js --
   -		This script adds interactive behavior to a plot made by
   -		sigmet_svg.
   -
   .	Copyright (c) 2013, Gordon D. Carrie. All rights reserved.
   .
   .	Redistribution and use in source and binary forms, with or without
   .	modification, are permitted provided that the following conditions
   .	are met:
   .
   .	* Redistributions of source code must retain the above copyright
   .	notice, this list of conditions and the following disclaimer.
   .	* Redistributions in binary form must reproduce the above copyright
   .	notice, this list of conditions and the following disclaimer in the
   .	documentation and/or other materials provided with the distribution.
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
   .	$Revision: 1.1 $ $Date: 2014/05/21 22:05:11 $
 */

/*
   This function adds variables and functions related to the SVG plot
   to its call object. It sets up several event listeners, where the
   plot variables and functions persist anonymously in closures.
 */

window.addEventListener("load", function (evt)
	{
	"use strict";
	/*jslint browser:true */

	/*
	 *************************************************************
	 ****************** Adjust these variables. ******************
	 *************************************************************
	 */

	/* Lengths of axis tick marks, pixels */
	var tick = (document.getElementsByClassName("x axis tick"))[0];
	var tick_len = tick.y2.baseVal.value - tick.y1.baseVal.value;

	/* Label font size, might vary during run time */
	var font_sz;

	/* Padding between label elements */
	var pad;

	/* Number of significant digits in axis labels */
	var x_prx = 6;
	var y_prx = 6;

	/*
	   If keep_margins is true, plot will resize with window to preserve
	   margins. If false, plot will remain at prescribed size.
	 */

	var keep_margins = true;

	/*
	 *************************************************************
	 ******* Everything below here should not change much. *******
	 *************************************************************
	 */

	/* These objects store information about the plot elements */
	var root = document.rootElement;
	var plot = document.getElementById("plot");
	var plotArea = document.getElementById("PlotRect");
	var plotBackground = document.getElementById("plotBackground");
	var cartG = document.getElementById("cartG");
	var xAxis = document.getElementById("xAxis");
	var xAxisClip = document.getElementById("xAxisClipRect");
	var xTitle = document.getElementById("xTitle");
	var yAxis = document.getElementById("yAxis");
	var yAxisClip = document.getElementById("yAxisClipRect");
	var yTitle = document.getElementById("yTitle");
	var yTitleXForm = document.getElementById("yTitleTransform");
	var zoom_in = document.getElementById("zoom_in");
	var zoom_out = document.getElementById("zoom_out");
	var print = document.getElementById("print");
	var color_legend = document.getElementById("color_legend");

	/*
	   Axis labels are allowed to extend beyond plot edge by
	   xOverHang and yOverHang pixels.
	 */

	var xOverHang = xAxis.width.baseVal.value - plot.width.baseVal.value;
	var yOverHang = yAxis.height.baseVal.value - plot.height.baseVal.value;

	/*
	   Axis labels:
	   For each axis there will be an array of objects.
	   Each object will have as members:
	   lbl	- a text element with the text of the label
	   tick	- a line element with a tick mark
	 */

	var x_labels = [];
	var y_labels = [];

	var svgNs = "http://www.w3.org/2000/svg";
	var xlinkNs="http://www.w3.org/1999/xlink";

	/*
	   start_plot_drag, plot_drag, and end_plot_drag event handlers,
	   defined below, enable user to drag Cartesian plot and axes with
	   the mouse. These variables store information in this scope about
	   the initial positions of the plot elements and about the current
	   drag.
	 */

	var plotSVGX, plotSVGY;		/* SVG coordinates of plot element */
	var xAxisSVGX, xAxisSVGY;	/* SVG coordinates of x axis element */
	var yAxisSVGX, yAxisSVGY;	/* SVG coordinates of y axis element */
	var dragSVGX0, dragSVGY0;	/* SVG coordinates of mouse at start
					   of drag */
	var prevEvtSVGX, prevEvtSVGY;	/* SVG coordinates of mouse at previous
					   mouse event during drag */

	plotSVGX = plot.x.baseVal.value;
	plotSVGY = plot.y.baseVal.value;
	xAxisSVGX = xAxis.x.baseVal.value;
	xAxisSVGY = xAxis.y.baseVal.value;
	yAxisSVGX = yAxis.x.baseVal.value;
	yAxisSVGY = yAxis.y.baseVal.value;

	/*
	   Record root element dimensions and margins around plot.
	   These will be needed if root element resizes.
	   rootWidth and rootHght might change. Margins will be preserved.
	 */

	var rootWidth = root.width.baseVal.value;
	var rootHght = root.height.baseVal.value;
	var leftMgn = plot.x.baseVal.value;
	var rghtMgn = rootWidth - leftMgn - plot.width.baseVal.value;
	var topMgn = plot.y.baseVal.value;
	var btmMgn = rootHght - topMgn - plot.height.baseVal.value;

	/* Local function definitions */

	/*
	   This function gets limits of the plot area in Cartesian coordinates.
	   Return value is an object with the following members:
	   .	left = x coordinate at left edge of plot area.
	   .	rght = x coordinate at right edge of plot area.
	   .	top = y coordinate at top edge of plot area.
	   .	btm = y coordinate at bottom edge of plot area.
	 */

	function get_cart()
	{
	    var xForm = cartG.transform.baseVal.getItem(0).matrix;
	    var a = xForm.a;
	    var d = xForm.d;
	    var e = xForm.e;
	    var f = xForm.f;
	    var cart = {};
	    cart.left = -e / a;
	    cart.rght = (plot.width.baseVal.value - e) / a;
	    cart.top = -f / d;
	    cart.btm = (plot.height.baseVal.value - f) / d;
	    return cart;
	}

	/*
	   Set transform for plot area in Cartesian coordinates from cart,
	   which is an object with the same members as the return value
	   from get_cart. This only modifies the <g ...> element that
	   provides the transform. It does not redraw the axes.
	 */

	function setXform(cart)
	{
	    var xForm = cartG.transform.baseVal.getItem(0).matrix;
	    var plotWidth = plot.width.baseVal.value;
	    var plotHeight = plot.height.baseVal.value;
	    xForm.a = plotWidth / (cart.rght - cart.left);
	    xForm.b = 0.0;
	    xForm.c = 0.0;
	    xForm.d = plotHeight / (cart.btm - cart.top);
	    xForm.e = plotWidth * cart.left / (cart.left - cart.rght);
	    xForm.f = plotHeight * cart.top / (cart.top - cart.btm);
	}

	/* Convert Cartesian x to SVG x */
	function cart_x_to_svg(cartX)
	{
	    var xLeftSVG = plot.x.baseVal.value;
	    var plotWidth = plot.width.baseVal.value;
	    var cart = get_cart();
	    var pxPerM = plotWidth / (cart.rght - cart.left);
	    return xLeftSVG + (cartX - cart.left) * pxPerM;
	}

	/* Convert SVG x Cartesian x */
	function svg_x_to_cart(svgX)
	{
	    var xLeftSVG = plot.x.baseVal.value;
	    var plotWidth = plot.width.baseVal.value;
	    var cart = get_cart();
	    var mPerPx = (cart.rght - cart.left) / plotWidth;
	    return cart.left + (svgX - xLeftSVG) * mPerPx;
	}

	/* Convert Cartesian y to SVG y */
	function cart_y_to_svg(cartY)
	{
	    var yTopSVG = plot.y.baseVal.value;
	    var plotHeight = plot.height.baseVal.value;
	    var cart = get_cart();
	    var pxPerM = plotHeight / (cart.btm - cart.top);
	    return yTopSVG + (cartY - cart.top) * pxPerM;
	}

	/* Convert SVG y Cartesian y */
	function svg_y_to_cart(svgY)
	{
	    var yTopSVG = plot.y.baseVal.value;
	    var plotHeight = plot.height.baseVal.value;
	    var cart = get_cart();
	    var mPerPx = (cart.btm - cart.top) / plotHeight;
	    return cart.top + (svgY - yTopSVG) * mPerPx;
	}

	/* Print x with precision prx, removing trailing "." and 0's */
	function to_prx(x, prx)
	{
	    return Number(x.toPrecision(prx));
	}

	/*
	   Create a set of axis labels for an axis ranging from x_min to x_max
	   with increment dx. Returns an array of coordinate values.
	 */

	function coord_list(x_min, x_max, dx)
	{
	    var x0;			/* Coordinate of first label = nearest
					   multiple of dx less than x_min */
	    var x;			/* x coordinate */
	    var coords;			/* Return value */
	    var n, m;			/* Loop indeces */

	    coords = [];
	    x0 = Math.floor(x_min / dx) * dx;
	    for (n = m = 0; n <= Math.ceil((x_max - x_min) / dx); n++) {
		x = x0 + n * dx;
		if ( x >= x_min - dx / 4 && x <= x_max + dx / 4 ) {
		    coords[m] = x;
		    m++;
		}
	    }
	    return coords;
	}

	/*
	   Show a label, which must be a label object with text and tick
	   members. The element will still exist in the document.
	 */ 

	function show_label(label)
	{
	    label.lbl.setAttribute("visibility", "visible");
	    label.tick.setAttribute("visibility", "visible");
	    label.lbl.textContent = "";
	}

	/*
	   Hide label, which must be a label object with text and tick members.
	   The element will still exist in the document.
	 */ 

	function hide_label(label)
	{
	    label.lbl.setAttribute("visibility", "hidden");
	    label.tick.setAttribute("visibility", "hidden");
	    label.lbl.textContent = "";
	}

	/*
	   Apply coordinate list coords to x axis. Return total display length
	   of the labels.
	 */

	function apply_x_coords(coords)
	{
	    var x;			/* Label location */
	    var y;			/* Label text location */
	    var y1, y2;			/* Limits of tick */
	    var plotLeft, plotRght;	/* SVG x coordinates of left and right
					   side of plot */
	    var l;			/* Label index */
	    var textLength;		/* SVG width required to display text of
					   all labels */
	    var lbl, tick;		/* Label and tick elements */
	    var bbox;			/* Bounding box for a text label */

	    y = xAxis.y.baseVal.value + tick_len + pad + font_sz;
	    y1 = xAxis.y.baseVal.value;
	    y2 = y1 + tick_len;
	    plotLeft = plot.x.baseVal.value;
	    plotRght = plotLeft + plot.width.baseVal.value;
	    for (l = 0, textLength = 0.0; l < coords.length; l++) {
		if ( !x_labels[l] ) {
		    lbl = document.createElementNS(svgNs, "text");
		    lbl.setAttribute("class", "x axis label");
		    lbl.setAttribute("text-anchor", "middle");
		    xAxis.appendChild(lbl);
		    tick = document.createElementNS(svgNs, "line");
		    tick.setAttribute("class", "x axis tick");
		    tick.setAttribute("stroke", "black");
		    tick.setAttribute("stroke-width", "1");
		    xAxis.appendChild(tick);
		    x_labels[l] = {};
		    x_labels[l].lbl = lbl;
		    x_labels[l].tick = tick;
		}
		x = cart_x_to_svg(coords[l]);
		if ( plotLeft <= x && x <= plotRght ) {
		    show_label(x_labels[l]);
		    x_labels[l].lbl.setAttribute("x", x);
		    x_labels[l].lbl.setAttribute("y", y);
		    x_labels[l].lbl.textContent = to_prx(coords[l], x_prx);
		    x_labels[l].tick.setAttribute("x1", x);
		    x_labels[l].tick.setAttribute("x2", x);
		    x_labels[l].tick.setAttribute("y1", y1);
		    x_labels[l].tick.setAttribute("y2", y2);
		    textLength += x_labels[l].lbl.getComputedTextLength();
		} else {
		    hide_label(x_labels[l]);
		}
	    }
	    for ( ; l < x_labels.length; l++) {
		hide_label(x_labels[l]);
	    }
	    return textLength;
	}

	/*
	   Apply coordinate list coords to y axis. Return total display height
	   of the labels.
	 */

	function apply_y_coords(coords)
	{
	    var yAxisRght;		/* SVG x coordinates of RIGHT side of
					   y axis element */
	    var x;			/* Label text location */
	    var x1, x2;			/* Limits of tick */
	    var y;			/* SVG y coordinate of a label */
	    var plotTop, plotBtm;	/* SVG y coordinates of top and bottom
					   of plot */
	    var l;			/* Label, coordinate index */
	    var bbox;			/* Bounding box for an element */
	    var textHeight;		/* Total display height */
	    var lbl, tick;

	    yAxisRght = yAxis.x.baseVal.value + yAxis.width.baseVal.value;
	    x = yAxisRght - 1.5 * tick_len;
	    x1 = yAxisRght - tick_len;
	    x2 = yAxisRght;
	    plotTop = plot.y.baseVal.value;
	    plotBtm = plotTop + plot.height.baseVal.value;
	    for (l = 0, textHeight = 0.0; l < coords.length; l++) {
		if ( !y_labels[l] ) {
		    lbl = document.createElementNS(svgNs, "text");
		    lbl.setAttribute("class", "y axis label");
		    lbl.setAttribute("text-anchor", "end");
		    lbl.setAttribute("dominant-baseline", "mathematical");
		    yAxis.appendChild(lbl);
		    tick = document.createElementNS(svgNs, "line");
		    tick.setAttribute("class", "y axis tick");
		    tick.setAttribute("stroke", "black");
		    tick.setAttribute("stroke-width", "1");
		    yAxis.appendChild(tick);
		    y_labels[l] = {};
		    y_labels[l].lbl = lbl;
		    y_labels[l].tick = tick;
		}
		y = cart_y_to_svg(coords[l]);
		if ( plotSVGY <= y && y <= plotBtm ) {
		    show_label(y_labels[l]);
		    y_labels[l].lbl.setAttribute("x", x);
		    y_labels[l].lbl.setAttribute("y", y);
		    y_labels[l].lbl.textContent = to_prx(coords[l], y_prx);
		    y_labels[l].tick.setAttribute("x1", x1);
		    y_labels[l].tick.setAttribute("x2", x2);
		    y_labels[l].tick.setAttribute("y1", y);
		    y_labels[l].tick.setAttribute("y2", y);
		    bbox = y_labels[l].lbl.getBBox();
		    textHeight += bbox.height;
		} else {
		    hide_label(y_labels[l]);
		}
	    }
	    for ( ; l < y_labels.length; l++) {
		hide_label(y_labels[l]);
	    }
	    return textHeight;
	}

	/*
	   Produce a set of labels for coordinates ranging from lo to hi.
	   apply_coords must be a function that creates the labels in the
	   document and returns the amount of space they use. max_sz must
	   specify the maximum amount of space they are allowed to use.
	 */

	function mk_labels(lo, hi, apply_coords, max_sz)
	{
	    /*
	       Initialize dx with smallest power of 10 larger in magnitude
	       than hi - lo. Decrease magnitude of dx. Place
	       label set for the smaller dx into l1. If printing the labels
	       in l1 would overflow the axis with characters, restore and
	       use l0. Otherwise, replace l0 with l1 and retry with a
	       smaller dx.
	     */

	    var dx, have_labels, l0, l1, t;

	    if ( lo === hi ) {
		apply_coords([l0]);
		return;
	    }
	    if ( lo > hi ) {
		t = hi;
		hi = lo;
		lo = t;
	    }
	    dx = Math.pow(10.0, Math.ceil(Math.log(hi - lo) / Math.LN10));
	    for (have_labels = false; !have_labels; ) {
		l1 = coord_list(lo, hi, dx);
		if ( apply_coords(l1) > max_sz ) {
		    apply_coords(l0);
		    have_labels = true;
		} else {
		    l0 = l1;
		}
		dx *= 0.5;			/* If dx was 10, now it is 5 */
		l1 = coord_list(lo, hi, dx);
		if ( apply_coords(l1) > max_sz ) {
		    apply_coords(l0);
		    have_labels = true;
		} else {
		    l0 = l1;
		}
		dx *= 0.4;			/* If dx was 5, now it is 2 */
		l1 = coord_list(lo, hi, dx);
		if ( apply_coords(l1) > max_sz ) {
		    apply_coords(l0);
		    have_labels = true;
		} else {
		    l0 = l1;
		}
		dx *= 0.5;			/* If dx was 2, now it is 1 */
	    }
	}

	/* Label the plot axes. */
	function update_axes ()
	{
	    var viewBox;		/* Axis viewBox */
	    var plotWidth;		/* Plot width, SVG coordinates */
	    var plotHeight;		/* Plot height, SVG coordinates */
	    var axisWidth;		/* Axis width, SVG coordinates */
	    var axisHeight;		/* Axis height, SVG coordinates */
	    var cart;			/* Limits of plot in Cartesian
					   coordinates */
	    var xForm;			/* Transform to rotate y axis title */
	    var lbl;			/* Label text element */
	    var bbox;			/* Bounding box for lbl */

	    plotWidth = plot.width.baseVal.value;
	    plotHeight = plot.height.baseVal.value;
	    cart = get_cart();

	    /* Update font size and padding */
	    lbl = document.getElementsByClassName("x axis label")[0];
	    bbox = lbl.getBBox();
	    font_sz = bbox.height;
	    pad = xAxis.height.baseVal.value - tick_len - font_sz;

	    /* Restore x axis position and update viewBox */
	    xAxis.setAttribute("x", xAxisSVGX);
	    xAxisSVGY = plotSVGY + plotHeight;
	    xAxis.setAttribute("y", xAxisSVGY);
	    axisWidth = plotWidth + xOverHang;
	    axisHeight = xAxis.viewBox.baseVal.height;
	    xAxis.setAttribute("width", axisWidth);
	    xAxisClip.setAttribute("y", xAxisSVGY);
	    xAxisClip.setAttribute("width", axisWidth);
	    viewBox = xAxisSVGX;
	    viewBox += " " + xAxisSVGY;
	    viewBox += " " + axisWidth;
	    viewBox += " " + axisHeight;
	    xAxis.setAttribute("viewBox", viewBox);
	    xTitle.setAttribute("x", xAxisSVGX + axisWidth / 2.0);
	    xTitle.setAttribute("y", xAxisSVGY + axisHeight + pad + font_sz);

	    /* Create new labels for x axis */
	    mk_labels(cart.left, cart.rght, apply_x_coords, plotWidth / 4);

	    /* Restore y axis position and update viewBox */
	    yAxis.setAttribute("y", yAxisSVGY);
	    axisWidth = yAxis.viewBox.baseVal.width;
	    axisHeight = plotHeight + yOverHang;
	    yAxis.setAttribute("height", axisHeight);
	    yAxisClip.setAttribute("height", axisHeight);
	    viewBox = yAxis.viewBox.baseVal.x;
	    viewBox += " " + yAxisSVGY;
	    viewBox += " " + axisWidth;
	    viewBox += " " + axisHeight;
	    yAxis.setAttribute("viewBox", viewBox);
	    xForm = yTitleXForm.transform.baseVal.getItem(0).matrix;
	    xForm.f = yAxisSVGY + axisHeight / 2.0;

	    /* Create new labels for y axis */
	    mk_labels(cart.btm, cart.top, apply_y_coords, plotHeight / 4);
	}

	/* Draw the background */
	function update_background()
	{
	    var cart = get_cart();
	    if ( cart.left < cart.rght ) {
		plotBackground.setAttribute("x", cart.left);
		plotBackground.setAttribute("width", cart.rght - cart.left);
	    } else {
		plotBackground.setAttribute("x", cart.rght);
		plotBackground.setAttribute("width", cart.left - cart.rght);
	    }
	    if ( cart.btm < cart.top ) {
		plotBackground.setAttribute("y", cart.btm);
		plotBackground.setAttribute("height", cart.top - cart.btm);
	    } else {
		plotBackground.setAttribute("y", cart.top);
		plotBackground.setAttribute("height", cart.btm - cart.top);
	    }
	}

	/*
	   plot_drag is called at each mouse move while the plot is being
	   dragged. It determines how much the mouse has moved since the last
	   event, and shifts the dragable elements by that amount.
	 */

	function plot_drag(evt)
	{
	    var dx, dy;			/* How much to move the elements */

	    dx = evt.clientX - prevEvtSVGX;
	    dy = evt.clientY - prevEvtSVGY;
	    plot.setAttribute("x", plot.x.baseVal.value + dx);
	    plot.setAttribute("y", plot.y.baseVal.value + dy);
	    xAxis.setAttribute("x", xAxis.x.baseVal.value + dx);
	    yAxis.setAttribute("y", yAxis.y.baseVal.value + dy);
	    prevEvtSVGX = evt.clientX;
	    prevEvtSVGY = evt.clientY;
	}

	/*
	   end_plot_drag is called at mouse up. It determines how much the
	   viewBox has changed since the start of the drag. It restores dragged
	   elements to their initial coordinates, plotSVGX, plotSVGY, but with a
	   new shifted viewBox.
	 */

	function end_plot_drag(evt)
	{
	    /*
	       Compute total distance dragged, in CARTESIAN coordinates, and
	       move plot viewBox by this amount
	     */

	    var plotWidth, plotHeight;	/* SVG dimensions of plot area */
	    var cart;			/* Cartesian dimensions of plot area */
	    var mPerPx;			/* Convert Cartesian distance to SVG */
	    var dx, dy;			/* Drag distance in Cartesian
					   coordinates */

	    cart = get_cart();
	    plotWidth = plot.width.baseVal.value;
	    mPerPx = (cart.rght - cart.left) / plotWidth;
	    dx = (dragSVGX0 - evt.clientX) * mPerPx;
	    cart.left += dx;
	    cart.rght += dx;
	    plotHeight = plot.height.baseVal.value;
	    mPerPx = (cart.btm - cart.top) / plotHeight;
	    dy = (dragSVGY0 - evt.clientY) * mPerPx;
	    cart.btm += dy;
	    cart.top += dy;
	    setXform(cart);

	    /*
	       Restore plot and background to their position at start of drag.
	       Elements in the plot will remain in the positions they were
	       dragged to because of the adjments to the viewBox.
	     */

	    plot.setAttribute("x", plotSVGX);
	    plot.setAttribute("y", plotSVGY);

	    update_background();
	    update_axes();

	    plot.removeEventListener("mousemove", plot_drag, false);
	    plot.removeEventListener("mouseup", end_plot_drag, false);
	}

	/*
	   start_plot_drag is called at mouse down. It records the location of
	   the plot in its parent as members x0 and y0. It records the initial
	   cursor location in dragSVGX0 and dragSVGY0, which remain constant
	   throughout the drag. It also records the cursor location in members
	   prevEvtSVGX and prevEvtSVGY, which change at every mousemove during the
	   drag.
	 */

	function start_plot_drag(evt)
	{
	    prevEvtSVGX = dragSVGX0 = evt.clientX;
	    prevEvtSVGY = dragSVGY0 = evt.clientY;
	    plot.addEventListener("mousemove", plot_drag, false);
	    plot.addEventListener("mouseup", end_plot_drag, false);
	}

	plot.addEventListener("mousedown", start_plot_drag, false);

	/* Make interactive elements visible */
	var e, elems = document.getElementsByClassName("interactive");
	for (e = 0; e < elems.length; e++) {
	    elems[e].setAttribute("visibility", "visible");
	}

	/*
	   Create a text element that displays the Cartesian coordinates
	   of the cursor location.
	 */

	var cursor_loc = document.createElementNS(svgNs, "text");
	cursor_loc.setAttribute("x", "120");
	cursor_loc.setAttribute("y", "4");
	cursor_loc.setAttribute("dominant-baseline", "hanging");
	cursor_loc.textContent = "x y";
	root.appendChild(cursor_loc);

	function update_cursor_loc(evt)
	{
	    var x = svg_x_to_cart(evt.clientX);
	    var y = svg_y_to_cart(evt.clientY);
	    var txt = "Cursor: " + to_prx(x, x_prx) + " " + to_prx(y, x_prx);
	    cursor_loc.textContent = txt;
	}
	plot.addEventListener("mousemove", update_cursor_loc, false);

	/*
	   This function applies zoom factor s to certain presentation
	   attributes of element elem and its children. It is actually used to
	   undo a zoom so that elements do not become thicker or thinner as the
	   plot view zooms in and out.
	 */

	function zoom_attrs(elem, s)
	{
	    if ( elem.nodeType == Node.ELEMENT_NODE ) {
		var attrs = ["stroke-width", "stroke-dashoffset",
		    "markerWidth", "markerHeight"];
		for (var n = 0; n < attrs.length; n++) {
		    var a = Number(elem.getAttribute(attrs[n]));
		    if ( a && a > 0.0 ) {
			elem.setAttribute(attrs[n], a * s);
		    }
		}
		a = Number(elem.getAttribute("stroke-dasharray"));
		if ( a ) {
		    var dash, dashes = "";
		    for (dash in a.split(/\s+|,/)) {
			dashes = dashes + " " + Number(dash) * s;
		    }
		    elem.setAttribute("stroke-dasharray", dashes);
		}
	    }
	    var children = elem.childNodes;
	    for (var c = 0; c < children.length; c++) {
		zoom_attrs(children[c], s);
	    }
	}

	/*
	   This convenience function applies zoom factor s to the plot.
	   s < 0 => zooming in, s > 0 => zooming out.
	 */

	function zoom_plot(s)
	{
	    var cart = get_cart();
	    var dx = (cart.rght - cart.left) * (1.0 - s) / 2.0;
	    cart.left += dx;
	    cart.rght -= dx;
	    var dy = (cart.top - cart.btm) * (1.0 - s) / 2.0;
	    cart.btm += dy;
	    cart.top -= dy;
	    setXform(cart);
	    for (var c = 0; c < plot.childNodes.length; c++) {
		zoom_attrs(plot.childNodes[c], s);
	    }
	    update_background();
	    update_axes();
	}

	/* Zoom controls */
	zoom_in.addEventListener("click",
		function (evt) { zoom_plot(3.0 / 4.0); }, false);
	zoom_out.addEventListener("click", 
		function (evt) { zoom_plot(4.0 / 3.0); }, false);

	/*
	   Grow plot if window resizes.
	 */

	function resize(evt)
	{
	    var innerWidth = this.innerWidth;
	    var innerHeight = this.innerHeight;

	    var currRootWidth = root.width.baseVal.value;
	    var currRootHeight = root.height.baseVal.value;
	    var newRootWidth = innerWidth;
	    var newRootHeight = innerHeight

		var currPlotWidth = plot.width.baseVal.value;
	    var currPlotHeight = plot.height.baseVal.value;
	    var newPlotWidth = newRootWidth - leftMgn - rghtMgn;
	    var newPlotHeight = newRootHeight - topMgn - btmMgn;

	    var cart = get_cart();
	    var mPerPx;
	    var delta;			/* Increase in plot width or height,
					   Cartesian coordinates */

	    /* Update Cartesian limits using current plot rectangle */
	    mPerPx = (cart.rght - cart.left) / currPlotWidth;
	    delta = (newRootWidth - currRootWidth) * mPerPx;
	    cart.left -= delta / 2;
	    cart.rght += delta / 2;

	    mPerPx = (cart.top - cart.btm) / currPlotHeight;
	    delta = (newRootHeight - currRootHeight) * mPerPx;
	    cart.top += delta / 2;
	    cart.btm -= delta / 2;

	    /* Adjust plot rectangle */
	    root.setAttribute("width", newRootWidth);
	    root.setAttribute("height", newRootHeight);
	    plot.setAttribute("width", newPlotWidth);
	    plot.setAttribute("height", newPlotHeight);
	    plotArea.setAttribute("width", newPlotWidth);
	    plotArea.setAttribute("height", newPlotHeight);

	    setXform(cart);
	    update_background();
	    update_axes();

	    /* Adjust location of color legend */
	    color_legend.setAttribute("x", leftMgn + newPlotWidth + 24);

	}
	if ( keep_margins ) {
	    this.addEventListener("resize", resize, true);
	}

	/* Print button */
	print.addEventListener("click", function (evt) {
		root.removeChild(zoom_in);
		root.removeChild(zoom_out);
		root.removeChild(print);
		root.removeChild(cursor_loc);
		window.print();
		window.setTimeout(function () {
		    root.appendChild(zoom_in);
		    root.appendChild(zoom_out);
		    root.appendChild(print);
		    root.appendChild(cursor_loc);
		    }, 500.0);
		}, false);

	/*
	   Redraw with javascript. This prevents sudden changes
	   in the image if the static document produced by xyplot.awk
	   noticeably differs from the Javascript rendition.
	 */

	while ( xAxis.lastChild ) {
	    xAxis.removeChild(xAxis.lastChild);
	}
	while ( yAxis.lastChild ) {
	    yAxis.removeChild(yAxis.lastChild);
	}
	if ( keep_margins ) {
	    var cart = get_cart();
	    root.setAttribute("width", this.innerWidth);
	    root.setAttribute("height", this.innerHeight);
	    plot.setAttribute("width", this.innerWidth - leftMgn - rghtMgn);
	    plot.setAttribute("height", this.innerHeight - topMgn - btmMgn);
	    plotArea.setAttribute("width", this.innerWidth - leftMgn - rghtMgn);
	    plotArea.setAttribute("height", this.innerHeight - topMgn - btmMgn);
	    setXform(cart);
	    update_background();
	    var legendX = leftMgn + plot.width.baseVal.value + 24;
	    color_legend.setAttribute("x", legendX);
	}
	update_axes();

	}, false);			/* Done defining load callback */

