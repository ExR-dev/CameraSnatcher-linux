
#define USE_THREADS

#include "img_processing.h"

#include "timer.h"
#include "img_data.h"
#include "jpegutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <omp.h>
#include <pthread.h>


void _yuyv_to_rgb(unsigned char y1, unsigned char u, unsigned char y2, unsigned char v, Color *rgb)
{
    int c = y1 - 16;
    int d = u - 128;
    int e = v - 128;

    for (unsigned char i = 0; i < 2; i++)
    {
        rgb[i].R = MAX(0, MIN((298 * c + 409 * e + 128) >> 8, 255));
        rgb[i].G = MAX(0, MIN((298 * c - 100 * d - 208 * e + 128) >> 8, 255));
        rgb[i].B = MAX(0, MIN((298 * c + 516 * d + 128) >> 8, 255));

        c = y2 - 16;
    }
}

int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Format *format, Color *rgb)
{
    unsigned char 
        col_y[format->size],
        col_u[format->size],
        col_v[format->size],
        col_y_cpy[format->size],
        col_u_cpy[format->size],
        col_v_cpy[format->size];

    int result = decode_jpeg_raw(
        mjpeg, mjpeg_size, 
        0, Y4M_CHROMA_422, 
        format->width, format->height, 
        col_y, col_u, col_v);

    if (result != 0)
        printf("Error in decode_jpeg_raw: %d\n",result);

    int yuv_size = format->width * format->height / 2;

    for (int i = 0; i < format->size; i++)
    {
        col_y_cpy[i] = col_y[i];
        col_u_cpy[i] = col_u[i];
        col_v_cpy[i] = col_v[i];
    }
    
    // Multi-threaded:
    timer_begin_measure(T_CONVERSION);
    {
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
    }
    timer_end_measure(T_CONVERSION);
    
    // Single-threaded:
    timer_begin_measure(CONVERSION);
    {
        for (int i = 0; i < yuv_size; i++)
        {
            unsigned char 
                y1 = col_y_cpy[i * 2],
                u = col_u_cpy[i],
                y2 = col_y_cpy[i * 2 + 1],
                v = col_v_cpy[i];

                _yuyv_to_rgb(y1, u, y2, v, &rgb[i * 2]);
        }
    }
    timer_end_measure(CONVERSION);
    return 0;
}

unsigned char get_avg_luminance(const Img_Format *format, const Color *rgb, unsigned int pix_skip)
{
    unsigned int pix_count = 1;
    unsigned int color_sum = 127;

    for (int y = 0; y < format->height; y += 1 + pix_skip)
    {
        for (int x = 0; x < format->width; x += 1 + pix_skip)
        {
            int i = x + y * format->width;

            color_sum += (rgb[i].R + rgb[i].G + rgb[i].B) / 3;
            pix_count++;
        }
    }
    
    return color_sum / pix_count;
}


Color _desired_col_at_dist(float dist, float max_dist)
{
    unsigned char luminance = (unsigned char)LERP(255.0f, 0.0f, MIN(dist / max_dist, 1.0f));
    return (Color){ .R = 255, .G = luminance, .B = luminance};
}

int scan_for_dot(const Img_Format *format, const Color *rgb, int *result_i, int *result_str)
{    
    int dot_radius = 20;
    float max_dist = (float)dot_radius;

    *result_str = -1, 
    *result_i = -1;

    // Single-threaded
    {
        timer_begin_measure(SCAN);

        for (int i = 0; i < format->size; i++)
        {
            if (rgb[i].R == 255 && rgb[i].G >= 253 && rgb[i].B >= 254)
            {
                i += 7;

                int 
                    i_x = i % format->width, 
                    i_y = i / format->width;

                float curr_match_strength = 0.0f;
                unsigned int iteration_count = 0;

                // Loop over all pixels within a 2 * dot_radius square of the current pixel,
                // skipping any pixels that fall outside the image.
                for (int o_y = MAX(i_y - (int)dot_radius, 0); 
                    o_y < MIN(i_y + (int)dot_radius, format->height); 
                    o_y++)
                {
                    for (int o_x = MAX(i_x - (int)dot_radius, 0); 
                        o_x < MIN(i_x + (int)dot_radius, format->width); 
                        o_x++)
                    {
                        if (iteration_count++ % 2 == 0)
                            continue;

                        int i_offset = o_x + o_y * format->width;
                        if (rgb[i_offset].R + 3 <= rgb[i_offset].G || 
                            rgb[i_offset].R + 3 <= rgb[i_offset].B ||
                            rgb[i_offset].R <= 225)
                            continue;

                        float center_dist_sqr = (float)((o_x - i_x) * (o_x - i_x) + (o_y - i_y) * (o_y - i_y));
                        /*if (center_dist_sqr > dot_radius_sqr)
                            continue;*/
                        float center_dist = sqrtf(center_dist_sqr);

                        Color desired_col = _desired_col_at_dist(center_dist, max_dist);
                        float col_offset = color_magnitude_sqr(rgb[i_offset], desired_col);

                        curr_match_strength += 100.0f / (col_offset / (log2f(col_offset + 2.0f)) + 1.0f);
                    }
                }
                
                if (curr_match_strength > (float)*result_str)
                {
                    *result_str = (int)curr_match_strength;
                    *result_i = i;
                }
            }
        }
        timer_end_measure(SCAN);
    }
    
    // Multi-threaded:
    {
        timer_begin_measure(T_SCAN);

        const unsigned char thread_count = 4;

        int
            best_match_strength[thread_count],
            best_match_index[thread_count];

        #pragma omp parallel num_threads(thread_count)
        {
            unsigned int 
                t_id = omp_get_thread_num(), 
                start_i = format->size * t_id / thread_count, 
                end_i = format->size * (t_id + 1) / thread_count;

            best_match_strength[t_id] = -1,
            best_match_index[t_id] = -1;

            for (int i = start_i; i < end_i; i++)
            {
                if (rgb[i].R == 255 && rgb[i].G >= 253 && rgb[i].B >= 254)
                {
                    i += 7;

                    int 
                        i_x = i % format->width, 
                        i_y = i / format->width;

                    float curr_match_strength = 0.0f;
                    unsigned int iteration_count = 0;

                    // Loop over all pixels within a 2 * dot_radius square of the current pixel,
                    // skipping any pixels that fall outside the image.
                    for (int o_y = MAX(i_y - (int)dot_radius, 0); 
                        o_y < MIN(i_y + (int)dot_radius, format->height); 
                        o_y++)
                    {
                        for (int o_x = MAX(i_x - (int)dot_radius, 0); 
                            o_x < MIN(i_x + (int)dot_radius, format->width); 
                            o_x++)
                        {
                            if (iteration_count++ % 2 == 0)
                                continue;

                            int i_offset = o_x + o_y * format->width;
                            if (rgb[i_offset].R + 3 <= rgb[i_offset].G || 
                                rgb[i_offset].R + 3 <= rgb[i_offset].B ||
                                rgb[i_offset].R <= 225)
                                continue;

                            float center_dist_sqr = (float)((o_x - i_x) * (o_x - i_x) + (o_y - i_y) * (o_y - i_y));
                            /*if (center_dist_sqr > dot_radius_sqr)
                                continue;*/
                            float center_dist = sqrtf(center_dist_sqr);

                            Color desired_col = _desired_col_at_dist(center_dist, max_dist);
                            float col_offset = color_magnitude_sqr(rgb[i_offset], desired_col);

                            curr_match_strength += 100.0f / (col_offset / (log2f(col_offset + 2.0f)) + 1.0f);
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

        timer_end_measure(T_SCAN);
    }
    return 0;
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

    int r_i, r_str;
    result = scan_for_dot(format, rgb, &r_i, &r_str);

    if (r_i != -1 && r_str > 300)
    {
        printf("%d\n", r_str);
        int 
            x = r_i % format->width, 
            y = r_i / format->width;

        /*for (int o_y = MAX(y - 3, 0); o_y < MIN(y + 3, format->height); o_y++)
        {
            for (int o_x = MAX(x - 3, 0); o_x < MIN(x + 3, format->width); o_x++)
            {
                int i_offset = o_x + o_y * format->width;

                rgb[i_offset] = (Color){0, 0, 255};
            }
        }*/
        
        result = draw_circle(format, rgb, x, y, 15, (r_str / 50) + 1);
        //esult = draw_circle(format, rgb, x, y, 25, log2(r_str / 100 + 2));
        if (result  != 0)
            return -1;
    }


    return 0;
}