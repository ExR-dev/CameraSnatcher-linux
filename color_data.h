#ifndef INCLUDE_COLOR_DATA_H
#define INCLUDE_COLOR_DATA_H

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))
#define CLAMP(x, a, b) (MAX(a, MIN(x, b)))
#define LERP(a, b, t) (a * (1.0 - t) + (b * t))


typedef struct RGB
{
    unsigned char R;
    unsigned char G;
    unsigned char B;
} RGB;

typedef struct HSV
{
    float H; // Hue (0-360)
    float S; // Saturation (0-100)
    float V; // Value (0-100)
} HSV;


HSV rgb_to_hsv(RGB rgb);

RGB hsv_to_rgb(HSV hsv);

#endif
