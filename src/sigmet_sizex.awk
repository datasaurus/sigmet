#!/usr/bin/awk -f
#
# Estimate amount of memory a volume needs given output from
# "sigmet_raw vol_hdr" or "sigmet_hdr -a".
#
# Usage example - estimate file size in MB:
#	sz_b=`sigmet_raw vol_hdr $raw_fl | sigmet_sizex.awk`
#	printf 'scale=0;1 + %s / 1024.0 / 1024.0\n' $sz_b | bc
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
    tot = 1;
    incr["DB_XHDR"]=0;
    incr["DB_DBT"]=1;
    incr["DB_DBZ"]=1;
    incr["DB_VEL"]=1;
    incr["DB_WIDTH"]=1;
    incr["DB_ZDR"]=1;
    incr["DB_DBZC"]=1;
    incr["DB_DBT2"]=2;
    incr["DB_DBZ2"]=2;
    incr["DB_VEL2"]=2;
    incr["DB_WIDTH2"]=2;
    incr["DB_ZDR2"]=2;
    incr["DB_RAINRATE2"]=2;
    incr["DB_KDP"]=1;
    incr["DB_KDP2"]=2;
    incr["DB_PHIDP"]=1;
    incr["DB_VELC"]=1;
    incr["DB_SQI"]=1;
    incr["DB_RHOHV"]=1;
    incr["DB_RHOHV2"]=2;
    incr["DB_DBZC2"]=2;
    incr["DB_VELC2"]=2;
    incr["DB_SQI2"]=2;
    incr["DB_PHIDP2"]=2;
    incr["DB_LDRH"]=1;
    incr["DB_LDRH2"]=2;
    incr["DB_LDRV"]=1;
    incr["DB_LDRV2"]=2;
}
/DB_/ {
    gsub(".*=", "");
    gsub("\"", "");
    dt_tot = 0;
    for (n = 1; n <= NF; n++) {
	dt_tot += incr[$n];
    }
    tot *= dt_tot;
}
/num_sweeps/ {
    split($0, a, "=");
    tot *= a[2];
}
/num_rays/ {
    split($0, a, "=");
    tot *= a[2];
}
/num_bins/ {
    split($0, a, "=");
    tot *= a[2];
}
END {
    print tot;
}
