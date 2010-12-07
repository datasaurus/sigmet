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
    printf "int Sigmet_DataType_GetN(char *a, enum Sigmet_DataTypeN *y_p)\n";
    printf "{\n"
    printf "    struct type_entr *y;\n\n"
    printf "    if ( !a ) {\n"
    printf "        return 0;\n"
    printf "    }\n"
    printf "    if ( (y = in_word_set(a, (unsigned int)strlen(a))) ) {"
    printf "        *y_p = y->i;\n"
    printf "        return 1;\n"
    printf "    } else {\n"
    printf "        return 0;\n"
    printf "    }\n"
    printf "}\n"
}
