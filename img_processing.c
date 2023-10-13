
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


Color _desired_col_at_dist(float dist, float max_dist)
{
    unsigned char luminance = (unsigned char)LERP(255.0f, 0.0f, MIN(dist / max_dist, 1.0f));
    return (Color){ .R = 255, .G = luminance, .B = luminance};
}

int _compare_match_strength(
    const Img_Format *format, const Color *rgb, float scan_radius, 
    int *i, int *res_str, int *res_i)
{
    if (rgb[*i].R == 255 && rgb[*i].G >= 253 && rgb[*i].B >= 254)
    {
        *i += 7;

        int 
            i_x = *i % format->width, 
            i_y = *i / format->width;

        float curr_str = 0.0f;
        unsigned int iter_count = 0;

        // Loop over all pixels within a 2 * scan_radius square of the given pixel,
        // skipping any pixel that is outside the bounds of the image or does not fall in the correct color range.
        for (int o_y = MAX(i_y - (int)scan_radius, 0); 
            o_y < MIN(i_y + (int)scan_radius, format->height); 
            o_y++)
        {
            for (int o_x = MAX(i_x - (int)scan_radius, 0); 
                o_x < MIN(i_x + (int)scan_radius, format->width); 
                o_x++)
            {
                // Skip every even pixel for better performance.
                if (iter_count++ % 2 == 0)
                    continue;

                int i_offset = o_x + o_y * format->width;

                // Early elimination of non-red pixels.
                if (rgb[i_offset].R + 3 <= rgb[i_offset].G || 
                    rgb[i_offset].R + 3 <= rgb[i_offset].B ||
                    rgb[i_offset].R <= 225)
                    continue;

                float center_dist_sqr = (float)(
                    (o_x - i_x) * (o_x - i_x) + 
                    (o_y - i_y) * (o_y - i_y));
                float center_dist = sqrtf(center_dist_sqr);

                // Gets the optimal color at a given distance from the center.
                // Pixels near the center should be white, while pixels far away should be red.
                Color desired_col = _desired_col_at_dist(center_dist, scan_radius);

                // Compares the optimal color with the actual color.
                // Increases the total strength by an amount inversely proportional to the deviation from desired_col.
                float col_offset = color_magnitude_sqr(rgb[i_offset], desired_col);
                curr_str += 100.0f / (col_offset / (log2f(col_offset + 2.0f)) + 1.0f);
            }
        }
        
        if (curr_str > (float)*res_str)
        {
            *res_str = (int)curr_str;
            *res_i = *i;
        }
    }

    return 0;
}

int scan_for_dot(const Img_Format *format, const Color *rgb, int *res_i, int *res_str)
{    
    int scan_radius = 20;

    *res_str = -1, 
    *res_i = -1;

    // Single-threaded
    timer_begin_measure(SCAN);
    {
        for (int i = 0; i < format->size; i++)
        {
            _compare_match_strength(
                format, rgb, scan_radius, 
                &i, res_str, res_i
            );
        }
    }
    timer_end_measure(SCAN);


    *res_str = -1, 
    *res_i = -1;
    
    // Multi-threaded:
    timer_begin_measure(T_SCAN);
    {
        const unsigned char thread_count = 4;

        int
            best_str[thread_count],
            best_i[thread_count];

        #pragma omp parallel num_threads(thread_count)
        {
            unsigned int 
                t_id = omp_get_thread_num(), 
                start_i = format->size * t_id / thread_count, 
                end_i = format->size * (t_id + 1) / thread_count;

            best_str[t_id] = -1,
            best_i[t_id] = -1;

            for (int i = start_i; i < end_i; i++)
            {
                _compare_match_strength(
                    format, rgb, scan_radius, 
                    &i, &(best_str[t_id]), &(best_i[t_id])
                );
            }
        }

        for (int i = 0; i < thread_count; i++)
        {
            if (best_str[i] > *res_str)
            {
                *res_str = best_str[i];
                *res_i = best_i[i];
            }
        }
    }
    timer_end_measure(T_SCAN);
    return 0;
}

int draw_circle(const Img_Format *format, Color *rgb, int x, int y, int r, int w)
{
    for (float angle = 0.0f; angle < PI / 2.0f; angle += 1.0f / ((float)(r + w) * PI))
    {
        float 
            ang_cos = cosf(angle),
            ang_sin = sinf(angle);

        for (int y_flip = -1; y_flip <= 1; y_flip += 2)
        {
            for (int x_flip = -1; x_flip <= 1; x_flip += 2)
            {
                for (int j = 0; j < w; j++)
                {
                    int x_offset = x + x_flip * (int)(ang_cos * (float)(r + j));
                    int y_offset = y + y_flip * (int)(ang_sin * (float)(r + j));

                    if (x_offset < 0 || x_offset >= format->width)
                        break;
                    if (y_offset < 0 || y_offset >= format->height)
                        break;

                    int i = x_offset + y_offset * format->width;

                    rgb[i].R = 255;
                    rgb[i].G = 0;
                    rgb[i].B = 0;
                }
            }
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