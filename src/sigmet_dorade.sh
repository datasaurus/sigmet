#!/bin/sh
#
# Make DORADE sweep files for a Sigmet raw volume in the current directory.
# Include DM (returned power) if volume has DB_DBZ or DB_DBZ2.
#
# If CLSEM, SIGMET_RAW_VOL_SZ, SIGMET_SEM_MEM, and SIGMET_SEM_PROC
# are set in the environment, this script will use the clsem
# utility to announce its completion.
#
# Optional environment variables:
#	SIGMET_SEM_PROC			- value = identifier for a semaphore
#					  assumed to be decremented for this
#					  process. When this process terminates,
#					  the associated semaphore will be
#					  incremented with a call to clsem.
#	SIGMET_SEM_MEM			- value = identifier for a semaphore
#					  assumed to be decremented by the
#					  number of megabytes this process will
#					  need to store the volume. When this
#					  process terminates, the associated
#					  semaphore will be incremented with a
#					  call to clsem.
#	SIGMET_RAW_VOL_SZ		- value = estimated number of megabytes
#					  this process will need to store the
#					  volume, needed by clsem if
#					  SIGMET_SEM_MEM is set.
#	SIGMET_CORRECTIONS		- name of a file with correction specifiers
#	SIGMET_GZIP			- if set, compress sweep files when done.
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

CLSEM=${CLSEM:-":"}

# Clean up on exit
cleanup() {
    if test -S $sock
    then
	sigmet_raw unload -f $sock
    fi
    $CLSEM 1 $SIGMET_SEM_PROC
    $CLSEM $SIGMET_RAW_VOL_SZ $SIGMET_SEM_MEM
}
trap " cleanup; " EXIT
trap " echo $0 exiting on TERM signal;exit 1; " TERM
trap " echo $0 exiting on KILL signal;exit 1; " KILL
trap " echo $0 exiting on QUIT signal;exit 1; " QUIT
trap " echo $0 exiting on INT signal;exit 1; " INT
trap " echo $0 exiting on HUP signal;exit 1; " HUP

# Add DM to the volume
dm()
{
    dbz=$1
    sigmet_raw new_field DM -d "Returned power" -u "dBZ" -v $dbz $sock
    sigmet_raw new_field LOGR -d "log10 of distance along beam" \
	    -u "none" -v r_beam $sock
    sigmet_raw log10 LOGR $sock
    sigmet_raw mul LOGR 20.0 $sock
    sigmet_raw sub DM LOGR $sock
    sigmet_raw del_field LOGR $sock
}

if [ $# -eq 1 ] && [ $1 = "-v" ]
then
    echo $0 1.0. Copyright 2011 Gordon D. Carrie. All rights reserved.
    trap "" EXIT
    exit 0
fi
if [ $# -ne 1 ]
then
    echo "Usage: $0 raw_fl"
    exit 1
fi
raw_fl=$1
if ! test -f $raw_fl
then
    echo "$0: no readable file named $raw_fl"
    exit 1
fi
if test $SIGMET_CORRECTIONS
then
    echo "$0: applying corrections from file $SIGMET_CORRECTIONS"
fi

# Start sigmet_raw daemon.
sock=`echo $raw_fl | sed -e 's!.*/!!' -e 's/\.gz$//' -e 's/\.bz2$//'`
if echo $raw_fl | grep -q '\.gz$'
then
    gunzip -c $raw_fl | sigmet_raw load - $sock || exit 1
elif echo $raw_fl | grep -q '\.bz2$'
then
    bzcat -c $raw_fl | sigmet_raw load - $sock || exit 1
else
    sigmet_raw load $raw_fl $sock || exit 1
fi

# Create DM field
if test "`sigmet_raw vol_hdr $sock 2> /dev/null | grep 'DB_DBZ2'`"
then
    dm DB_DBZ2
elif test "`sigmet_raw vol_hdr $sock 2> /dev/null | grep 'DB_DBZ'`"
then
    dm DB_DBZ
fi

# Apply correction information, if any.
if test $SIGMET_CORRECTIONS
then
    sigmet_correct $SIGMET_CORRECTIONS $sock || exit 1
fi

# Make sweep files
sigmet_raw dorade $sock
if test $SIGMET_GZIP
then
    gzip swp*
fi

# Done. Exit will run cleanup function defined above.
exit 0
