
#define USE_THREADS

#include "img_processing.h"
#include "img_data.h"
#include "jpegutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

//#include <unistd.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <sys/ioctl.h>
//#include <sys/stat.h>
//#include <sys/mman.h>

#include <omp.h>
///#include <pthread.h>


void _yuyv_to_rgb(unsigned char y1, unsigned char u, unsigned char y2, unsigned char v, Color *rgb)
{
    int c = y1 - 16;
    int d = u - 128;
    int e = v - 128;

    rgb[0].R = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));
    rgb[0].G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
    rgb[0].B = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));

    c = y2 - 16;

    rgb[1].R = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));
    rgb[1].G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
    rgb[1].B = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));
}

int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Format *format, Color *rgb)
{
    unsigned char 
        col_y[format->size],
        col_u[format->size],
        col_v[format->size];

    int result = decode_jpeg_raw(
        mjpeg, mjpeg_size, 
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

#else // single-threaded
    
    for (int i = 0; i < yuv_size; i++)
    {
        unsigned char 
            y1 = col_y[i * 2],
            u = col_u[i],
            y2 = col_y[i * 2 + 1],
            v = col_v[i];

        _yuyv_to_rgb(y1, u, v, &rgb[i * 2]);
    }

#endif
    return 0;
}


inline Color _desired_col_at_dist(float dist, float max_dist)
{
    return (Color){255, LERP(255, 0, dist / max_dist), LERP(255, 0, dist / max_dist)};
}

int scan_for_dot(const Img_Format *format, const Color *rgb, int *result_i, int* result_str)
{
    int dot_radius = 15;
    float max_dist = sqrtf(dot_radius * dot_radius);

    *result_str = -1, 
    *result_i = -1;

#ifdef USE_THREADS

    const unsigned char thread_count = 3;

    int
        *best_match_strength = malloc(sizeof(int) * thread_count),
        *best_match_index = malloc(sizeof(int) * thread_count);

    #pragma omp parallel num_threads(thread_count)
    {
        int 
            t_id = omp_get_thread_num(), 
            start_i = format->size * t_id / thread_count, 
            end_i = format->size * (t_id + 1) / thread_count;

        best_match_strength[t_id] = -1,
        best_match_index[t_id] = -1;

        for (int i = start_i; i < end_i; i++)
        {
            if (rgb[i].R >= 250 && rgb[i].G >= 250 && rgb[i].B >= 250)
            {
                int 
                    i_x = i % format->width, 
                    i_y = (i - i_x) / format->height;

                float curr_match_strength = 0.0f;

                // Loop over all pixels within a 2 * dot_radius square of the current pixel,
                // skipping any pixels that fall outside the image.
                for (int o_y = MAX(i_y - dot_radius, 0); o_y < MIN(i_y + dot_radius, format->height); o_y++)
                {
                    for (int o_x = MAX(i_x - dot_radius, 0); o_x < MIN(i_x + dot_radius, format->width); o_x++)
                    {
                        int i_offset = o_x + o_y * format->width;

                        Color desired_col = _desired_col_at_dist(
                            sqrtf((float)(
                                (o_x - i_x) * (o_x - i_x)
                              + (o_y - i_y) * (o_y - i_y))), 
                            max_dist);

                        float col_offset = color_magnitude_sqr(rgb[i_offset], desired_col);

                        curr_match_strength += 10.0f / log2f(col_offset);
                    }
                }
                
                if (curr_match_strength > (float)best_match_strength[t_id])
                {
                    best_match_strength[t_id] = (int)curr_match_strength;
                    best_match_index[t_id] = i;
                }
            }
        }
    }

    for (int i = 0; i < thread_count; i++)
    {
        if (best_match_strength[i] > *result_str)
        {
            *result_str = best_match_strength[i];
            *result_i = best_match_index[i];
        }
    }
    
    free(best_match_strength);
    free(best_match_index);

#else // single-threaded

    for (int i = 0; i < format->size; i++)
    {
        if (rgb[i].R >= 250 && rgb[i].G >= 250 && rgb[i].B >= 250)
        {
            int 
                i_x = i % format->width, 
                i_y = (i - i_x) / format->height;

            float curr_match_strength = 0.0f;

            // Loop over all pixels within a 2 * dot_radius square of the current pixel,
            // skipping any pixels that fall outside the image.
            for (int o_y = MAX(i_y - dot_radius, 0); o_y < MIN(i_y + dot_radius, format->height); o_y++)
            {
                for (int o_x = MAX(i_x - dot_radius, 0); o_x < MIN(i_x + dot_radius, format->width); o_x++)
                {
                    int i_offset = o_x + o_y * format->width;

                    Color desired_col = _desired_col_at_dist(
                        sqrtf((float)(
                            (o_x - i_x) * (o_x - i_x)
                            + (o_y - i_y) * (o_y - i_y))), 
                        max_dist);

                    float col_offset = color_magnitude_sqr(rgb[i_offset], desired_col);

                    curr_match_strength += 10.0f / log2f(col_offset);
                }
            }
            
            if (curr_match_strength > (float)*result_str)
            {
                *result_str = (int)curr_match_strength;
                *result_i = i;
            }
        }
    }

#endif
    return 1;
}

int draw_circle(const Img_Format *format, Color *rgb, int x, int y, int r, int w)
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

    result = draw_circle(format, rgb, 400, 250, 200, 5);
    if (result  != 0)
        return -1;

    return 0;
}