#ifndef INCLUDE_IMG_DATA_H
#define INCLUDE_IMG_DATA_H

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

#define CLAMP(x, a, b) (MAX(a, MIN(x, b)))
#define LERP(a, b, t) (a * (1.0 - t) + (b * t))

#define VEC3_SQR_MAG(u, v) (u[])

#define PI 3.14159265358979323846


typedef struct Img_Format
{
    unsigned int width;
    unsigned int height;
    unsigned int size;
} Img_Format;

typedef struct Color
{
    unsigned char R;
    unsigned char G;
    unsigned char B;
} Color;

typedef struct HSV
{
    float H; // Hue (0-360)
    float S; // Saturation (0-100)
    float V; // Value (0-100)
} HSV;


HSV rgb_to_hsv(Color rgb);

Color hsv_to_rgb(HSV hsv);

float color_magnitude_sqr(Color col1, Color col2);

float color_distance(Color col1, Color col2);

#endif
