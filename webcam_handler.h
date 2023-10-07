#ifndef INCLUDE_WEBCAM_HANDLER_H
#define INCLUDE_WEBCAM_HANDLER_H

#include "color_data.h"

typedef struct Img_Format
{
    unsigned int width;
    unsigned int height;
    unsigned int size;
} Img_Format;

typedef struct Capture_Data
{
    int handle;
    unsigned char *img_mem[2];
} Capture_Data;


extern Img_Format img_format;
extern Capture_Data capture_data;


int webcam_init();

int next_frame();
int close_frame();

int mjpeg_to_rgb(RGB *rgb);
#endif
