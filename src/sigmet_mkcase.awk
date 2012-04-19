#!/usr/bin/awk -f
#
# Read lines from standard input of form:
#	SiteYYMMDDHHMMSS*
# assumed to be file names.  For each case (day-hour) in the input,
# generate commands to make a directory and for the case and link or move
# the files named on standard input into it.
#
# Select whether to link or move with environment variable SIGMET_MV.
# Output commands will be of form $SIGMET_MV file_name case_dir/
# If not set, defaults to ln.
#
# Sample usage:
# find file_dir ... | sigmet_vol_tm | sort -un | dt.awk | sigmet_mkcase.awk \
#	| while read l;do eval $l;done
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

BEGIN {
    # sfx provides suffices for multiple cases on the same day.
    sfx[1]="B"
    sfx[2]="C"
    sfx[3]="D"
    sfx[4]="E"
    sfx[5]="F"
    sfx[6]="G"

    dat_ptn = "[0-9][0-9][0-9][0-9][0-9][0-9]"
    tm_ptn = "[0-9][0-9][0-9][0-9][0-9][0-9]"

    if ( length(ENVIRON["SIGMET_MV"]) == 0 ) {
	move = "ln";
    } else {
	move = ENVIRON["SIGMET_MV"];
    }
}
/^###/ {
    # dt.awk puts a ### line at case boundaries. Start
    # processing new case at next line.
    getline
    date = $2
    time = $3
    split(date, ymd, "/")
    month = ymd[2]
    day = ymd[3]

    # c stores a count of cases for the day. If there is more than
    # one case for a day, subsequent cases are given a suffix from
    # the sfx array.
    if ( c[month,day] == 0 ) {
	printf "dir=%02d%02d\n", month, day
    } else if ( c[month,day] == 1 ) {
	printf "mv %02d%02d %02d%02dA\n", month, day, month, day
	printf "dir=%02d%02dB\n", month, day
    } else {
	suffix = sfx[c[month,day]]
	printf "dir=%02d%02d%s\n", month, day, suffix
    }
    printf "mkdir -p $dir\n"
    c[month,day]++
}
/^[0-9]+/ {
    printf "%s %s $dir\n", move, $4
}
