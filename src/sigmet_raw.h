/*
   -	sigmet_raw.h --
   -		Declarations for the sigmet_raw daemon and client.
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
   .	$Revision: 1.37 $ $Date: 2012/01/24 18:51:46 $
 */

#ifndef SIGMET_RAW_H_
#define SIGMET_RAW_H_

#include <stdio.h>
#include "sigmet.h"

/* Global functions */
void SigmetRaw_Load(char *, char *);
typedef int (SigmetRaw_Callback)(int , char **);

/*
   Callbacks for the subcommands.
 */

SigmetRaw_Callback version_cb;
SigmetRaw_Callback pid_cb;
SigmetRaw_Callback load_cb;
SigmetRaw_Callback data_types_cb;
SigmetRaw_Callback volume_headers_cb;
SigmetRaw_Callback vol_hdr_cb;
SigmetRaw_Callback near_sweep_cb;
SigmetRaw_Callback sweep_headers_cb;
SigmetRaw_Callback ray_headers_cb;
SigmetRaw_Callback new_field_cb;
SigmetRaw_Callback del_field_cb;
SigmetRaw_Callback size_cb;
SigmetRaw_Callback set_field_cb;
SigmetRaw_Callback add_cb;
SigmetRaw_Callback sub_cb;
SigmetRaw_Callback mul_cb;
SigmetRaw_Callback div_cb;
SigmetRaw_Callback log10_cb;
SigmetRaw_Callback incr_time_cb;
SigmetRaw_Callback data_cb;
SigmetRaw_Callback bdata_cb;
SigmetRaw_Callback bin_outline_cb;
SigmetRaw_Callback radar_lon_cb;
SigmetRaw_Callback radar_lat_cb;
SigmetRaw_Callback shift_az_cb;
SigmetRaw_Callback outlines_cb;
SigmetRaw_Callback dorade_cb;

#endif
