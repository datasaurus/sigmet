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

# Where to install things
PREFIX=/usr/local
#PREFIX=${HOME}/local

# Usual places to put things under $PREFIX
BINDIR=${PREFIX}/bin
LIBEXEC_DIR = ${PREFIX}/libexec
MANDIR=${PREFIX}/share/man

# Compilers and options
#
CC = c99
CFLAGS_OPT = -O3
CFLAGS_DEV = -g -Wall -Wmissing-prototypes

#CC = clang
#CFLAGS = -O3

#EFENCE_LIBS = -lefence
LIBS = ${EFENCE_LIBS} -lm

# Leave the rest of the Makefile alone

EXEC_BINS = sigmet_raw sigmet_hdr geog sigmet_good hsv bighi biglo biglrg \
	    bigsml tm_calc color_legend
EXEC_SCRIPTS = sig_raw sigmet_mkcase.awk sigmet_sizex.awk pisa 		\
	       sigmet_svg sigmet_html sigmet_png_kml sigmet_dorade	\
	       sigmet_svg.awk sigmet_vol_tm sigmet_fdupes raster_clrs	\
	       utm_zone dt.awk pisa.awk
LIBEXEC_SCRIPTS = sigmet_svg.js sigmet_buttons.js
EXECS = ${EXEC_BINS} ${EXEC_SCRIPTS}

CP = cp -p -f
RM = rm -fr

.SUFFIXES : .o

.c.o :
	${CC} ${CFLAGS_DEV} -c $<

all : ${EXECS}

install : ${EXECS}
	mkdir -p ${BINDIR}
	${CP} ${EXECS} ${BINDIR}
	mkdir -p ${LIBEXEC_DIR}
	${CP} ${LIBEXEC_SCRIPTS} ${LIBEXEC_DIR}
	mkdir -p ${MANDIR}/man1
	${CP} ../man/man1/*.1 ${MANDIR}/man1
	mkdir -p ${MANDIR}/man3
	${CP} ../man/man3/*.3 ${MANDIR}/man3

distrib :
	./mk_distrib

# Production targets

SIGMET_RAW_SRC = sigmet_raw.c sigmet_vol.c sigmet_data.c sigmet_proj.c \
sigmet_dorade.c dorade_lib.c geog_lib.c geog_proj.c swap.c strlcpy.c \
val_buf.c get_colors.c bisearch_lib.c str.c tm_calc_lib.c hash.c alloc.c
sigmet_raw : ${SIGMET_RAW_SRC} type_nbit.h
	${CC} ${CFLAGS_OPT} -o sigmet_raw ${SIGMET_RAW_SRC} ${LIBS}

SIGMET_HDR_SRC = sigmet_hdr.c sigmet_vol.c sigmet_data.c swap.c geog_lib.c \
geog_proj.c strlcpy.c tm_calc_lib.c alloc.c
sigmet_hdr : ${SIGMET_HDR_SRC}
	${CC} ${CFLAGS_OPT} -o sigmet_hdr ${SIGMET_HDR_SRC} ${LIBS}

SIGMET_GOOD_SRC = sigmet_good.c sigmet_vol.c sigmet_data.c swap.c geog_lib.c \
geog_proj.c strlcpy.c tm_calc_lib.c alloc.c
sigmet_good : ${SIGMET_GOOD_SRC}
	${CC} ${CFLAGS_OPT} -o sigmet_good ${SIGMET_GOOD_SRC} ${LIBS}

GEOG_SRC = geog_app.c geog_lib.c geog_proj.c alloc.c
geog : ${GEOG_SRC}
	${CC} ${CFLAGS_OPT} -o geog ${GEOG_SRC} ${LIBS}

TM_CALC_SRC = tm_calc_app.c tm_calc_lib.c str.c alloc.c
tm_calc : ${TM_CALC_SRC}
	${CC} ${CFLAGS_OPT} -o tm_calc ${TM_CALC_SRC} ${LIBS}

hsv : hsv_app.c hsv_lib.c hsv_lib.h
	${CC} ${CFLAGS_OPT} -o hsv hsv_app.c hsv_lib.c ${LIBS}

bighi : bighi.c
	${CC} ${CFLAGS_OPT} -o bighi bighi.c ${LIBS}

biglo : biglo.c
	${CC} ${CFLAGS_OPT} -o biglo biglo.c ${LIBS}

biglrg : biglrg.c
	${CC} ${CFLAGS_OPT} -o biglrg biglrg.c ${LIBS}

bigsml : bigsml.c
	${CC} ${CFLAGS_OPT} -o bigsml bigsml.c ${LIBS}

color_legend : color_legend.c
	${CC} ${CFLAGS_OPT} -o color_legend color_legend.c ${LIBS}

# Debug targets

dev : sigmet_raw_dev sigmet_hdr_dev sigmet_good_dev geog_dev tm_calc_dev \
hsv_dev bighi_dev biglo_dev biglrg_dev bigsml_dev color_legend_dev

SIGMET_RAW_OBJ = sigmet_raw.o sigmet_vol.o sigmet_data.o sigmet_proj.o \
sigmet_dorade.o dorade_lib.o geog_lib.o geog_proj.o swap.o strlcpy.o \
val_buf.o get_colors.o bisearch_lib.o str.o tm_calc_lib.o hash.o alloc.o
sigmet_raw_dev : ${SIGMET_RAW_OBJ}
	${CC} -o sigmet_raw ${SIGMET_RAW_OBJ} ${LIBS}

SIGMET_TEST_OBJ = sigmet_test.o sigmet_vol.o sigmet_data.o \
geog_lib.o geog_proj.o swap.o strlcpy.o tm_calc_lib.o alloc.o
sigmet_test : ${SIGMET_TEST_OBJ} type_nbit.h
	${CC} -o sigmet_test ${SIGMET_TEST_OBJ} ${LIBS}

SIGMET_HDR_OBJ = sigmet_hdr.o sigmet_vol.o sigmet_data.o swap.o geog_lib.o \
geog_proj.o strlcpy.o tm_calc_lib.o alloc.o
sigmet_hdr_dev : ${SIGMET_HDR_OBJ}
	${CC} -o sigmet_hdr ${SIGMET_HDR_OBJ} ${LIBS}

SIGMET_GOOD_OBJ = sigmet_good.o sigmet_vol.o sigmet_data.o swap.o geog_lib.o \
geog_proj.o strlcpy.o tm_calc_lib.o alloc.o
sigmet_good_dev : ${SIGMET_GOOD_OBJ}
	${CC} -o sigmet_good ${SIGMET_GOOD_OBJ} ${LIBS}

GEOG_OBJ = geog_app.o geog_lib.o geog_proj.o alloc.o
geog_dev : ${GEOG_OBJ}
	${CC} -o geog ${GEOG_OBJ} ${LIBS}

TM_CALC_OBJ = tm_calc_app.o tm_calc_lib.o str.o alloc.o
tm_calc_dev : ${TM_CALC_OBJ}
	${CC} -o tm_calc ${TM_CALC_OBJ} ${LIBS}

HSV_OBJ =  hsv_app.o hsv_lib.o
hsv_dev : ${HSV_OBJ}
	${CC} -o hsv ${HSV_OBJ} ${LIBS}

bighi_dev : bighi.c
	${CC} ${CFLAGS_DEV} -o bighi bighi.c ${LIBS}

biglo_dev : biglo.c
	${CC} ${CFLAGS_DEV} -o biglo biglo.c ${LIBS}

biglrg_dev : biglrg.c
	${CC} ${CFLAGS_DEV} -o biglrg biglrg.c ${LIBS}

bigsml_dev : bigsml.c
	${CC} ${CFLAGS_DEV} -o bigsml bigsml.c ${LIBS}

color_legend_dev : color_legend.c
	${CC} ${CFLAGS_DEV} -o color_legend color_legend.c ${LIBS}

sigmet.h : type_nbit.h

sigmet_raw.o : sigmet_raw.c sigmet_raw.h sigmet.h

sigmet_test.o : sigmet_test.c sigmet.h

sigmet_good.o : sigmet_good.c sigmet.h

sigmet_vol.o : sigmet_vol.c sigmet.h

sigmet_data.o : sigmet_data.c sigmet.h

sigmet_proj.o : sigmet_proj.c sigmet_proj.h

sigmet_dorade.o : sigmet_dorade.c sigmet.h dorade_lib.h type_nbit.h

sigmet_hdr.o : sigmet_hdr.c sigmet.h

dorade_lib.o : dorade_lib.c dorade_lib.h type_nbit.h

geog_app.o : geog_app.c geog_lib.h
	${CC} ${CFLAGS} -c geog_app.c

geog_lib.o : geog_lib.c geog_proj.h geog_lib.h

geog_proj.o : geog_proj.c geog_proj.h geog_lib.h

type_nbit.h : mk_type_nbit
	./mk_type_nbit > type_nbit.h

mk_type_nbit : mk_type_nbit.c

strlcpy.o : strlcpy.c

swap.o : swap.c type_nbit.h

val_buf.o : val_buf.c type_nbit.h

bisearch_lib.o : bisearch_lib.c bisearch_lib.h

get_colors.o : get_colors.c get_colors.h

tm_calc_lib.o : tm_calc_lib.c tm_calc_lib.h

hsv_app.o : hsv_app.c

hsv_lib.o : hsv_lib.c

alloc.o : alloc.c alloc.h

str.o : str.c

SRC = prhash.c hash.c alloc.c
prhash : ${SRC}
	${CC} ${CFLAGS} -o prhash ${SRC} ${LFLAGS}

sigmet_correct : sigmet_correct.awk

sigmet_correct.awk : sigmet_raw

sigmet_dorade : sigmet_raw
	:

sigmet_mkcase.awk :

sigmet_sizex.awk :

sigmet_stor_corr :

pisa :

sigmet_png_kml : sigmet_raw sigmet_svg.awk

sigmet_svg.awk :

sigmet_vol_tm :

raster_clrs : hsv

utm_zone : geog

dt.awk :

clean :
	${RM} ${EXEC_BINS} sigmet_test prhash core *.core *.o type_nbit.h \
	    mk_type_nbit *.tmp *.dSYM _curr_note a.out

