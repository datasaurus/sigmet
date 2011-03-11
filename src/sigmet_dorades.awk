#!/usr/bin/awk -f

# From standard input, read lines containing correction specifiers
# or volume identifiers. Send to standard output in a form suitable
# as standard input for sigmet_dorades.sh.

/START +MUL/ {
    printf "export SIGMET_MUL=\"%s %f\"\n", $7, $8
}
/END +MUL/ {
    printf "unset SIGMET_MUL\n"
}
/START +DT/ {
    printf "export SIGMET_DT=%f\n", $7
}
/END +DT/ {
    printf "unset SIGMET_DT\n"
}
/START +LAL\/LON/ {
    printf "export SIGMET_LAT_LON=\"%f %f\"\n", $7, $8
}
/END +LAT\/LON/ {
    printf "unset SIGMET_LAT_LON\n"
}
/START +DAZ/ {
    printf "export SIGMET_DAZ=%f\n", $7
}
/END +DAZ/ {
    printf "unset SIGMET_DAZ\n"
}
/START +DEL_FIELDS/ {
    printf "export SIGMET_DEL_FIELDS=\""
    for (n = 7; n < NF; n++) {
	printf "%s ", $n
    }
    printf "\"\n"
}
/END +DEL_FIELDS/ {
    printf "unset SIGMET_DEL_FIELDS\n"
}
/^[^\/].*\.RAW/ {
    printf "%s/%s\n", ENVIRON["PWD"], $4
}
/^\/.*\.RAW/ {
    printf "%s\n", $4
}
