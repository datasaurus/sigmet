#!/usr/bin/awk -f
#
#	sigmet_correct.awk --
#		This awk script reads correction descriptions and generates
#		sigmet_raw commands that apply the corrections.
#		This script is auxiliary to the sigmet_correct script.
#		
# Copyright (c) 2011, Gordon D. Carrie. All rights reserved.
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

/New field/ {
    split($0, w, "|")
    data_type = w[2]
    description = w[3]
    unit = w[4]
    sub("^ *", "", data_type)
    sub(" *$", "", data_type)
    sub("^ *", "", description)
    sub(" *$", "", description)
    sub("^ *", "", unit)
    sub(" *$", "", unit)
    printf "new_field \"%s\" -d \"%s\" -u \"%s\"\n",
	    data_type, description, unit
}
/Copy [0-9A-Za-z_]+ to [0-9A-Za-z_]+/ {
    printf "set_field %s 0.0\n", $4
    printf "add %s %s\n", $4, $2
}
/Add [0-9.Ee-]+ to [0-9A-Za-z_]+/ {
    printf "add %s %f\n", $4, $2
}
/Multiply [0-9A-Za-z_]+ by [0-9.Ee-]+/ {
    printf "mul %s %f\n", $2, $4
}
/Increment time by [0-9.Ee-]+ seconds/ {
    printf "incr_time %f\n", $4
}
/Radar lat = [0-9Ee.-]+/ {
    printf "radar_lat %f\n", $4
}
/Radar lon = [0-9Ee.-]+/ {
    printf "radar_lon %f\n", $4
}
/Add [0-9Ee.-]+ degrees to all azimuths/ {
    printf "shift_az %f\n", $2
}
/Delete / {
    for (n = 2; n <= NF; n++) {
	printf "if volume_headers | grep -q '%s|types';then", $n
	printf " del_field %s;", $n
	printf "else echo Not deleting %s, not in volume 1>&2 | : ;fi\n", $n
    }
}
