/*
   -	val_buf.h --
   - 		Declarations for functions that copy values from generic
   -		addresses.
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
   .	Please send feedback to dev0@trekix.net
   .
   .	$Revision: 1.1 $ $Date: 2013/02/27 20:39:06 $
 */

#ifndef VAL_BUF_H_
#define VAL_BUF_H_
#include <stddef.h>
#include "type_nbit.h"
#include "swap.h"

void ValBuf_GetBytes(char **, char *, size_t);
I2BYT ValBuf_GetI2BYT(char **);
I4BYT ValBuf_GetI4BYT(char **);
F4BYT ValBuf_GetF4BYT(char **);
F8BYT ValBuf_GetF8BYT(char **);
void ValBuf_PutBytes(char **, char *, size_t);
void ValBuf_PutI2BYT(char **, I2BYT);
void ValBuf_PutI4BYT(char **, I4BYT);
void ValBuf_PutF4BYT(char **, F4BYT);
void ValBuf_PutF8BYT(char **, F8BYT);

#endif
