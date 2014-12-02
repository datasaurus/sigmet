/*
   -	swap.c ---
   -		This file defines functions that copy values,
   -		swapping bytes if desired.  See swap (3).
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
   .	$Revision: 1.10 $ $Date: 2012/09/13 19:42:30 $
 */

#include <string.h>
#include "swap.h"
#include "type_nbit.h"

/* If true, we are swapping bytes */
static int swapping;

void Swap_Off(void) { swapping = 0; }

void Swap_On(void) { swapping = 1; }

void Toggle_Swap(void) { swapping = !swapping; }

void Swap_2Byt(void *p)
{
    if (swapping) {
	U2BYT *q = (U2BYT *)p;
	*q = SWAP2BYT(*q);
    }
}

void Swap_4Byt(void *p)
{
    if (swapping) {
	U4BYT *q = (U4BYT *)p;
	*q = SWAP4BYT(*q);
    }
}

void Swap_8Byt(void *p)
{
    if (swapping) {
	char t[8], *p_ = (char *)p;

	memcpy(t, p, 8);
	p_[7] = t[0];
	p_[6] = t[1];
	p_[5] = t[2];
	p_[4] = t[3];
	p_[3] = t[4];
	p_[2] = t[5];
	p_[1] = t[6];
	p_[0] = t[7];
    }
}

void Swap_16Bit(void *p)
{
    if (swapping) {
	U16BIT *q = (U16BIT *)p;
	*q = SWAP16BIT(*q);
    }
}

void Swap_32Bit(void *p)
{
    if (swapping) {
	U32BIT *q = (U32BIT *)p;
	*q = SWAP32BIT(*q);
    }
}
