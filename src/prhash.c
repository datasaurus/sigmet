/*
   -	prhash.local.c --
   -		This application grows a hash table to perfection and then
   -		prints it.
   -	
   .	Usage:
   .		prhash word1 word2 ...
   .
   .	Output will be from Hash_Print.  All entries will have value 1
   .	(arbitrary).
   .
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
   .	$Revision: 1.5 $ $Date: 2012/11/08 21:18:37 $
 */

#include <stdio.h>
#include "hash.h"

int main(int argc, char *argv[])
{
    struct Hash_Tbl h;
    char **a;
    int val = 1;
    unsigned b, e, bb;
    int n;

    if ( !Hash_Init(&h, (unsigned)(argc - 1)) ) {
	fprintf(stderr, "Could not create hash table.\n");
	exit(EXIT_FAILURE);
    }
    for (a = argv + 1; *a; a++) {
	if ( !Hash_Add(&h, *a, &val) ) {
	    fprintf(stderr, "Could not add \"%s\".\n", *a);
	    exit(EXIT_FAILURE);
	}
    }
    Hash_Sz(&h, &b, &e, &bb);
    for (b++ ; bb > 1; b++) {
	if ( !Hash_Adj(&h, b) ) {
	    fprintf(stderr, "Could not grow hash table.\n");
	    exit(EXIT_FAILURE);
	}
	Hash_Sz(&h, &b, &e, &bb);
    }
    printf("#define N_HASH_CMD %d\n", h.n_buckets);
    printf("static char *cmd1v[N_HASH_CMD] = {\n");
    for (n = 0; n < h.n_buckets; n++) {
	if ( h.buckets[n] ) {
	    printf("\"%s\", ", h.buckets[n]->key);
	} else {
	    printf("\"\", ");
	}
	if ( (n + 1) % 8 == 0 ) {
	    printf("\n");
	}
    }
    printf("\n};\n");
    printf("static callback *cb1v[N_HASH_CMD] = {\n");
    for (n = 0; n < h.n_buckets; n++) {
	if ( h.buckets[n] ) {
	    printf("%s_cb, ", h.buckets[n]->key);
	} else {
	    printf("NULL, ");
	}
	if ( (n + 1) % 8 == 0 ) {
	    printf("\n");
	}
    }
    printf("\n};\n");
    return 0;
}
