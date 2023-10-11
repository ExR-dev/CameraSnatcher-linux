
#define USE_THREADS

#include "img_processing.h"
#include "img_data.h"
#include "jpegutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/videodev2.h>
#include <omp.h>
#include <pthread.h>


void _yuyv_to_rgb(unsigned char y1, unsigned char u, unsigned char y2, unsigned char v, Color *_rgb)
{
    int c = y1 - 16;
    int d = u - 128;
    int e = v - 128;

    _rgb[0].R = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));
    _rgb[0].G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
    _rgb[0].B = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));

    c = y2 - 16;

    _rgb[1].R = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));
    _rgb[1].G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
    _rgb[1].B = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));
}

int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Format *format, Color *rgb)
{
    unsigned char 
        col_y[format->size],
        col_u[format->size],
        col_v[format->size];

    int result = decode_jpeg_raw(
        mjpeg, 
        mjpeg_size, 
        0, Y4M_CHROMA_422, 
        format->width, format->height, 
        col_y, col_u, col_v);

    if (result != 0)
        printf("Error in decode_jpeg_raw: %d\n",result);

    int yuv_size = format->width * format->height / 2;

#ifdef USE_THREADS
    const unsigned char thread_count = 3;
    #pragma omp parallel num_threads(thread_count)
    {
        int 
            t_id = omp_get_thread_num(), 
            start_i = yuv_size * t_id / thread_count, 
            end_i = yuv_size * (t_id + 1) / thread_count;

        for (int i = start_i; i < end_i; i++)
        {
            unsigned char 
                y1 = col_y[i * 2],
                u = col_u[i],
                y2 = col_y[i * 2 + 1],
                v = col_v[i];

            _yuyv_to_rgb(y1, u, y2, v, &rgb[i * 2]);
        }
    }
#else
    for (int i = 0; i < yuv_size; i++)
    {
        unsigned char 
            y1 = col_y[i * 2],
            u = col_u[i],
            y2 = col_y[i * 2 + 1],
            v = col_v[i];

        _yuyv_to_rgb(y1, u, v, &rgb[i * 2]);
        _yuyv_to_rgb(y2, u, v, &rgb[i * 2 + 1]);
    }
#endif

    return 0;
}


int col_manip_add_circle(const Img_Format *format, Color *rgb, int x, int y, int r, int w)
{
    for (float angle = 0.0f; angle < PI * 2.0f; angle += 1.0f / ((float)(r + w) * PI))
    {
        for (int j = 0; j < w; j++)
        {
            int x_offset = x + (int)(cosf(angle) * (float)(r + j));
            int y_offset = y + (int)(sinf(angle) * (float)(r + j));

            if (x_offset < 0 || x_offset >= format->width)
                continue;
            if (y_offset < 0 || y_offset >= format->height)
                continue;

            int i = x_offset + y_offset * format->width;

            rgb[i].R = 255;
            rgb[i].G = 0;
            rgb[i].B = 0;
        }
    }

    return 0;
}

int apply_img_effects(const Img_Format *format, Color *rgb)
{
    int result = 0;

    result = col_manip_add_circle(format, rgb, 400, 250, 200, 5);
    if (result  != 0)
        return -1;

    return 0;
}