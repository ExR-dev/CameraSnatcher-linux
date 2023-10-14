#ifndef INCLUDE_IMG_PROCESSING_H
#define INCLUDE_IMG_PROCESSING_H

#include "img_data.h"
#include "aabb.h"


int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Fmt *format, RGB *rgb);

int draw_circle(const Img_Fmt *format, RGB *rgb, Vec2 pos, int r, int w, RGB col);
int draw_box(const Img_Fmt *format, RGB *rgb, AABB box, int w, RGB col);

int find_laser_dot(const Img_Fmt *fmt, const RGB *rgb, Vec2 *pos, float *confidence);
int apply_img_effects(const Img_Fmt *format, RGB *rgb);
#endif
