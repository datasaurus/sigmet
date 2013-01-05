#!/usr/bin/awk -f
#
# Helper for sigmet_ppi_svg
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

/x_min/ {
    if ( x_min == "nan" ) {
	x_min = $2;
    }
    hav_x_min = 1;
}
/x_max/ {
    if ( x_max == "nan" ) {
	x_max = $2;
    }
    hav_x_max = 1;
}
/y_min/ {
    if ( y_min == "nan" ) {
	y_min = $2;
    }
    hav_y_min = 1;
}
/y_max/ {
    if ( y_max == "nan" ) {
	y_max = $2;
    }
    if ( !hdr_sent && hav_x_min && hav_x_max && hav_y_min ) {
	w = x_max - x_min;
	h = y_max - y_min;
	y1 = y_min + h;
	w_px = ENVIRON["SIGMET_RAW_IMG_SZ"];
	if ( w_px == 0 ) {
	    w_px = 800;
	}
	h_px = w_px / w * h;
	printf  "<?xml version=\"1.0\"";
	printf  " encoding=\"ISO-8859-1\" standalone=\"no\"?>\n";
	print  "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\"";
	print  "    \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">";
	print  "<svg";
	print  "    xmlns=\"http://www.w3.org/2000/svg\"";
	print  "    xmlns:xlink=\"http://www.w3.org/1999/xlink\"";
	printf "    x=\"%.1f\"\n", ENVIRON["LEFT"];
	printf "    y=\"%.1f\"\n", ENVIRON["TOP"];
	printf "    width=\"%.1f\"", w_px;
	printf "    height=\"%.1f\"", h_px;
	printf "    viewBox=\"0.0 0.0 %.1f %.1f\">\n", w, h;
	printf "<g transform=\"matrix(1 0 0 -1 %.1f %.1f)\">\n", -x_min, y1;
	hdr_sent = 1;
    }
}
/color/ {
    if ( poly ) {
	print "\"/>\n";
    }
    printf "<path style=\"fill: %s;\" d=\"\n", $2
	poly = 1;
}
/gate/ {
    printf "M %.1f %.1f L %.1f %.1f L %.1f %.1f L %.1f %.1f Z\n", \
	$2, $3, $4, $5, $6, $7, $8, $9;
}
END {
    if ( poly ) {
	printf "\"/>\n";
    }
    printf "</g>\n</svg>\n"
}
