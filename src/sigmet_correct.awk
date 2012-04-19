#!/usr/bin/awk -f
#
#	sigmet_correct.awk --
#		This awk script reads correction descriptions and generates
#		sigmet_raw commands that apply the corrections. Commands
#		should be trailed with a sigmet_raw socket name. This script
#		is auxiliary to the sigmet_correct script.
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
    printf "sigmet_raw new_field \"%s\" -d \"%s\" -u \"%s\" %s\n",
	    data_type, description, unit, sock
}
/Copy [0-9A-Za-z_]+ to [0-9A-Za-z_]+/ {
    printf "sigmet_raw set_field %s 0.0 %s\n", $4, sock
    printf "sigmet_raw add %s %s %s\n", $4, $2, sock
}
/Add [0-9.Ee-]+ to [0-9A-Za-z_]+/ {
    printf "sigmet_raw add %s %f %s\n", $4, $2, sock
}
/Multiply [0-9A-Za-z_]+ by [0-9.Ee-]+/ {
    printf "sigmet_raw mul %s %f %s\n", $2, $4, sock
}
/Increment time by [0-9.Ee-]+ seconds/ {
    printf "sigmet_raw incr_time %f %s\n", $4, sock
}
/Radar lat = [0-9Ee.-]+/ {
    printf "sigmet_raw radar_lat %f %s\n", $4, sock
}
/Radar lon = [0-9Ee.-]+/ {
    printf "sigmet_raw radar_lon %f %s\n", $4, sock
}
/Add [0-9Ee.-]+ degrees to all azimuths/ {
    printf "sigmet_raw shift_az %f %s\n", $2, sock
}
/Delete / {
    for (n = 2; n <= NF; n++) {
	printf "if sigmet_raw volume_headers %s | grep -q '%s|types';then", \
		sock, $n
	printf " sigmet_raw del_field %s %s;", $n, sock
	printf "else echo Not deleting %s, not in volume;fi\n", $n
    }
}
