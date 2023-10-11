
#define USE_THREADS

#include "img_data.h"

#include <math.h>


HSV rgb_to_hsv(Color rgb) 
{
    float 
        r = rgb.R / 255.0f, 
        g = rgb.G / 255.0f, 
        b = rgb.B / 255.0f;

    float max = MAX(r, MAX(g, b));
    float min = MIN(r, MIN(g, b));
    float diff = max - min;

    float h = 0.0, s, v;

    if (max == min) 	h = 0.0f;
    else if (max == r)	h = fmodf((60.0f * ((g - b) / diff) + 360.0f), 360.0f);
    else if (max == g)	h = fmodf((60.0f * ((b - r) / diff) + 120.0f), 360.0f);
    else if (max == b)	h = fmodf((60.0f * ((r - g) / diff) + 240.0f), 360.0f);
	else				h = 0.0f;

    s = (max == 0.0f) ? (0.0f) : ((diff / max) * 100.0f);
    v = max * 100.0f;

    return (HSV){h, s, v};
}

Color hsv_to_rgb(HSV hsv)
{
    float r = 0.0f, g = 0.0f, b = 0.0f;

	if (hsv.S == 0.0f)
	{
		r = hsv.V;
		g = hsv.V;
		b = hsv.V;
	}
	else
	{
		int i;
		float f, p, q, t;

		if (hsv.H == 360.0f)
			hsv.H = 0.0f;
		else
			hsv.H = hsv.H / 60.0f;

		i = (int)trunc(hsv.H);
		f = hsv.H - i;

		p = hsv.V * (1.0f - hsv.S);
		q = hsv.V * (1.0f - (hsv.S * f));
		t = hsv.V * (1.0f - (hsv.S * (1.0f - f)));

		switch (i)
		{
		case 0:
			r = hsv.V;
			g = t;
			b = p;
			break;

		case 1:
			r = q;
			g = hsv.V;
			b = p;
			break;

		case 2:
			r = p;
			g = hsv.V;
			b = t;
			break;

		case 3:
			r = p;
			g = q;
			b = hsv.V;
			break;

		case 4:
			r = t;
			g = p;
			b = hsv.V;
			break;

		default:
			r = hsv.V;
			g = p;
			b = q;
			break;
		}

	}

	Color rgb = {
        .R = r * 255,
        .G = g * 255,
        .B = b * 255
    };

	return rgb;
}

float color_magnitude_sqr(Color col1, Color col2)
{
	return (col2.R - col1.R) * (col2.R - col1.R) 
		 + (col2.G - col1.G) * (col2.G - col1.G) 
		 + (col2.B - col1.B) * (col2.B - col1.B);
}
