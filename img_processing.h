#ifndef INCLUDE_IMG_PROCESSING_H
#define INCLUDE_IMG_PROCESSING_H

#include "img_data.h"


int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Format *format, Color *rgb);

int scan_for_dot(const Img_Format *format, const Color *rgb, int *result_i, int* result_str);

int draw_circle(const Img_Format *format, Color *rgb, int x, int y, int r, int w);

int apply_img_effects(const Img_Format *format, Color *rgb);

#endif
