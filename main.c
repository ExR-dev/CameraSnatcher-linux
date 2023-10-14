// gcc -Wall -g main.c jpegutils.c timer.c img_data.c aabb.c  webcam_handler.c img_processing.c -o release -O0 -ljpeg -lm -lSDL2 -fopenmp -lpthread
// valgrind --leak-check=full --track-origins=yes -s ./release

// gcc -Wall -g main.c jpegutils.c timer.c img_data.c aabb.c webcam_handler.c img_processing.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release

// gcc main.c jpegutils.c timer.c img_data.c aabb.c  webcam_handler.c img_processing.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release


#define USE_THREADS


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "include/stb_image_write.h"
#include "include/timer.h"
#include "include/img_data.h"
#include "include/webcam_handler.h"
#include "include/img_processing.h"
#include "include/aabb.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <omp.h>
#include <pthread.h>


#define var_name(var) #var

#define IMG_WIDTH 320 // 320 640 800 1280
#define IMG_HEIGHT 240 // 240 480 600 720
#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT


typedef struct Save_Img_Thread_Data
{
    unsigned int frame_num;
    RGB *rgb_copy;
} Save_Img_Thread_Data;


int process_image(const Img_Fmt *fmt, RGB *rgb)
{
    unsigned char *mjpeg = NULL;
    unsigned int mjpeg_size;

    int result = get_frame(&mjpeg, &mjpeg_size);
    if (result == -1)
        return -1;

    result = mjpeg_to_rgb(mjpeg, mjpeg_size, fmt, rgb);
    if (result == -1)
        return -1;

    if (fmt->visualize == 0.0f)
    {
        Vec2 dot_pos;
        float confidence = 0.0f;
        result = find_laser_dot(fmt, rgb, &dot_pos, &confidence);
        confidence -= fmt->dot_threshold;

        if (confidence > 0)
            draw_circle(fmt, rgb, dot_pos, 10, CLAMP((int)(log2f(confidence + 1.0f)), 1, 10), (RGB){0,0,255});

        AABB 
            left_select = (AABB){0, 0, fmt->width/5, fmt->height},
            right_select = (AABB){fmt->width*4/5, 0, fmt->width, fmt->height},
            fwd_select = (AABB){fmt->width/5, 0, fmt->width*4/5, fmt->height/3},
            back_select = (AABB){fmt->width/5, fmt->height*2/3, fmt->width*4/5, fmt->height};

        bool 
            go_left = point_intersect(dot_pos, left_select) && confidence > 0, 
            go_right = point_intersect(dot_pos, right_select) && confidence > 0, 
            go_fwd = point_intersect(dot_pos, fwd_select) && confidence > 0, 
            go_back = point_intersect(dot_pos, back_select) && confidence > 0;

        draw_box(fmt, rgb, left_select, 2, go_left ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, right_select, 2, go_right ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, fwd_select, 2, go_fwd ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, back_select, 2, go_back ? (RGB){0, 255, 0} : (RGB){255, 0, 0});

        if (go_left)
            printf("Turning left...");
        else if (go_right)
            printf("Turning right...");
        else if (go_fwd)
            printf("Going forward...");
        else if (go_back)
            printf("Going back...");
        else
            printf("Idling...");
        printf("\n");
    }
    else
    {
        result = apply_img_effects(fmt, rgb);
        if (result == -1)
            return -1;
    }
    
    return 0;
}

int save_png(RGB *rgb, char *name)
{
    char *file_name = malloc(strlen(name) + strlen(".png") + 1);

    // Append the extension to the filename.
    strcpy(file_name, name);
    strcat(file_name, ".png");

    int write_result = stbi_write_png(file_name, 
        IMG_WIDTH, IMG_HEIGHT, 
        3, rgb, IMG_WIDTH * 3);

    free(file_name);

    if (write_result != 1)
    {
        printf("ERROR: stbi_write_png returned %d, expected 1.\n", write_result);
        return -1;
    }

    return 0;
}

void *threaded_save_png(void *input)
{
    Save_Img_Thread_Data t_data = *(Save_Img_Thread_Data*)input;

    // Get the length of the indexed name using snprintf.
    int name_length = snprintf(NULL, 0, "Frame %d", t_data.frame_num);
    // Write the name to a string using sprintf.
    char *img_name = malloc(name_length + 1);
    sprintf(img_name, "Frame %d", t_data.frame_num);

    int save_result = save_png(t_data.rgb_copy, img_name);
    
    free(img_name);
    free(t_data.rgb_copy);

    printf("Frame %d Captured. (out: %d)\n", t_data.frame_num, save_result);
    return NULL;
}


typedef struct Key_Var_Pair
{
    char *name;
    void *ptr;
    int SDL_key;
    float step;
} Key_Var_Pair;

int handle_keypresses(Img_Fmt *fmt, int key_count, const Uint8 *state, const Uint8 *last_state)
{
    if (state[SDL_SCANCODE_ESCAPE] == 1)
    {
        printf("Pressed escape.\n");
        return -1;
    }

    bool shift = 1 <= state[SDL_SCANCODE_LSHIFT] + state[SDL_SCANCODE_RSHIFT];
    bool   alt = 1 <= state[SDL_SCANCODE_LALT] + state[SDL_SCANCODE_RALT];
    bool    up = 1 == state[SDL_SCANCODE_UP];
    bool  down = 1 == state[SDL_SCANCODE_DOWN];
    bool  mult = 1 == state[SDL_SCANCODE_M];

    const Key_Var_Pair f_pairs[] = {
        // Full image scan visualization.
        { "visualize", &fmt->visualize, SDL_SCANCODE_Q, 1.0f },
        // Visualize total strength as greyscale or individual hsv strengths as rgb.
        { "greyscale", &fmt->greyscale, SDL_SCANCODE_W, 1.0f },
        // Dot detection threshold.
        { "dot_threshold", &fmt->dot_threshold, SDL_SCANCODE_E, 0.2f },

        // Enables skipping pixels based on initial color-matching.
        // Warning: lowering these can severely impact performance.
        { "filter_hue", &fmt->filter_hue, SDL_SCANCODE_R, 0.05f },
        { "filter_sat", &fmt->filter_sat, SDL_SCANCODE_T, 0.05f },
        { "filter_val", &fmt->filter_val, SDL_SCANCODE_Y, 0.05f },

        // Radius of surrounding pixel scan.
        { "scan_rad", &fmt->scan_rad, SDL_SCANCODE_A, 0.2f },
        // Skip length after detecting valid pixel.
        { "skip_len", &fmt->skip_len, SDL_SCANCODE_S, 0.2f },
        // The minimal distance between each pixel sampled within scan_rad.
        { "sample_step", &fmt->sample_step, SDL_SCANCODE_D, 0.2f },

        // Interpolates between a new and old method of weighing hue.
        { "old_h", &fmt->old_h, SDL_SCANCODE_F, 0.1f },

        // HSV detection weights.
        { "h_str", &fmt->h_str, SDL_SCANCODE_Z, 0.1f },
        { "s_str", &fmt->s_str, SDL_SCANCODE_X, 0.1f },
        { "v_str", &fmt->v_str, SDL_SCANCODE_C, 0.1f }
    };

    for (int i = 0; i < sizeof(f_pairs) / sizeof(Key_Var_Pair); i++)
    {
        if (state[f_pairs[i].SDL_key] == 1)
        {
            float last_val = *(float*)f_pairs[i].ptr;

            if (last_state[f_pairs[i].SDL_key] != 1)
                printf("%s: %.2f\n", f_pairs[i].name, last_val);

            if (mult)
            {
                if (up)         *(float*)f_pairs[i].ptr *= (shift ? 1.25f : (alt ? 1.01f : 1.05f));
                else if (down)  *(float*)f_pairs[i].ptr *= (shift ? 0.8f : (alt ? 0.99f : 0.95f));
            }
            else
            {
                if (up)         *(float*)f_pairs[i].ptr += f_pairs[i].step * (shift ? 10.0f : (alt ? 0.1f : 1.0f));
                else if (down)  *(float*)f_pairs[i].ptr -= f_pairs[i].step * (shift ? 10.0f : (alt ? 0.1f : 1.0f));
            }

            *(float*)f_pairs[i].ptr = MAX(0.0f, *(float*)f_pairs[i].ptr);
            
            if (*(float*)f_pairs[i].ptr != last_val)
                printf("%s: %.2f\n", f_pairs[i].name, *(float*)f_pairs[i].ptr);
        }
    }
    return 0;
}


int start_snatching()
{
    Img_Fmt fmt = (Img_Fmt){ 
        .width = IMG_WIDTH,
        .height = IMG_HEIGHT,
        .size = IMG_SIZE,


        .visualize = 1.0f,
        .greyscale = 6.0f,
        .dot_threshold = 0.0f,

        .filter_hue = 0.99f,
        .filter_sat = 0.99f,
        .filter_val = 0.99f,

        .scan_rad = 2.0f,
        .skip_len = 0.0f,
        .sample_step = 0.0f,

        .old_h = 0.0f,

        .h_str = 1.0f,
        .s_str = 0.0f,
        .v_str = 0.0f

        /*
        .visualize = 1.0f,
        .greyscale = 0.0f,
        .filter_white = 1.0f,

        .dot_threshold = 8.0f,
        .old_h = 0.0f,

        .scan_rad = IMG_HEIGHT / 25.0f,
        .skip_len = 1.0f,
        .sample_step = 3.0f,
        

        .h_str = 1.0f,
        .s_str = 1.0f,
        .v_str = 1.0f
        */
    };

    if (webcam_init(&fmt) == -1) return -1;

    printf("\nOpening Window...\n");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) 
    {
        printf("ERROR: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window *g_window = SDL_CreateWindow("SDL Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, IMG_WIDTH, IMG_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (g_window == NULL)
    {
        printf("ERROR: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer *g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (g_renderer == NULL)
    {
        printf("ERROR: %s\n", SDL_GetError());
        return -1;
    }
    
    SDL_Texture *g_stream_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, IMG_WIDTH, IMG_HEIGHT);
    if (g_stream_texture == NULL)
    {
        printf("ERROR: %s\n", SDL_GetError());
        return -1;
    }
    

    int key_count = 0;
    const Uint8 *state = SDL_GetKeyboardState(&key_count);
    Uint8 *const last_state = malloc(key_count * sizeof(Uint8));

    unsigned char img_num = 0;
    bool escape = false;

    printf("\n{\n");
    timer_init();
    while (!escape)
    {
        timer_begin_measure(FRAME);

        for (int i = 0; i < key_count; i++)
            last_state[i] = state[i];
        SDL_PumpEvents();

        if (handle_keypresses(&fmt, key_count, state, last_state) == -1)
            escape = true;


        if (next_frame() == -1) return -1;

        void *window_pixels;
        int pitch;
        if (SDL_LockTexture(g_stream_texture, NULL, &window_pixels, &pitch) < 0) 
        {
            printf("ERROR: %s\n", SDL_GetError());
            return -1;
        }

        // Begin image manipulation.
        timer_begin_measure(MANIPULATION);
        RGB rgb[IMG_SIZE];
        if (process_image(&fmt, rgb) == -1) 
            return -1;

        // Checks if space was pressed this frame
        if (state[SDL_SCANCODE_SPACE] == 1 && last_state[SDL_SCANCODE_SPACE] == 0)
        {
            printf("Saving Frame %d...\n", img_num);

            RGB *rgb_copy = malloc(IMG_SIZE * sizeof(RGB));
            for (int i = 0; i < IMG_SIZE; i++)
                rgb_copy[i] = rgb[i];
            
            pthread_t frame_capture_handle;
            Save_Img_Thread_Data t_data = (Save_Img_Thread_Data){img_num++, rgb_copy};
            
            // Create a detached thread that will try to save a copy of the current frame.
            // Failures are ignored.
            pthread_create(&frame_capture_handle, NULL, threaded_save_png, &t_data);
            pthread_detach(frame_capture_handle);
        }
        
        for (int i = 0; i < IMG_SIZE; i++)
        {
            RGB *window_rgb = &((RGB*)window_pixels)[i];
            *window_rgb = rgb[i];
        }
        timer_end_measure(MANIPULATION); // End image manipulation.


        if (close_frame() == -1) 
            return -1;

        SDL_UnlockTexture(g_stream_texture);
        if (SDL_RenderClear(g_renderer) < 0) 
        {
            printf("ERROR: %s\n", SDL_GetError());
            return -1;
        }
        if (SDL_RenderCopy(g_renderer, g_stream_texture, NULL, NULL) < 0) 
        {
            printf("ERROR: %s\n", SDL_GetError());
            return -1;
        }

        SDL_RenderPresent(g_renderer);

        if (timer_end_measure(FRAME) == 1)
        {
            //escape = true;
        }
    }
    timer_quit();
    printf("}\n");

    free(last_state);

    SDL_DestroyRenderer(g_renderer);
    
    SDL_ClearError();
    SDL_DestroyWindow(g_window);
    if (strcmp(SDL_GetError(), "") != 0)
    {
        printf("ERROR: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Quit();
    return 0;
}


int main(int argc, const char** argv)
{     
    printf("\n======Start============\n");
    int handlerOut = start_snatching();
    printf("\n======Quit=============\n");

    timer_conclude();

    printf("Handler Output: %i\n", handlerOut);
    printf("=======================\n");
    return handlerOut;
}
