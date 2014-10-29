/*
   -	cmd_tbl.c --
   -		This application grows a hash table to perfection and then
   -		prints it.
   -	
   .	Usage:
   .		cmd_tbl cmd1 cmd2 ...
   .
   .	Copyright (c) 2012, Gordon D. Carrie. All rights reserved.
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
   .	$Revision: $ $Date: $
 */

#include <stdio.h>
#include "err_msg.h"
#include "hash.h"

int main(int argc, char *argv[])
{
    struct Hash_Tbl h;
    char **a;
    unsigned b, e, bb;
    struct Hash_Entry **bp, **bp1, *ep;

    if ( !Hash_Init(&h, (unsigned)(argc - 1)) ) {
	fprintf(stderr, "Could not create hash table.\n");
	exit(EXIT_FAILURE);
    }
    for (a = argv + 1; *a; a++) {
	if ( !Hash_Add(&h, *a, a) ) {
	    fprintf(stderr, "Could not add \"%s\".\n%s\n", *a, Err_Get());
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
    for (bp = tblP->buckets, bp1 = bp + tblP->n_buckets; bp < bp1; bp++) {
	for (ep = *bp; ep; ep = ep->next) {
	    printf("\"%s %s\",", ep->key, ep->val);
	}
    }
    return 0;
}
