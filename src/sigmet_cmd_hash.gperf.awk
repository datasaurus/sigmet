#!/usr/bin/awk -f
# This script reads sigmet_raw command names from standard input and feeds them as
# keys to gperf, which makes a hash table for them.

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
    printf "int SigmetRaw_CmdI(const char *a)\n";
    printf "{\n"
    printf "    struct cmd_entr *y;\n"
    printf "    return ( a && (y = in_word_set(a, (unsigned int)strlen(a))) )"
    printf " ? y->i : -1;\n"
    printf "}\n"
}
