
//#define COMPARE_PERFORMANCE

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
#include <SDL2/SDL.h>


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
        col_v[format->size];

    int result = decode_jpeg_raw(
        mjpeg, mjpeg_size, 
        0, Y4M_CHROMA_422, 
        format->width, format->height, 
        col_y, col_u, col_v);

    if (result != 0)
        printf("Error in decode_jpeg_raw: %d\n",result);

    int yuv_size = format->width * format->height / 2;

#ifdef COMPARE_PERFORMANCE
    unsigned char 
        col_y_cpy[format->size],
        col_u_cpy[format->size],
        col_v_cpy[format->size];

    for (int i = 0; i < format->size; i++)
    {
        col_y_cpy[i] = col_y[i];
        col_u_cpy[i] = col_u[i];
        col_v_cpy[i] = col_v[i];
    }
#endif
    
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
    
#ifdef COMPARE_PERFORMANCE
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
#endif
    return 0;
}


int _compare_match_strength_hsv(const Img_Format *format, const HSV *hsv, 
    float scan_radius, int i, float *res_str, int *res_i)
{
    int to_skip = 0;

    if ((hsv[i].H <= 5.0f || hsv[i].H >= 355.0f) && hsv[i].S <= 0.01f && hsv[i].V >= 0.99f)
    {
        to_skip = 1;

        int 
            i_x = i % format->width, 
            i_y = i / format->width;

        float curr_str = 0.0f;

        for (int o_y = MAX(i_y - (int)scan_radius, 0); 
            o_y < MIN(i_y + (int)scan_radius, format->height); 
            o_y++)
        {
            for (int o_x = MAX(i_x - (int)scan_radius, 0); 
                o_x < MIN(i_x + (int)scan_radius, format->width); 
                o_x++)
            {
                // Decrease the amount of samples taken per pixel for performance reasons.
                if ((o_x - i_x) % 3 != 0 || 
                    (o_y - i_y) % 3 != 0)
                    continue;

                float center_dist_sqr = (float)(
                    (o_x - i_x) * (o_x - i_x) + 
                    (o_y - i_y) * (o_y - i_y));
                if (center_dist_sqr > scan_radius * scan_radius)
                    continue;
                float center_dist = sqrtf(center_dist_sqr);


                int i_offset = o_x + o_y * format->width;

                float 
                    tapered_dist = center_dist / (center_dist + scan_radius / 8.0f),
                    desired_s = powf(CLERP(0.0f, MAX(0.0f, LERP(-2.5f, 1.0f, tapered_dist)) * 0.85f, powf(tapered_dist, 0.1f)), 1.5f),
                    desired_v = LERP(1.0f, 0.9f, tapered_dist);

                float 
                    curr_h_offset =  1.0f - CLAMP(fabsf((fmodf(hsv[i_offset].H + 180.0f, 360.0f) - 180.0f) / 120.0f) + CLERP(0.5f, 0.0f, hsv[i_offset].S * 2.0f), 0.0f, 1.0f),
                    curr_s_offset = fabsf((1.0f - desired_s) - hsv[i_offset].S),
                    curr_v_offset = CLERP(1.0f, 0.0f, (desired_v - hsv[i_offset].V) / desired_v);
                
                curr_str += curr_h_offset * curr_s_offset * curr_v_offset;
            }
        }
        
        if (curr_str > *res_str)
        {
            *res_str = curr_str;
            *res_i = i;
        }
    }

    return to_skip;
}

int _draw_hsv(const Img_Format *format, Color *rgb, const HSV *hsv, int m_x, int m_y)
{
    float scan_radius = format->height / 28;

    const unsigned char thread_count = 4;
    #pragma omp parallel num_threads(thread_count)
    {
        unsigned int 
            t_id = omp_get_thread_num(), 
            start_i = format->size * t_id / thread_count, 
            end_i = format->size * (t_id + 1) / thread_count;

        for (int i = start_i; i < end_i; i++)
        {
            int 
                i_x = i % format->width, 
                i_y = i / format->width;

            if (i_x == m_x && i_y == m_y)
            {
                float tot_str = 0.0f;

                int 
                    i_x = i % format->width, 
                    i_y = i / format->width;

                for (int o_y = MAX(i_y - (int)scan_radius, 0); 
                    o_y < MIN(i_y + (int)scan_radius, format->height); 
                    o_y++)
                {
                    for (int o_x = MAX(i_x - (int)scan_radius, 0); 
                        o_x < MIN(i_x + (int)scan_radius, format->width); 
                        o_x++)
                    {
                        // Decrease the amount of samples taken per pixel for performance reasons.
                        if ((o_x - i_x) % 3 == 0 || 
                            (o_y - i_y) % 3 == 0)
                            continue;

                        float center_dist_sqr = (float)(
                            (o_x - i_x) * (o_x - i_x) + 
                            (o_y - i_y) * (o_y - i_y));
                        if (center_dist_sqr > scan_radius * scan_radius)
                            continue;
                        float center_dist = sqrtf(center_dist_sqr);

                        int i_offset = o_x + o_y * format->width;

                        float 
                            tapered_dist = center_dist / (center_dist + scan_radius / 8.0f),
                            desired_s = powf(CLERP(0.0f, MAX(0.0f, LERP(-2.5f, 1.0f, tapered_dist)) * 0.85f, powf(tapered_dist, 0.1f)), 1.5f),
                            desired_v = LERP(1.0f, 0.9f, tapered_dist);

                        float 
                            curr_h_offset =  1.0f - CLAMP(fabsf((fmodf(hsv[i_offset].H + 180.0f, 360.0f) - 180.0f) / 120.0f) + CLERP(0.5f, 0.0f, hsv[i_offset].S * 2.0f), 0.0f, 1.0f),
                            curr_s_offset = fabsf((1.0f - desired_s) - hsv[i_offset].S),
                            curr_v_offset = CLERP(1.0f, 0.0f, (desired_v - hsv[i_offset].V) / desired_v);

                        float curr_str = 1.0f / (1.0f + curr_h_offset + curr_s_offset + curr_v_offset);
                        //float curr_str = curr_h_offset*curr_h_offset * curr_s_offset*curr_s_offset * curr_v_offset*curr_v_offset;
                        tot_str += curr_h_offset * curr_s_offset * curr_v_offset;
                        //tot_str += curr_str;
                        
                        rgb[i_offset] = (Color){
                            .R = MAX(0.0f, MIN(curr_h_offset, 1.0f)) * 255.0f, 
                            .G = MAX(0.0f, MIN(curr_s_offset, 1.0f)) * 255.0f, 
                            .B = MAX(0.0f, MIN(curr_v_offset, 1.0f)) * 255.0f
                        };
                    }
                }

                printf("(%.2f, %.2f, %.2f)\n", hsv[i].H, hsv[i].S, hsv[i].V);
                printf("str: %f\n\n", tot_str);
            }
        }
    }
    return 0;
}


int _scan_for_dot_hsv(const Img_Format *format, const HSV *hsv, int *res_i, float *res_str)
{    
    float scan_radius = format->height / 25;

#ifdef COMPARE_PERFORMANCE
    // Single-threaded:
    {
        timer_begin_measure(SCAN);
        *res_str = -1.0, 
        *res_i = -1;

        for (int i = 0; i < format->size; i++)
        {
            i += _compare_match_strength_hsv(
                format, hsv, scan_radius, 
                i, res_str, res_i
            );
        }
        timer_end_measure(SCAN);
    }
#endif
    
    // Multi-threaded:
    {
        timer_begin_measure(T_SCAN);

        *res_str = -1.0f, 
        *res_i = -1;

        const unsigned char thread_count = 4;

        float best_str[thread_count];
        int best_i[thread_count];

        #pragma omp parallel num_threads(thread_count)
        {
            unsigned int 
                t_id = omp_get_thread_num(), 
                start_i = format->size * t_id / thread_count, 
                end_i = format->size * (t_id + 1) / thread_count;

            best_str[t_id] = -1.0f,
            best_i[t_id] = -1;

            for (int i = start_i; i < end_i; i++)
            {
                i += _compare_match_strength_hsv(
                    format, hsv, scan_radius, 
                    i, &(best_str[t_id]), &(best_i[t_id])
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

        timer_end_measure(T_SCAN);
    }
    
    return 0;
}

void _visualize_pixel_strengths_hsv(const Img_Format *format, Color *rgb, HSV *hsv)
{
    float scan_radius = format->height / 25;

    const unsigned char thread_count = 4;

    #pragma omp parallel num_threads(thread_count)
    {
        unsigned int 
            t_id = omp_get_thread_num(), 
            start_i = format->size * t_id / thread_count, 
            end_i = format->size * (t_id + 1) / thread_count;

        for (int i = start_i; i < end_i; i++)
        {
            float str = 0.0f;
            int index = -1;

            int skip = _compare_match_strength_hsv(
                format, hsv, scan_radius, 
                i, &str, &index
            );

            rgb[i] = (Color){
                .R = (unsigned char)(str / (str + 10.0f)), 
                .G = (unsigned char)(str / (str + 10.0f)), 
                .B = (unsigned char)(str / (str + 10.0f)) 
            };
            i += skip;
        }
    }
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

                    rgb[i].R = 0;
                    rgb[i].G = 255;
                    rgb[i].B = 0;
                }
            }
        }
    }

    return 0;
}

int apply_img_effects(const Img_Format *format, Color *rgb)
{
    HSV hsv[format->size];
    for (int i = 0; i < format->size; i++)
    {
        hsv[i] = rgb_to_hsv(rgb[i]);
    }
    
    /*int x, y;
    SDL_GetMouseState(&x, &y);
    _draw_hsv(format, rgb, hsv, x, y);*/

    //_visualize_pixel_strengths_hsv(format, rgb, hsv);

    int result = 0;

    float r_str;
    int r_i;
    result = _scan_for_dot_hsv(format, hsv, &r_i, &r_str);

    if (r_i != -1 && r_str > 8.0f)
    {
        printf("%f\n", r_str);
        int 
            x = r_i % format->width, 
            y = r_i / format->width;

        for (int o_y = MAX(y - 1, 0); o_y < MIN(y + 1, format->height); o_y++)
        {
            for (int o_x = MAX(x - 1, 0); o_x < MIN(x + 1, format->width); o_x++)
            {
                int i_offset = o_x + o_y * format->width;

                rgb[i_offset] = (Color){0, 0, 0};
            }
        }
        
        result = draw_circle(format, rgb, x, y, 30, (int)(r_str / 10.0f) + 1);
        if (result  != 0)
            return -1;
    }

    /*int 
        x = format->width / 2, 
        y = format->height / 2,
        i = x + y * format->width;

    draw_circle(format, rgb, x, y, 10, 1);
    printf("\n(%d, %d, %d)\n\n", rgb[i].R, rgb[i].G, rgb[i].B);
    printf("Dist white: %f\n", sqrt(color_magnitude_sqr(rgb[i], (Color){.R = 255, .G = 255, .B = 255})));
    printf("Dist black: %f\n", sqrt(color_magnitude_sqr(rgb[i], (Color){.R = 0, .G = 0, .B = 0})));
    printf("Dist red: %f\n", sqrt(color_magnitude_sqr(rgb[i], (Color){.R = 255, .G = 0, .B = 0})));*/

    return 0;
}