
//#define COMPARE_PERFORMANCE

#include "include/img_processing.h"

#include "include/timer.h"
#include "include/img_data.h"
#include "include/jpegutils.h"
#include "include/aabb.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include <omp.h>
#include <SDL2/SDL.h>


void _yuyv_to_rgb(unsigned char y1, unsigned char u, unsigned char y2, unsigned char v, RGB *rgb)
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

int mjpeg_to_rgb(unsigned char *mjpeg, unsigned int mjpeg_size, const Img_Fmt *fmt, RGB *rgb)
{
    unsigned char 
        col_y[fmt->size],
        col_u[fmt->size],
        col_v[fmt->size];

    int result = decode_jpeg_raw(
        mjpeg, mjpeg_size, 
        0, Y4M_CHROMA_422, 
        fmt->width, fmt->height, 
        col_y, col_u, col_v);

    if (result != 0)
        printf("Error in decode_jpeg_raw: %d\n",result);

    int yuv_size = fmt->width * fmt->height / 2;

#ifdef COMPARE_PERFORMANCE
    unsigned char 
        col_y_cpy[fmt->size],
        col_u_cpy[fmt->size],
        col_v_cpy[fmt->size];

    for (int i = 0; i < fmt->size; i++)
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


int _compare_match_strength(const Img_Fmt *fmt, const HSV *hsv, 
    int i, float *res_str, int *res_i, HSV *out_hsv)
{
    if ((hsv[i].H > 180.0f - fmt->filter_hue * 180.0f && hsv[i].H < 180.0f + fmt->filter_hue * 180.0f) ||
        hsv[i].S > 1.0f - fmt->filter_sat || hsv[i].V < fmt->filter_val)
        return 0;

    int to_skip = (int)(fmt->skip_len);

    int 
        i_x = i % fmt->width, 
        i_y = i / fmt->width;

    float curr_str = 0.0f;
    float hsv_div = 0.0f;

    for (int o_y = MAX(i_y - (int)(fmt->scan_rad), 0); 
        o_y < MIN(i_y + (int)(fmt->scan_rad), fmt->height); 
        o_y++)
    {
        for (int o_x = MAX(i_x - (int)(fmt->scan_rad), 0); 
            o_x < MIN(i_x + (int)(fmt->scan_rad), fmt->width); 
            o_x++)
        {
            // Decrease the amount of samples taken per pixel for performance reasons.
            if ((o_x - i_x) % MAX(1, (int)(fmt->sample_step)) != 0 || 
                (o_y - i_y) % MAX(1, (int)(fmt->sample_step)) != 0)
                continue;

            float center_dist_sqr = (float)(
                (o_x - i_x) * (o_x - i_x) + 
                (o_y - i_y) * (o_y - i_y));
            if (center_dist_sqr > fmt->scan_rad * fmt->scan_rad)
                continue;
            float center_dist = sqrtf(center_dist_sqr);


            int i_offset = o_x + o_y * fmt->width;

            float 
                tapered_dist = center_dist / (center_dist + fmt->scan_rad / 8.0f),
                desired_s = powf(CLERP(0.0f, MAX(0.0f, LERP(-2.5f, 1.0f, tapered_dist)) * 0.85f, powf(tapered_dist, 0.1f)), 1.5f),
                desired_v = LERP(1.0f, 0.9f, tapered_dist);

            float
                old_curr_h_offset = 1.0f - CLAMP(fabsf((fmodf(hsv[i_offset].H + 180.0f, 360.0f) - 180.0f) / 120.0f) + CLERP(0.5f, 0.0f, hsv[i_offset].S * 2.0f), 0.0f, 1.0f),
                curr_h_offset = LERP(((CLAMP(fabsf(hsv[i_offset].H - 180.0f), 0.0f, 360.0f)) * ((hsv[i_offset].V + 1.0f) / 2.0f)) / 180.0f, old_curr_h_offset, fmt->old_h),
                curr_s_offset = fabsf((1.0f - desired_s) - hsv[i_offset].S),
                curr_v_offset = CLERP(1.0f, 0.0f, (desired_v - hsv[i_offset].V) / desired_v);
            
            curr_h_offset = CLAMP(curr_h_offset * fmt->h_str, 0.0f, 1.0f);
            curr_s_offset = CLAMP(curr_s_offset * fmt->s_str, 0.0f, 1.0f);
            curr_v_offset = CLAMP(curr_v_offset * fmt->v_str, 0.0f, 1.0f);

            curr_str += curr_h_offset * curr_s_offset * curr_v_offset;

            if (out_hsv != NULL)
            {
                out_hsv->H += curr_h_offset;
                out_hsv->S += curr_s_offset;
                out_hsv->V += curr_v_offset;
                hsv_div++;
            }
        }
    }

    if (out_hsv != NULL)
    {
        out_hsv->H /= MAX(1.0f, hsv_div);
        out_hsv->S /= MAX(1.0f, hsv_div);
        out_hsv->V /= MAX(1.0f, hsv_div);
    }
    
    if (curr_str > *res_str)
    {
        *res_str = curr_str;
        *res_i = i;
    }
    return to_skip;
}

int _scan_for_dot(const Img_Fmt *fmt, const HSV *hsv, int *res_i, float *res_str)
{
#ifdef COMPARE_PERFORMANCE
    // Single-threaded:
    {
        timer_begin_measure(SCAN);
        *res_str = -1.0, 
        *res_i = -1;

        for (int i = 0; i < fmt->size; i++)
        {
            HSV out_hsv = (HSV){.H=0.0f, .S=0.0f, .V=0.0f};
            i += _compare_match_strength(
                fmt, hsv, scan_radius, 
                i, res_str, res_i, &out_hsv
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
                start_i = fmt->size * t_id / thread_count, 
                end_i = fmt->size * (t_id + 1) / thread_count;

            best_str[t_id] = -1.0f,
            best_i[t_id] = -1;
            
            for (int i = start_i; i < end_i; i++)
            {
                HSV out_hsv = (HSV){.H=0.0f, .S=0.0f, .V=0.0f};
                i += _compare_match_strength(fmt, hsv, i, 
                    &(best_str[t_id]), &(best_i[t_id]), &out_hsv
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


void _visualize_pixel_strengths(const Img_Fmt *fmt, RGB *rgb, HSV *hsv)
{
    const unsigned char thread_count = 4;

    #pragma omp parallel num_threads(thread_count)
    {
        unsigned int 
            t_id = omp_get_thread_num(), 
            start_i = fmt->size * t_id / thread_count, 
            end_i = fmt->size * (t_id + 1) / thread_count;

        for (int i = start_i; i < end_i; i++)
        {
            float str = 0.0f;
            int index = -1;

            HSV out_hsv = (HSV){.H=0.0f, .S=0.0f, .V=0.0f};
            int skip = _compare_match_strength(
                fmt, hsv, i, &str, &index, &out_hsv
            );

            if (fmt->greyscale == 0.0f)
            {
                rgb[i] = (RGB){
                    .R = (unsigned char)(str / (str + 10.0f) * 255.0f), 
                    .G = (unsigned char)(str / (str + 10.0f) * 255.0f), 
                    .B = (unsigned char)(str / (str + 10.0f) * 255.0f) 
                };
            }
            else
            {
                rgb[i] = (RGB){
                    .R = (unsigned char)(out_hsv.H * 255.0f), 
                    .G = (unsigned char)(out_hsv.S * 255.0f), 
                    .B = (unsigned char)(out_hsv.V * 255.0f) 
                };
            }

            i += skip;
        }
    }
}


int draw_circle(const Img_Fmt *fmt, RGB *rgb, Vec2 pos, int r, int w, RGB col)
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
                    int x_offset = pos.x + x_flip * (int)(ang_cos * (float)(r + j));
                    int y_offset = pos.y + y_flip * (int)(ang_sin * (float)(r + j));

                    if (x_offset < 0 || x_offset >= fmt->width)
                        break;
                    if (y_offset < 0 || y_offset >= fmt->height)
                        break;

                    int i = x_offset + y_offset * fmt->width;
                    rgb[i] = col;
                }
            }
        }
    }

    return 0;
}

int draw_box(const Img_Fmt *fmt, RGB *rgb, AABB box, int w, RGB col)
{
    // Ensure that the line-width does not exceed the size of the aabb.
    w = CLAMP(w, 1, MIN((box.e - box.w) / 2, (box.s - box.n) / 2));

    #pragma omp parallel num_threads(4)
    {
        unsigned int t_id = omp_get_thread_num();
        Vec2 top_left, bot_right;

        switch (t_id)
        {
        case 0: // Fill in northern edge.
            top_left =  (Vec2){ .x = box.w,      .y = box.n     };
            bot_right = (Vec2){ .x = box.e,      .y = box.n + w };
            break;
        case 1: // Fill in southern edge.
            top_left =  (Vec2){ .x = box.w,      .y = box.s - w };
            bot_right = (Vec2){ .x = box.e,      .y = box.s     };
            break;
        case 2: // Fill in western edge.
            top_left =  (Vec2){ .x = box.w,      .y = box.n + w };
            bot_right = (Vec2){ .x = box.w + w,  .y = box.s - w };
            break;
        case 3: // Fill in eastern edge.
            top_left =  (Vec2){ .x = box.e - w,  .y = box.n + w };
            bot_right = (Vec2){ .x = box.e,      .y = box.s - w };
            break;
        }

        for (int y = top_left.y; y < bot_right.y; y++)
        {
            if (y >= fmt->height)
                break;

            for (int x = top_left.x; x < bot_right.x; x++)
            {
                if (x >= fmt->width)
                    break;

                int i = x + y * fmt->width;
                rgb[i] = col;
            }
        }
    }
    
    return 0;
}


int find_laser_dot(const Img_Fmt *fmt, const RGB *rgb, Vec2 *pos, float *confidence)
{
    HSV hsv[fmt->size];
    for (int i = 0; i < fmt->size; i++)
    {
        hsv[i] = rgb_to_hsv(rgb[i]);
    }

    float r_str;
    int r_i;
    _scan_for_dot(fmt, hsv, &r_i, &r_str);

    if (r_i == -1)
    {
        *pos = (Vec2){0,0};
        *confidence = -1;
        return -1;
    }

    *pos = (Vec2){r_i % fmt->width, r_i / fmt->width};
    *confidence = r_str;
    return 0;
}


int apply_img_effects(const Img_Fmt *fmt, RGB *rgb)
{
    HSV hsv[fmt->size];
    for (int i = 0; i < fmt->size; i++)
    {
        hsv[i] = rgb_to_hsv(rgb[i]);
    }

    _visualize_pixel_strengths(fmt, rgb, hsv);

    return 0;
}
