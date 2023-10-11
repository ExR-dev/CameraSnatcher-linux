#ifndef INCLUDE_IMG_PROCESSING_H
#define INCLUDE_IMG_PROCESSING_H

#include "img_data.h"


int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Format *format, Color *rgb);

int apply_img_effects(const Img_Format *format, Color *rgb);
#endif
