/*
   -	sigmet_svg.js --
   -		This script adds interactive behavior to a plot made by
   -		sigmet_html.
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
 */

/*
   This function adds variables and functions related to the SVG plot
   to its call object. It sets up several event listeners, where the
   SVG plot variables and functions persist anonymously in closures.
 */

window.addEventListener("load", function (evt) {
	"use strict";
	/*jslint browser:true */

	/*
	   Compute a0 = Earth radius.
	   Use 4/3 rule.
	   Use International Standard Nautical Mile = 1852.0 meters
	 */

	var a0 = 4.0 / 3.0 * 1852.0 * 60.0 * 180.0 / Math.PI;

	/* Convert degrees to radians and back */
	var radPerDeg = Math.PI / 180.0;
	var degPerRad = 180.0 / Math.PI;

	/* Lengths of axis tick marks, pixels */
	var tick = (document.getElementsByClassName("xAxisTick"))[0];
	var tick_len = tick.y2.baseVal.value - tick.y1.baseVal.value;

	/* Label font size, might vary during run time */
	var font_sz;

	/* Padding between label elements */
	var pad;

	/*
	   Number of significant digits in axis labels. Convenience function
	   to_prx prints a number x with precision prx, without trailing
	   0's or decimal point.
	 */

	var x_prx = 6;
	var y_prx = 6;
	function to_prx(x, prx)
	{
	    var s = x.toPrecision(prx);
	    s = s.replace(/0+$/, "");
	    return s.replace(/\.?$/, "");
	}

	/* Objects for plot elements */
	var outSVG = document.getElementById("outermost");
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

	/* Interactive elements */
	var zoom_in = document.getElementById("zoom_in");
	var zoom_out = document.getElementById("zoom_out");
	var cursor_loc = document.getElementById("cursor_loc");
	var color_legend = document.getElementById("color_legend");

	/*
	   If keep_margins is true, plot will resize with window to preserve
	   margins. If false, plot will remain at size when loaded.

	   If keep_margins is true, PLOT MUST BE AT TOP AND LEFT OF WINDOW.
	   When plot is resized, it will leave winBtm pixels at bottom for
	   other window elements. Elements within winBtm pixels of SVG bottom
	   edge will always be visible. There may be lower elements, but they
	   will disappear after a resize event, and must be scrolled into view.

	   Set winBtm to size of interactive area.
	 */

	var keep_margins = true;
	var winBtm = 60.0;
	if ( zoom_in ) {
	    winBtm += zoom_in.clientHeight;
	}
	if ( cursor_loc ) {
	    winBtm += cursor_loc.clientHeight;
	}

	/*
	   Axis labels:
	   For each axis there will be an array of objects.
	   Each object will have as members:
	   lbl	- a text element with the text of the label
	   tick	- a line element with a tick mark
	 */

	var x_labels = [];
	var y_labels = [];

	/*
	   Coordinates of plot elements before and during drag events.
	   start_plot_drag, plot_drag, and end_plot_drag event handlers,
	   defined below need them.
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

	/* Distances axis labels can go beyond plot edges, pixels */ 
	var xOverHang = xAxis.width.baseVal.value - plot.width.baseVal.value;
	var yOverHang = yAxis.height.baseVal.value - plot.height.baseVal.value;

	/* These functions update the cursor location display. */ 
	function update_rhi_loc(evt)
	{
	    var x = svg_x_to_cart(evt.clientX);
	    var y = svg_y_to_cart(evt.clientY);
	    var cursorLoc = {};
	    GeogStep(radarLon, radarLat, az, x / a0, cursorLoc);
	    var lon = cursorLoc.lon * degPerRad;
	    var lat = cursorLoc.lat * degPerRad;
	    var ew = (lon > 0.0) ? " E " : " W ";
	    var ns = (lat > 0.0) ? " N " : " S ";
	    lon = Math.abs(lon);
	    lat = Math.abs(lat);
	    var txt = "Cursor: "
		+ lon.toFixed(3) + ew
		+ lat.toFixed(3) + ns
		+ y.toFixed(0) + " m-agl";
	    cursor_loc.textContent = txt;
	}
	function update_ppi_loc(evt)
	{
	    var x = svg_x_to_cart(evt.clientX);
	    var y = svg_y_to_cart(evt.clientY);
	    var az = Math.atan2(y, x);
	    var delta = Math.sqrt(x * x + y * y) / a0;
	    var cursorLoc = {};
	    GeogStep(radarLon, radarLat, az, delta, cursorLoc);
	    var lon = cursorLoc.lon * degPerRad;
	    var lat = cursorLoc.lat * degPerRad;
	    var ew = (lon > 0.0) ? " E " : " W ";
	    var ns = (lat > 0.0) ? " N " : " S ";
	    lon = Math.abs(lon);
	    lat = Math.abs(lat);
	    var ht = 2.0 * a0 * Math.sin(delta / 2.0)
		* Math.sin(tilt + delta / 2.0) / Math.cos(tilt + delta);
	    var txt = "Cursor: "
		+ lon.toFixed(3) + ew
		+ lat.toFixed(3) + ns
		+ ht.toFixed(0) + " m-agl";
	    cursor_loc.textContent = txt;
	}

	/*
	   If cursor location display is enabled, fetch radar location,
	   sweep type, and sweep angle from the caption. Set cursor_loc
	   update function appropriate to sweep type.
	 */

	if ( cursor_loc ) {
	    var caption = document.getElementById("caption");
	    var lonLatPtn = /([e\d.-]+) deg lon, ([e\d.-]+) deg lat/;
	    var match;
	    match = caption.textContent.match(lonLatPtn);
	    if ( match ) {
		var radarLon = match[1] * radPerDeg;
		var radarLat = match[2] * radPerDeg;
	    }
	    var rhiPtn = /RHI az = ([e\d.-]+) deg/;
	    match = caption.textContent.match(rhiPtn);
	    if ( match ) {
		var az = match[1] * radPerDeg;
		plot.addEventListener("mousemove", update_rhi_loc, false);
	    }
	    var ppiPtn = /PPI tilt = ([e\d.-]+) deg/;
	    match = caption.textContent.match(ppiPtn);
	    if ( match ) {
		var tilt = match[1] * Math.PI / 180.0;
		plot.addEventListener("mousemove", update_ppi_loc, false);
	    }
	}

	/*
	   Enable zoom buttons.

	   Function zoom_plot applies zoom factor s to the plot.
	   s < 0 => zooming in, s > 0 => zooming out.

	   Function zoom_attrs adjusts certain elements so that lines
	   and markers do not become thicker or thinner as user zooms
	   in or out.
	 */

	function zoom_plot(s)
	{
	    var dx, dy, corner;
	    var cart = get_cart();

	    /*
	       If origin is at a corner, zoom about it.
	       Otherwise, zoom about center.
	     */

	    dx = Math.abs(cart.rght - cart.left) / 128.0;
	    dy = Math.abs(cart.top - cart.btm) / 128.0;
	    corner = ( (Math.abs(cart.left) < dx || Math.abs(cart.rght) < dx)
		    && (Math.abs(cart.btm) < dy || Math.abs(cart.top) < dy) );
	    if (corner) {
		cart.left *= s;
		cart.rght *= s;
		cart.btm *= s;
		cart.top *= s;
	    } else {
		dx = (cart.rght - cart.left) * (1.0 - s) / 2.0;
		cart.left += dx;
		cart.rght -= dx;
		dy = (cart.top - cart.btm) * (1.0 - s) / 2.0;
		cart.btm += dy;
		cart.top -= dy;
	    }
	    setXform(cart);
	    for (var c = 0; c < plot.childNodes.length; c++) {
		zoom_attrs(plot.childNodes[c], s);
	    }
	    update_background();
	    update_axes();
	}
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
	if ( zoom_in ) {
	    zoom_in.addEventListener("click",
		    function (evt) { zoom_plot(3.0 / 4.0); }, false);
	}
	if ( zoom_out ) {
	    zoom_out.addEventListener("click", 
		    function (evt) { zoom_plot(4.0 / 3.0); }, false);
	}

	/*
	   resize function adjusts plot to preserve original margins
	   if window resizes. leftMgn, rghtMgn, topMgn, and btmMgn store the
	   original margins.
	 */

	var leftMgn = plot.x.baseVal.value;
	var rghtMgn = outSVG.width.baseVal.value - leftMgn
	    - plot.width.baseVal.value;
	var topMgn = plot.y.baseVal.value;
	var btmMgn = outSVG.height.baseVal.value - topMgn
	    - plot.height.baseVal.value;
	function resize(evt)
	{
	    var winWidth = this.innerWidth;
	    var winHeight = this.innerHeight;

	    var currSVGWidth = outSVG.width.baseVal.value;
	    var currSVGHeight = outSVG.height.baseVal.value;
	    var newSVGWidth = winWidth;
	    var newSVGHeight = winHeight - winBtm;

	    var currPlotWidth = plot.width.baseVal.value;
	    var currPlotHeight = plot.height.baseVal.value;
	    var newPlotWidth = newSVGWidth - leftMgn - rghtMgn;
	    var newPlotHeight = newSVGHeight - topMgn - btmMgn;
	    var delta, dx, dy;

	    var cart = get_cart();

	    /*
	       If origin is at a corner, zoom about it.
	       Otherwise, zoom about center.
	     */

	    dx = Math.abs(cart.rght - cart.left) / 128.0;
	    dy = Math.abs(cart.top - cart.btm) / 128.0;
	    if ( (Math.abs(cart.left) < dx || Math.abs(cart.rght) < dx)
		    && (Math.abs(cart.btm) < dy || Math.abs(cart.top) < dy) ) {
		delta = newSVGWidth / currSVGWidth;
		cart.left *= delta;
		cart.rght *= delta;
		delta = newSVGHeight / currSVGHeight;
		cart.btm *= delta;
		cart.top *= delta;
	    } else {
		var mPerPx;

		/* Update Cartesian limits using current plot rectangle */
		mPerPx = (cart.rght - cart.left) / currPlotWidth;
		delta = (newSVGWidth - currSVGWidth) * mPerPx;
		cart.left -= delta / 2;
		cart.rght += delta / 2;

		mPerPx = (cart.top - cart.btm) / currPlotHeight;
		delta = (newSVGHeight - currSVGHeight) * mPerPx;
		cart.top += delta / 2;
		cart.btm -= delta / 2;
	    }

	    /* Adjust plot rectangle */
	    outSVG.setAttribute("width", newSVGWidth);
	    outSVG.setAttribute("height", newSVGHeight);
	    plot.setAttribute("width", newPlotWidth);
	    plot.setAttribute("height", newPlotHeight);
	    plotArea.setAttribute("width", newPlotWidth);
	    plotArea.setAttribute("height", newPlotHeight);

	    setXform(cart);
	    update_background();
	    update_axes();

	    /* Adjust location of color legend */
	    var transform = color_legend.transform.baseVal.getItem(0);
	    transform.setTranslate(leftMgn + newPlotWidth + 24,
		    transform.matrix.f);

	}
	if ( keep_margins ) {
	    this.addEventListener("resize", resize, true);
	}

	/*
	   Enable plot dragging.

	   start_plot_drag is called at mouse down. It records the location of
	   the plot in its parent as members x0 and y0. It records the initial
	   cursor location in dragSVGX0 and dragSVGY0, which remain constant
	   throughout the drag. It also records the cursor location in members
	   prevEvtSVGX and prevEvtSVGY, which change at every mousemove during the
	   drag.

	   plot_drag is called at each mouse move while the plot is being
	   dragged. It determines how much the mouse has moved since the last
	   event, and shifts the dragable elements by that amount.

	   end_plot_drag is called at mouse up. It determines how much the
	   viewBox has changed since the start of the drag. It restores dragged
	   elements to their initial coordinates, plotSVGX, plotSVGY, but with a
	   new shifted viewBox.
	 */

	function start_plot_drag(evt)
	{
	    prevEvtSVGX = dragSVGX0 = evt.clientX;
	    prevEvtSVGY = dragSVGY0 = evt.clientY;
	    plot.addEventListener("mousemove", plot_drag, false);
	    plot.addEventListener("mouseup", end_plot_drag, false);
	}
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
	plot.addEventListener("mousedown", start_plot_drag, false);

	/*
	   Get the limits of the plot area in Cartesian coordinates.
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
	   which must be a return value from get_cart. This only modifies
	   the <g ...> element that provides the transform. It does not
	   redraw anything.
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

	/* Label the plot axes. Needed after drag, zoom, resize. */
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
	    lbl = document.getElementsByClassName("xAxisLabel")[0];
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
	    var svgNs = "http://www.w3.org/2000/svg";

	    y = xAxis.y.baseVal.value + tick_len + pad + font_sz;
	    y1 = xAxis.y.baseVal.value;
	    y2 = y1 + tick_len;
	    plotLeft = plot.x.baseVal.value;
	    plotRght = plotLeft + plot.width.baseVal.value;
	    for (l = 0, textLength = 0.0; l < coords.length; l++) {
		if ( !x_labels[l] ) {
		    lbl = document.createElementNS(svgNs, "text");
		    lbl.setAttribute("class", "xAxisLabel");
		    lbl.setAttribute("text-anchor", "middle");
		    xAxis.appendChild(lbl);
		    tick = document.createElementNS(svgNs, "line");
		    tick.setAttribute("class", "xAxisTick");
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
	    var svgNs = "http://www.w3.org/2000/svg";

	    yAxisRght = yAxis.x.baseVal.value + yAxis.width.baseVal.value;
	    x = yAxisRght - 1.5 * tick_len;
	    x1 = yAxisRght - tick_len;
	    x2 = yAxisRght;
	    plotTop = plot.y.baseVal.value;
	    plotBtm = plotTop + plot.height.baseVal.value;
	    for (l = 0, textHeight = 0.0; l < coords.length; l++) {
		if ( !y_labels[l] ) {
		    lbl = document.createElementNS(svgNs, "text");
		    lbl.setAttribute("class", "yAxisLabel");
		    lbl.setAttribute("text-anchor", "end");
		    lbl.setAttribute("dominant-baseline", "mathematical");
		    yAxis.appendChild(lbl);
		    tick = document.createElementNS(svgNs, "line");
		    tick.setAttribute("class", "yAxisTick");
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

	/*
	   Compute destination point at given separation delta and
	   direction dir from point at longitude lon1, latitude lat1.
	   Destination longitude and latitude will be placed in loc
	   as lon and lat members. All angles are in radians. delta in
	   great circle radians.

	   Ref.
	   Sinnott, R. W., "Virtues of the Haversine",
	   Sky and Telescope, vol. 68, no. 2, 1984, p. 159
	   cited in: http://www.census.gov/cgi-bin/geo/gisfaq?Q5.1
	 */

	function GeogStep(lon1, lat1, dir, delta, loc)
	{
	    var sin_s, sin_d, cos_d, dlon, a, x, y, lon;

	    sin_s = Math.sin(delta);
	    sin_d = Math.sin(dir);
	    cos_d = Math.cos(dir);
	    a = 0.5 * (Math.sin(lat1 + delta) * (1.0 + cos_d)
		    + Math.sin(lat1 - delta) * (1.0 - cos_d));
	    loc.lat = (a > 1.0) ? Math.PI / 2
		: (a < -1.0) ? -Math.PI / 2 : Math.asin(a);
	    y = sin_s * sin_d;
	    x = 0.5 * (Math.cos(lat1 + delta) * (1 + cos_d)
		    + Math.cos(lat1 - delta) * (1 - cos_d));
	    dlon = Math.atan2(y, x);
	    lon = lon1 + dlon;
	    if ( loc.lon > Math.PI ) {
		loc.lon = lon - 2.0 * Math.PI;
	    } else if ( loc.lon < Math.PI ) {
		loc.lon = lon + 2.0 * Math.PI;
	    } else {
		loc.lon = lon;
	    }
	}

	/*
	   Redraw with javascript. This prevents sudden changes
	   in the image if the static document produced by pisa.awk
	   noticeably differs from the Javascript rendition.
	 */

	while ( xAxis.lastChild ) {
	    xAxis.removeChild(xAxis.lastChild);
	}
	while ( yAxis.lastChild ) {
	    yAxis.removeChild(yAxis.lastChild);
	}
	if ( keep_margins ) {
	    resize.call(this, {});
	}

}, false);			/* Done defining load callback */

