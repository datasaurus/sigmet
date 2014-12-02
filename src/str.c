/*
   -	str.c --
   -		This file defines string manipulation
   -		functions.  See str (3).
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
   .	$Revision: 1.26 $ $Date: 2013/01/10 21:23:36 $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include "str.h"

/* Largest possible number of elements in array of strings */
static int mx = INT_MAX / (int)sizeof(char *);

char *Str_Esc(char *str)
{
    char *p, *q, t;
    int n;

    for (p = q = str; *q; p++, q++) {
	if (*q == '\\') {
	    switch (*++q) {
		/* Replace escape sequence with associated character */
		case 'a':
		    *p = '\a';
		    break;
		case 'b':
		    *p = '\b';
		    break;
		case 'f':
		    *p = '\f';
		    break;
		case 'n':
		    *p = '\n';
		    break;
		case 'r':
		    *p = '\r';
		    break;
		case 't':
		    *p = '\t';
		    break;
		case 'v':
		    *p = '\v';
		    break;
		case '\'':
		    *p = '\'';
		    break;
		case '\\':
		    *p = '\\';
		    break;
		case '0':
		    /* Escape sequence is a sequence of octal digits. */
		    n = (int)strspn(++q, "01234567");
		    t = *(q + n);
		    *(q + n) = '\0';
		    *p = (char)strtoul(q, &q, 8);
		    *q-- = t;
		    break;
		default:
		    *p = *q;
	    }
	} else {
	    *p = *q;
	}
    }
    *p = '\0';

    return str;
}

char ** Str_Words(char *ln, char **argv, int *argc)
{
    char **v = NULL;		/* Array of words from ln. */
    char **t;			/* Temporary */
    int cx;			/* Number of words that can be stored at v */
    int cx2;			/* New value for cx when reallocating */
    int c;			/* Number of words in ln */
    char *p, *q;		/* Pointers into ln */
    int inwd;			/* If true, p points into a word */
    size_t sz;			/* Temporary */

    if (argv) {
	v = argv;
	cx = *argc;
	if (cx < 2) {
	    if ( !(t = (char **)realloc(v, 2 * sizeof(char *))) ) {
		free(v);
		*argc = -1;
		fprintf(stderr, "Could not allocate word array.\n");
		return NULL;
	    }
	    v = t;
	    cx = 2;
	}
    } else {
	cx = 2;
	if ( !(v = (char **)calloc((size_t)cx, sizeof(char *))) ) {
	    *argc = -1;
	    fprintf(stderr, "Could not allocate word array.\n");
	    return NULL;
	}
    }
    for (p = q = ln, inwd = 0, c = 0; *p; p++) {
	if ( isspace(*p) ) {
	    if ( inwd ) {
		*q++ = '\0';
	    }
	    inwd = 0;
	} else {
	    if ( !inwd ) {
		/* Have found start of a new word */
		if (c + 1 > cx) {
		    cx2 = 3 * cx / 2 + 4;
		    sz = (size_t)cx2 * sizeof(char *);
		    if (cx2 > mx || !(t = (char **)realloc(v, sz)) ) {
			free(v);
			*argc = -1;
			fprintf(stderr, "Could not allocate word array.\n");
			return NULL;
		    }
		    v = t;
		    cx = cx2;
		}
		v[c++] = q;
		inwd = 1;
	    }

	    if ( *p == '"' || *p == '\'' ) {
		/* Append run of characters bounded by quotes */
		char *e = strchr(p + 1, *p);

		if ( !e ) {
		    fprintf(stderr, "Unbalanced quote.\n");
		    free(v);
		    *argc = -1;
		    return NULL;
		}
		strncpy(q, p + 1, e - p - 1);
		q += e - p - 1;
		p = e;
	    } else {
		/* Append character */
		*q++ = *p;
	    }
	}
    }
    *q = '\0';
    *argc = c;
    sz = (size_t)(c + 1) * sizeof(char *);
    if ( (c + 1) > mx || !(t = (char **)realloc(v, sz)) ) {
	free(v);
	*argc = -1;
	fprintf(stderr, "Could not allocate word array.\n");
	return NULL;
    }
    v = t;
    v[c] = NULL;
    return v;
}

char * Str_Append(char *dest, size_t *l, size_t *lx, char *src, size_t n)
{
    char *t;
    size_t lx2;

    lx2 = *l + n + 1;
    if ( *lx < lx2 ) {
	size_t tx = *lx;

	while (tx < lx2) {
	    tx = (tx * 3) / 2 + 4;
	}
	if ( !(t = realloc(dest, tx)) ) {
	    fprintf(stderr, "Could not grow string for appending.\n");
	    return NULL;
	}
	dest = t;
	*lx = tx;
    }
    strncpy(dest + *l, src, n);
    *l += n;
    return dest;
}

int Str_GetLn(FILE *in, char eol, char **ln, size_t *l_max)
{
    int i;			/* Input character */
    char c;			/* Input character */
    char *t, *t1;		/* Input line */
    size_t n;			/* Number of characters in ln */
    size_t nx;			/* Temporarily hold value for *l_max */

    if ( !*ln ) {
	nx = 4;
	if ( !(t = (char *)malloc((size_t)nx)) ) {
	    fprintf(stderr, "Could not allocate memory for line.\n");
	    return 0;
	}
    } else {
	t = *ln;
	nx = *l_max;
    }
    n = 0;
    while ( (i = fgetc(in)) != EOF && (i != eol) ) {
	c = i;
	if ( !(t1 = Str_Append(t, &n, &nx, &c, (size_t)1)) ) {
	    free(t);
	    fprintf(stderr, "Could not append input character to string.\n");
	    return 0;
	}
	t = t1;
    }
    if ( !(*ln = (char *)realloc(t, (size_t)(n + 1))) ) {
	free(t);
	*ln = NULL;
	*l_max = 0;
	fprintf(stderr, "Could not reallocate memory for line.\n");
	return 0;
    }
    *(*ln + n) = '\0';
    *l_max = n + 1;
    if ( ferror(in) ) {
	perror(NULL);
	return 0;
    } else if ( feof(in) ) {
	return EOF;
    } else {
	return 1;
    }
}
