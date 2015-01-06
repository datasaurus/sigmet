/*
   -	From:
   -		http://www.cs.rit.edu/~ncs/color/t_convert.html
   -	by:
   -		Nan C. Schaller
   -		Professor Emerita, Computer Science Department
   -		Rochester Institute of Technology
   -	
   -	RGB to HSV & HSV to RGB
   -	
   -	The Hue/Saturation/Value model was created by A. R. Smith in 1978. It
   -	is based on such intuitive color characteristics as tint, shade and
   -	tone (or family, purety and intensity). The coordinate system is
   -	cylindrical, and the colors are defined inside a hexcone. The hue value
   -	H runs from 0 to 360º. The saturation S is the degree of strength or
   -	purity and is from 0 to 1. Purity is how much white is added to the
   -	color, so S=1 makes the purest color (no white). Brightness V also
   -	ranges from 0 to 1, where 0 is the black.
   -	
   -	There is no transformation matrix for RGB/HSV conversion, but the
   -	algorithm follows:
 */

/*
   r,g,b values are from 0 to 1
   h = [0,360], s = [0,1], v = [0,1]
   if s == 0, then h = -1 (undefined)
 */

#include <math.h>
#include "hsv_lib.h"

void RGBtoHSV(double r, double g, double b, double *h, double *s, double *v)
{
    double min, max, delta;

    min = (r < g && r < b) ? r : (g < r && g < b) ? g : b;
    max = (r > g && r > b) ? r : (g > r && g > b) ? g : b;
    *v = max;				/* v */
    delta = max - min;
    if ( max != 0 ) {
	*s = delta / max;		/* s */
    } else {
	/* r = g = b = 0 */             /* s = 0, v is undefined */
	*s = 0;
	*h = -1;
	return;
    }
    if ( r == max ) {
	*h = (g - b) / delta;		/* between yellow & magenta */
    } else if ( g == max ) {
	*h = 2 + (b - r) / delta;	/* between cyan & yellow */
    } else {
	*h = 4 + (r - g) / delta;	/* between magenta & cyan */
    }
    *h *= 60;				/* degrees */
    if ( *h < 0 ) {
	*h += 360;
    }
}

void HSVtoRGB(double *r, double *g, double *b, double h, double s, double v)
{
    double i, f, p, q, t;

    h = fmod(h, 360.0);
    if ( h < 0.0 ) {
	h += 360.0;
    }
    if ( s == 0 ) {
	/* achromatic (grey) */
	*r = *g = *b = v;
	return;
    }
    h /= 60;				/* sector 0 to 5 */
    f = modf(h, &i);
    p = v * (1 - s);
    q = v * (1 - s * f);
    t = v * (1 - s * (1 - f));
    switch((int)i) {
	case 0:
	    *r = v;
	    *g = t;
	    *b = p;
	    break;
	case 1:
	    *r = q;
	    *g = v;
	    *b = p;
	    break;
	case 2:
	    *r = p;
	    *g = v;
	    *b = t;
	    break;
	case 3:
	    *r = p;
	    *g = q;
	    *b = v;
	    break;
	case 4:
	    *r = t;
	    *g = p;
	    *b = v;
	    break;
	default:			/* case 5: */
	    *r = v;
	    *g = p;
	    *b = q;
	    break;
    }
}
