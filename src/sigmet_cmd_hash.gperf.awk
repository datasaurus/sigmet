#!/usr/bin/awk -f
#
#	sigmet_cmd_hash.gperf.awk --
#		This script reads sigmet_raw command names from standard input
#		and feeds them as keys to gperf, which makes a hash table for
#		them.
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
    printf "%%{\n"
    printf "#include <string.h>\n\n"
    printf "#include \"sigmet.h\"\n\n"
    printf "#include \"sigmet_raw.h\"\n\n"
    printf "%%}\n"
    printf "struct cmd_entr {char *name; int i;};\n\n"
    printf "static struct cmd_entr *in_word_set"
    printf "(register const char *str, register unsigned int len);\n\n"
    printf "%%%%\n"
}
/"/ {
    printf "%s, %d\n", $1, i;
    i++;
}
END {
    printf "%%%%\n"
    printf "int SigmetRaw_Cmd(const char *a)\n";
    printf "{\n"
    printf "    struct cmd_entr *y;\n"
    printf "    return ( a && (y = in_word_set(a, (unsigned int)strlen(a))) )"
    printf " ? y->i : -1;\n"
    printf "}\n"
}
