/*
   -	mk_type_nbit.c --
   -		Print a header file that associates
   -		standard C types with local types that
   -		have explicit bit and byte sizes.
   -
   .	Copyright (c) 2011, Gordon D. Carrie. All rights reserved.
   .	
   .	Redistribution and use in source and binary forms, with or without
   .	modification, are permitted provided that the following conditions
   .	are met:
   .	
   .	    * Redistributions of source code must retain the above copyright
   .	    notice, this list of conditions and the following disclaimer.
   .
   .	    * Redistributions in binary form must reproduce the above copyright
   .	    notice, this list of conditions and the following disclaimer in the
   .	    documentation and/or other materials provided with the distribution.
   .	
   .	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   .	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   .	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   .	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   .	HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   .	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   .	TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   .	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   .	LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   .	NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   .	SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
   .
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.12 $ $Date: 2012/11/08 21:06:48 $
 */

#include <stdio.h>
#include <limits.h>

/* C data types and their sizes */
#define NTYPES 10
struct {
    char typ;	/* Type: 'I' => signed integer type.
		   'U' => unsigned integer type.
		   'F' => floating point type */
    char *nm;	/* Name of standard C type */
    int bits;	/* Number of bits in type */
    int bytes;	/* Number of bytes in type */
} types[NTYPES] = {
    {'I', "char",
	CHAR_BIT * sizeof(char), sizeof(char)},
    {'U', "unsigned char",
	CHAR_BIT * sizeof(unsigned char), sizeof(unsigned char)},
    {'I', "short",
	CHAR_BIT * sizeof(short), sizeof(short)},
    {'U', "unsigned short",
	CHAR_BIT * sizeof(unsigned short), sizeof(unsigned short)},
    {'I', "int",
	CHAR_BIT * sizeof(int), sizeof(int)},
    {'U', "unsigned int",
	CHAR_BIT * sizeof(unsigned int), sizeof(unsigned int)},
    {'I', "long",
	CHAR_BIT * sizeof(long), sizeof(long)},
    {'U', "unsigned long",
	CHAR_BIT * sizeof(unsigned long), sizeof(unsigned long)},
    {'F', "float", CHAR_BIT * sizeof(float), sizeof(float)},
    {'F', "double", CHAR_BIT * sizeof(double), sizeof(double)}};

int main(void)
{
    int n;
    unsigned long m, mask;

    /* Print start of header wrapper */
    printf( "#ifndef TYPE_NBIT_H_\n"
	    "#define TYPE_NBIT_H_\n\n");

    /* Print lines that declare types whose names indicate bit count */
    printf( "/*\n"
	    "   I8BIT => 8 bit integer.\n"
	    "   U8BIT => 8 bit unsigned integer\n"
	    "   I16BIT => 16 bit integer.\n"
	    "   U16BIT => 16 bit unsigned integer\n"
	    "   I32BIT => 32 bit integer.\n"
	    "   U32BIT => 32 bit unsigned integer\n"
	    "   I64BIT => 64 bit integer (if any).\n"
	    "   U64BIT => 64 bit unsigned integer (if any)\n"
	    "   F32BIT => 32 bit floating point type\n"
	    "   F64BIT => 64 bit floating point type\n"
	    " */\n\n");
    for (n = 0; n < NTYPES; n++) {
	printf( "#ifndef TYPE_NBIT_%c%dBIT\n"
		"#define TYPE_NBIT_%c%dBIT\n"
		"typedef %s %c%dBIT;\n"
		"#endif\n",
		types[n].typ, types[n].bits,
		types[n].typ, types[n].bits,
		types[n].nm, types[n].typ, types[n].bits);
    }
    printf("\n");

    /* Print lines that declare types whose names indicate byte count */
    printf( "/*\n"
	    "   I1BYT => 1 byte integer.\n"
	    "   U1BYT => 1 byte unsigned integer\n"
	    "   I2BYT => 2 byte integer.\n"
	    "   U2BYT => 2 byte unsigned integer\n"
	    "   I4BYT => 4 byte integer.\n"
	    "   U4BYT => 4 byte unsigned integer\n"
	    "   I1BYT => 1 byte integer (if any).\n"
	    "   U1BYT => 1 byte unsigned integer (if any)\n"
	    "   F4BYT => 4 byte floating point type\n"
	    "   F1BYT => 1 byte floating point type\n"
	    " */\n\n");
    for (n = 0; n < NTYPES; n++) {
	printf( "#ifndef TYPE_NBYT_%c%dBYT\n"
		"#define TYPE_NBYT_%c%dBYT\n"
		"typedef %s %c%dBYT;\n"
		"#endif\n",
		types[n].typ, types[n].bytes,
		types[n].typ, types[n].bytes,
		types[n].nm, types[n].typ, types[n].bytes);
    }
    printf("\n");

    /* Print macros for byte swapping */
    printf("/* Byte swapping macros. */\n");
    for (m = mask = 1, n = 0; n < CHAR_BIT; n++, m <<= 1) {
	mask |= m;
    }
    printf("#define B0 %#lX\n", mask);
    for (n = 1; n < sizeof(long); n++) {
	mask <<= CHAR_BIT;
	printf("#define B%d %#lX\n", n, mask);
    }
    printf("#define SWAP2BYT(s) \\\n"
	    "        ((((U2BYT)s & B0) << %d) | (((U2BYT)s & B1) >> %d))\n"
	    "#define SWAP4BYT(i) \\\n"
	    "        ((((U4BYT)i & B0) << %d) | (((U4BYT)i & B1) << %d) \\\n"
	    "        | (((U4BYT)i & B2) >> %d) | (((U4BYT)i & B3) >> %d))\n",
	    CHAR_BIT, CHAR_BIT, 3 * CHAR_BIT, CHAR_BIT, CHAR_BIT, 3 * CHAR_BIT);
    printf("#define SWAP16BIT(s) \\\n"
	    "        ((((U16BIT)s & 0xFF) << 8) "
	    "| (((U16BIT)s & 0xFF00) >> 8))\n"
	    "#define SWAP32BIT(i) \\\n"
	    "        ((((U32BIT)i & 0xFF) << 24) "
	    "| (((U32BIT)i & 0xFF00) << 8) \\\n"
	    "        | (((U32BIT)i & 0xFF0000) >> 8) "
	    "| (((U32BIT)i & 0xFF000000) >> 24))\n");

    /* End wrapper */
    printf("\n#endif\n");
    return 0;
}
