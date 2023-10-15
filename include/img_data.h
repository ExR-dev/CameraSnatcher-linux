#ifndef INCLUDE_IMG_DATA_H
#define INCLUDE_IMG_DATA_H


#include <stdbool.h>


#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

#define LERP(a, b, t) (a * (1.0 - t) + (b * t))
#define CLAMP(x, a, b) (MAX(a, MIN(x, b)))
#define CLERP(a, b, t) (a * (1.0 - CLAMP(t, 0.0, 1.0)) + (b * CLAMP(t, 0.0, 1.0)))

#define PI 3.14159265358979323846


typedef struct Img_Fmt
{
    const unsigned int width, height, size;

    float 
        visualize, greyscale, verbose,
        filter_hue, filter_sat, filter_val,
        scan_rad, skip_len, sample_step,
        dot_threshold, alt_weights,
        h_str, s_str, v_str;

} Img_Fmt;

typedef struct RGB
{
    unsigned char R;
    unsigned char G;
    unsigned char B;
} RGB;

typedef struct HSV
{
    float H; // Hue (0-360)
    float S; // Saturation (0-1)
    float V; // Value (0-1)
} HSV;


HSV rgb_to_hsv(RGB rgb);

RGB hsv_to_rgb(HSV hsv);

float color_magnitude_sqr(RGB col1, RGB col2);

float color_distance(RGB col1, RGB col2);

#endif
