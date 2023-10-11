#ifndef INCLUDE_WEBCAM_HANDLER_H
#define INCLUDE_WEBCAM_HANDLER_H

#include "img_data.h"


int webcam_init(const Img_Format *format);

int next_frame();
int get_frame(unsigned char **mjpeg, unsigned int *mjpeg_size);
int close_frame();

#endif
