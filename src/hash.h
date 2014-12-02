/*
   -	hash.h --
   -		This file declares hash table functions
   -		and data structures.
   -		See hash (3).
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
   .	$Revision: 1.24 $ $Date: 2013/02/20 18:40:25 $
 */

#ifndef HASH_H_
#define HASH_H_

#include <stdlib.h>

/* Hash table entry */
struct Hash_Entry {
    char *key;				/* String identifier */
    void *val;				/* Value associated with string */
    struct Hash_Entry *next;		/* Pointer to next entry in bucket
					 * chain */
};

/* Hash table */
struct Hash_Tbl {
    struct Hash_Entry **buckets;	/* Bucket array.  Each element is a
					 * linked list of entries */
    unsigned n_buckets;
    unsigned n_entries;
};

/* Global functions */
int Hash_Init(struct Hash_Tbl *, unsigned);
void Hash_Clear(struct Hash_Tbl *);
unsigned Hash(const char *, unsigned);
int Hash_Add(struct Hash_Tbl *, const char *, void *);
int Hash_Set(struct Hash_Tbl *, const char *, void *);
void * Hash_Get(struct Hash_Tbl *, const char *);
void Hash_Print(struct Hash_Tbl *tblP);
int Hash_Adj(struct Hash_Tbl *, unsigned);
void Hash_Rm(struct Hash_Tbl *, const char *);
void Hash_Sz(struct Hash_Tbl *, unsigned *, unsigned *, unsigned *);

#endif
