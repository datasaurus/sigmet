/*
   -	BiSearch --
   -		Define functions that search arrays for intervals that
   -		contain given values.  See bisearch_lib (3).
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
   .	$Revision: 1.18 $ $Date: 2014/12/02 21:22:03 $
 */

#include "unix_defs.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "bisearch_lib.h"

/* Comparison functions for bsearch */
static int compar_f_asc(const void *, const void *);
static int compar_f_des(const void *, const void *);
static int compar_d_asc(const void *, const void *);
static int compar_d_des(const void *, const void *);

/*
   Put values that share intervals into linked lists for array data, which
   has n_data elements.  List indeces are placed into list, which must point
   to storage for n_data + n_bnds elements. Use DataType_1stIndex and
   DataType_NextIndex to retrieve elements from data which occupy a given
   interval.
 */

/*
   BiSearch_1stIndex and BiSearch_NextIndex use the return value as follows.

   Suppose the dat is the array of binned data, and bnds is the array of
   data boundaries into which data from dat are binned.
   Then traverse the linked list like this:
   Let m0 = heads[n].
   Let m1 = indeces[m0].
   Let m2 = indeces[m1].
   Let m3 = indeces[m2].
   And so on.

   Then bnds[n] <= dat[m0] < bnds[n+1].
   Then bnds[n] <= dat[m1] < bnds[n+1].
   Then bnds[n] <= dat[m2] < bnds[n+1].
   Then bnds[n] <= dat[m3] < bnds[n+1].
   And so on.

   A value in indeces of -1 indicates end of list for the interval for
   interval n.
 */

void BiSearch_DDataToList(double *data, int n_data, double *bnds, int n_bnds,
	int *lists)
{
    int n_intvls;
    int *heads, *indeces;
    int n, n_intvl, n_datum;
    int (*compar)(const void *, const void *);

    for (n = 0; n < n_data + n_bnds; n++) {
	lists[n] = -1;
    }
    if ( !data || n_data == 0 || !bnds || n_bnds == 0 ) {
	return;
    }
    n_intvls = n_bnds - 1;
    lists[0] = n_intvls;
    heads = lists + 1;
    indeces = lists + 1 + n_intvls;
    compar = (bnds[0] < bnds[1]) ? compar_d_asc : compar_d_des;

    /* Traverse data array in reverse so that indeces in lists will increase. */
    for (n_datum = n_data - 1; n_datum >= 0; n_datum--) {
	double *intvl_p;

	if ( isfinite(data[n_datum])
		&& (intvl_p = bsearch(data + n_datum, bnds, n_bnds - 1,
			sizeof(double), compar)) ) {
	    n_intvl = intvl_p - bnds;
	    if ( heads[n_intvl] == -1 ) {
		heads[n_intvl] = n_datum;
	    } else {
		indeces[n_datum] = heads[n_intvl];
		heads[n_intvl] = n_datum;
	    }
	}
    }
}
void BiSearch_FDataToList(float *data, int n_data, float *bnds, int n_bnds,
	int *lists)
{
    int n_intvls;
    int *heads, *indeces;
    int n, n_intvl, n_datum;
    int (*compar)(const void *, const void *);

    for (n = 0; n < n_data + n_bnds; n++) {
	lists[n] = -1;
    }
    if ( !data || n_data == 0 || !bnds || n_bnds == 0 ) {
	return;
    }
    n_intvls = n_bnds - 1;
    lists[0] = n_intvls;
    heads = lists + 1;
    indeces = lists + 1 + n_intvls;
    compar = (bnds[0] < bnds[1]) ? compar_f_asc : compar_f_des;

    /* Traverse data array in reverse so that indeces in lists will increase. */
    for (n_datum = n_data - 1; n_datum >= 0; n_datum--) {
	float *intvl_p;

	if ( isfinite(data[n_datum])
		&& (intvl_p = bsearch(data + n_datum, bnds, n_bnds - 1,
			sizeof(float), compar)) ) {
	    n_intvl = intvl_p - bnds;
	    if ( heads[n_intvl] == -1 ) {
		heads[n_intvl] = n_datum;
	    } else {
		indeces[n_datum] = heads[n_intvl];
		heads[n_intvl] = n_datum;
	    }
	}
    }
}

/* Comparison function for an array of floats sorted in ascending order */
static int compar_f_asc(const void *key_p, const void *val_p)
{
    float k = *(float *)key_p;
    const float *v = val_p;
    return (k < v[0]) ? -1 : (k >= v[1]) ? 1 : 0;
}

/* Comparison function for an array of floats sorted in descending order */
static int compar_f_des(const void *key_p, const void *val_p)
{
    float k = *(float *)key_p;
    const float *v = val_p;
    return (k > v[0]) ? -1 : (k <= v[1]) ? 1 : 0;
}

/* Comparison function for an array of doubles sorted in ascending order */
static int compar_d_asc(const void *key_p, const void *val_p)
{
    double k = *(double *)key_p;
    const double *v = val_p;
    return (k < v[0]) ? -1 : (k >= v[1]) ? 1 : 0;
}

/* Comparison function for an array of doubles sorted in descending order */
static int compar_d_des(const void *key_p, const void *val_p)
{
    double k = *(double *)key_p;
    const double *v = val_p;
    return (k > v[0]) ? -1 : (k <= v[1]) ? 1 : 0;
}

/*
   Return index of first element from data array that produced lists
   whose value occupies data range n_intvl, or -1 if no element from
   data occupies that interval.
 */

int BiSearch_1stIndex(int *lists, int n_intvl)
{
    return lists[n_intvl + 1];
}

/*
   Return index of next element from data array that produced lists
   whose value occupies same data range as data[nd], or -1 if no more
   data occupy the interval.
 */

int BiSearch_NextIndex(int *lists, int n_datum)
{
    return *(lists + 1 + lists[0] + n_datum);
}
