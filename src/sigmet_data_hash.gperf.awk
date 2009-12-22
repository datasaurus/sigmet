#!/usr/bin/awk -f
# This script reads Sigmet data type names from standard input and feeds them as
# keys to gperf, which makes a hash table for them.

BEGIN {
    printf "%%{\n"
    printf "#include <string.h>\n\n"
    printf "#include \"sigmet.h\"\n\n"
    printf "%%}\n"
    printf "struct type_entr {char *name; int i;};\n\n"
    printf "static struct type_entr *in_word_set"
    printf "(register const char *str, register unsigned int len);\n\n"
    printf "%%%%\n"
}
/DB_/ {
    printf "%s, %d\n", $1, i;
    i++;
}
END {
    printf "%%%%\n"
    printf "enum Sigmet_DataType Sigmet_DataType(char *a)\n";
    printf "{\n"
    printf "    struct type_entr *y;\n"
    printf "    return (y = in_word_set(a, (unsigned int)strlen(a)))"
    printf " ? y->i : DB_ERROR;\n"
    printf "}\n"
}
