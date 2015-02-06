/*
   -	sigmet_buttons.js --
   -		Enable navigation buttons in SVG image created by
   -		sigmet_svg and managed by sigmet_svg.js.
   -
   .	Copyright (c) 2015, Gordon D. Carrie. All rights reserved.
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

window.addEventListener("load", function (evt) {
	"use strict";
	/*jslint browser:true */

	var site = "http://smartr.metr.ou.edu/";
	var root = site + "smartr2/images/svg";

	/* URL of current image. */
	var curr_url = window.location.href;

	/*
	 * Get current data type, data_type, from URL, which is assumed to
	 * give the file name in a form like
	 * CPOLRVP_20130815235056_DB_DBZ_54.0.svg
	 */

	var data_type;
	if ( !(data_type = (curr_url.match(/_\d{14}_(DB_[0-9A-Z]+)_/))[1]) ) {
	    alert("Could not determine data type for " + curr_url);
	    return;
	}

	/* Previous and next volume buttons */
	var prev_btn = document.getElementById("prev_vol");
	var next_btn = document.getElementById("next_vol");

	/*
	 * These functions maintain img_ls - a list of image URL's currently
	 * available at the site for the current data type. get_img_ls requests
	 * a list. Callback update_img_ls updates img_ls when with a new list
	 * when it arrives.
	 */

	var img_ls;
	function get_img_ls(evt)
	{
	    var list = site + "/cgi-bin/smartr2/img_idx?data_type=" + data_type;
	    var request = new XMLHttpRequest();
	    request.open("GET", list, true);
	    request.onreadystatechange = update_img_ls;
	    request.send(null);
	}
	function update_img_ls(evt)
	{
	    var xml, n;
	    var request = this;

	    if ( request.readyState == 4 ) {
		if ( !request.responseXML ) {
		    return;
		}

		/* Update img_ls */
		xml = request.responseXML.firstChild;
		img_ls = new Array(0);
		for (n = 0; n < xml.childNodes.length; n++) {
		    if ( xml.childNodes[n].nodeName == "img" ) {
			img_ls.push(root + "/" + xml.childNodes[n].textContent);
		    }
		}

		/*
		 * If curr_url is at start of list, dim prev button.
		 * If curr_url is at end of list, dim next button.
		 * Otherwise, restore it.
		 */

		if ( curr_url == img_ls[0] ) {
		    color_button(prev_btn, "#888", "url(#dim_arrow)")
		} else if ( curr_url == img_ls[img_ls.length - 1] ) {
		    color_button(next_btn, "#888", "url(#dim_arrow)")
		} else {
		    color_button(prev_btn, "black", "url(#arrow)")
		    color_button(next_btn, "black", "url(#arrow)")
		}
	    }
	}
	function color_button(button, color, marker)
	{
	    var n, elem;

	    for (n = 0; n < button.childNodes.length; n++) {
		elem = button.childNodes[n];
		if ( elem.tagName == "rect"
			|| elem.tagName == "text"
			|| elem.tagName == "line" ) {
		    elem.setAttribute("stroke", color);
		}
		if ( elem.tagName == "line" ) {
		    elem.setAttribute("marker-end", marker);
		}
	    }
	}
	get_img_ls();

	/*
	 * Show or hide the SVG element that indicates to user that
	 * the page is updating.
	 */

	function show_updating()
	{
	    var updating = document.getElementById("updating");
	    updating.setAttribute("visibility", "visible");
	    updating.setAttribute("display", "inline");
	}
	function hide_updating()
	{
	    var updating = document.getElementById("updating");
	    updating.setAttribute("visibility", "hidden");
	    updating.setAttribute("display", "none");
	}

	/*
	 * Enable the buttons that select the previous and next volumes.
	 *
	 * prev_img initiates a request for the previous volume at the
	 * site, as indicated in the current copy of img_ls.
	 *
	 * next_img initiates a request for the next volume at the
	 * site, as indicated in the current copy of img_ls.
	 *
	 * dpy_img installs the new image in the document when it arrives.
	 */

	function prev_img(evt)
	{
	    var curr_img_idx = img_ls.indexOf(curr_url);
	    if ( curr_img_idx > 0 ) {
		var request = new XMLHttpRequest();
		show_updating();
		request.open("GET", img_ls[curr_img_idx - 1], true);
		request.onreadystatechange = dpy_img;
		request.send(null);
	    }
	}
	function next_img(evt)
	{
	    var curr_img_idx = img_ls.indexOf(curr_url);
	    if ( curr_img_idx < img_ls.length - 1 ) {
		var request = new XMLHttpRequest();
		show_updating();
		request.open("GET", img_ls[curr_img_idx + 1], true);
		request.onreadystatechange = dpy_img;
		request.send(null);
	    }
	}
	function dpy_img(evt)
	{
	    var request = this;
	    if ( request.readyState == 4 ) {
		if ( !request.responseXML ) {
		    return;
		}
		var new_doc = request.responseXML;
		var new_svg = new_doc.childNodes[1];
		var new_title = new_svg.getElementById("docTitle");
		var new_caption = new_svg.getElementById("caption");
		var new_plot_elems = new_svg.getElementById("plotElements");
		if ( !new_plot_elems || !new_caption ) {
		    hide_updating();
		    return;
		}
		var curr_title = document.getElementById("docTitle");
		curr_title.textContent = new_title.textContent;
		var curr_caption = document.getElementById("caption");
		curr_caption.parentNode.replaceChild(new_caption, curr_caption);
		var curr_plot_elems = document.getElementById("plotElements");
		curr_plot_elems.parentNode.replaceChild(
			new_plot_elems, curr_plot_elems);
		curr_url = request.responseXML.URL;
		hide_updating();
		get_img_ls();
	    }
	}
	prev_btn.addEventListener("click", prev_img, false);
	next_btn.addEventListener("click", next_img, false);

}, false);

