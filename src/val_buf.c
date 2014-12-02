/*
   -    val_buf.c --
   -		Copy values of various types to or from addresses.
   -
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
   .    Please send feedback to dev0@trekix.net
   .
   .    $Revision: 1.1 $ $Date: 2013/02/27 20:39:23 $
   .
 */

#include <string.h>
#include "type_nbit.h"
#include "val_buf.h"
#include "swap.h"

void ValBuf_GetBytes(char **buf_p, char *s, size_t n)
{
    memcpy(s, *buf_p, n);
    *buf_p += n;
}

I2BYT ValBuf_GetI2BYT(char **buf_p)
{
    I2BYT i = *(I2BYT *)*buf_p;
    *buf_p += 2;
    Swap_2Byt(&i);
    return i;
}

I4BYT ValBuf_GetI4BYT(char **buf_p)
{
    I4BYT i = *(I4BYT *)*buf_p;
    *buf_p += 4;
    Swap_4Byt(&i);
    return i;
}

F4BYT ValBuf_GetF4BYT(char **buf_p)
{
    F4BYT f = *(F4BYT *)*buf_p;
    *buf_p += 4;
    Swap_4Byt(&f);
    return f;
}

F8BYT ValBuf_GetF8BYT(char **buf_p)
{
    F8BYT f = *(F8BYT *)*buf_p;
    *buf_p += 8;
    Swap_8Byt(&f);
    return f;
}

void ValBuf_PutBytes(char **buf_p, char *s, size_t n)
{
    memcpy(*buf_p, s, n);
    *buf_p += n;
}

void ValBuf_PutI2BYT(char **buf_p, I2BYT i)
{
    Swap_2Byt(&i);
    *(I2BYT *)*buf_p = i;
    *buf_p += 2;
}

void ValBuf_PutI4BYT(char **buf_p, I4BYT i)
{
    Swap_4Byt(&i);
    *(I4BYT *)*buf_p = i;
    *buf_p += 4;
}

void ValBuf_PutF4BYT(char **buf_p, F4BYT f)
{
    Swap_4Byt(&f);
    *(F4BYT *)*buf_p = f;
    *buf_p += 4;
}

void ValBuf_PutF8BYT(char **buf_p, F8BYT f)
{
    Swap_8Byt(&f);
    *(F8BYT *)*buf_p = f;
    *buf_p += 8;
}
