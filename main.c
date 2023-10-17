// gcc -Wall -g main.c jpegutils.c timer.c img_data.c aabb.c  webcam_handler.c img_processing.c input_handler.c -o release -O0 -ljpeg -lm -lSDL2 -fopenmp -lpthread
// valgrind --leak-check=full --track-origins=yes -s ./release

// gcc -Wall -g main.c jpegutils.c timer.c img_data.c aabb.c webcam_handler.c img_processing.c input_handler.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release

// gcc main.c jpegutils.c timer.c img_data.c aabb.c  webcam_handler.c img_processing.c input_handler.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "include/stb_image_write.h"
#include "include/timer.h"
#include "include/img_data.h"
#include "include/webcam_handler.h"
#include "include/img_processing.h"
#include "include/aabb.h"
#include "include/input_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <omp.h>
#include <pthread.h>


#define IMG_WIDTH 640 // 320 640 800 1280
#define IMG_HEIGHT 480 // 240 480 600 720
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

    if (fmt->visualize == 1.0f)
    {
        result = apply_img_effects(fmt, rgb);
        if (result == -1)
            return -1;
    }
    else
    {
        Vec2 dot_pos;
        float confidence = 0.0f;
        
        result = find_laser_dot(fmt, rgb, &dot_pos, &confidence);

        confidence -= fmt->dot_threshold;
        if (confidence > 0)
            draw_circle(fmt, rgb, dot_pos, 10, CLAMP((int)(log2f(confidence + 1.0f)) + confidence / 10.0f, 1, 50), (RGB){0,0,255});

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

        draw_box(fmt, rgb, left_select, 1, go_left ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, right_select, 1, go_right ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, fwd_select, 1, go_fwd ? (RGB){0, 255, 0} : (RGB){255, 0, 0});
        draw_box(fmt, rgb, back_select, 1, go_back ? (RGB){0, 255, 0} : (RGB){255, 0, 0});

        if (fmt->verbose == 1.0f)
        {
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
    }
    
    return 0;
}

void *threaded_save_png(void *input)
{
    Save_Img_Thread_Data t_data = *(Save_Img_Thread_Data*)input;

    // Get the length of the indexed name using snprintf.
    int name_length = snprintf(NULL, 0, "Frame %d.png", t_data.frame_num);

    // Write the name to a string using sprintf.
    char *img_name = malloc(name_length + 1);
    sprintf(img_name, "Frame %d.png", t_data.frame_num);

    int write_result = stbi_write_png(img_name, 
        IMG_WIDTH, IMG_HEIGHT, 
        3, t_data.rgb_copy, IMG_WIDTH * 3);
    free(img_name);
    free(t_data.rgb_copy);

    if (write_result != 1)
    {
        printf("ERROR: stbi_write_png returned %d, expected 1.\n", write_result);
        return NULL;
    }

    printf("Frame %d Captured.\n", t_data.frame_num);
    return NULL;
}


int start_snatching(int argc, const char *argv[])
{
    Img_Fmt fmt = (Img_Fmt){ 
        .width = IMG_WIDTH,
        .height = IMG_HEIGHT,
        .size = IMG_SIZE,

        .visualize = 0.0f,
        .greyscale = 0.0f,
        .verbose = 0.0f,

        .filter_hue = 0.98f,
        .filter_sat = 0.97f,
        .filter_val = 0.99f,

        .scan_rad = MAX(1.2f, IMG_HEIGHT / 80.0f),
        .skip_len = 0.0f, //2
        .sample_step = 0.0f, //2

        .dot_threshold = 0.4f,
        .alt_weights = 0.0f,

        .h_str = 1.0f,
        .s_str = 1.0f,
        .v_str = 1.0f,

        .h_white_penalty = 0.95f,
        .h_white_falloff = 0.90f,
        .h_white_curve = 1.0f,

        .compare_threading = 1.0f, //0.0f,
        .thread_count = 4.0f,
    };

    const Key_Mapping mappings[] = {
        // Full image scan visualization.
        { &fmt.visualize, "visualize", SDL_SCANCODE_Q, TOGGLE },
        // Visualize total strength as greyscale or individual hsv strengths as rgb.
        { &fmt.greyscale, "greyscale", SDL_SCANCODE_W, TOGGLE },
        // Toggle verbose dot detection output.
        { &fmt.verbose, "verbose", SDL_SCANCODE_E, TOGGLE },

        // Enables skipping pixels based on initial color-matching.
        // Warning: lowering these can severely impact performance.
        { &fmt.filter_hue, "filter_hue", SDL_SCANCODE_R, CONTINUOUS, 0.05f },
        { &fmt.filter_sat, "filter_sat", SDL_SCANCODE_T, CONTINUOUS, 0.05f },
        { &fmt.filter_val, "filter_val", SDL_SCANCODE_Y, CONTINUOUS, 0.05f },

        // Radius of surrounding pixel scan.
        { &fmt.scan_rad, "scan_rad", SDL_SCANCODE_A, CONTINUOUS, 0.2f },
        // Skip length after detecting valid pixel.
        { &fmt.skip_len, "skip_len", SDL_SCANCODE_S, STEPWISE, 1.0f },
        // The minimal distance between each pixel sampled within scan_rad.
        { &fmt.sample_step, "sample_step", SDL_SCANCODE_D, STEPWISE, 1.0f },

        // Dot detection threshold.
        { &fmt.dot_threshold, "dot_threshold", SDL_SCANCODE_F, CONTINUOUS, 0.2f },
        // Interpolates between two methods of calculating HSV weights.
        { &fmt.alt_weights, "alt_weights", SDL_SCANCODE_G, CONTINUOUS, 0.1f },

        // HSV detection weights.
        { &fmt.h_str, "h_str", SDL_SCANCODE_Z, CONTINUOUS, 0.1f },
        { &fmt.s_str, "s_str", SDL_SCANCODE_X, CONTINUOUS, 0.1f },
        { &fmt.v_str, "v_str", SDL_SCANCODE_C, CONTINUOUS, 0.1f },

        // Determines the impact of whites & blacks on the hue weights.
        // Used to penalize overexposed regions.
        { &fmt.h_white_penalty, "h_white_penalty", SDL_SCANCODE_V, CONTINUOUS, 0.05f },
        { &fmt.h_white_falloff, "h_white_falloff", SDL_SCANCODE_B, CONTINUOUS, 0.05f },
        { &fmt.h_white_curve, "h_white_curve", SDL_SCANCODE_N, CONTINUOUS, 0.05f },

        { &fmt.compare_threading, "compare_threading", SDL_SCANCODE_M, TOGGLE },
        { &fmt.thread_count, "thread_count", SDL_SCANCODE_COMMA, STEPWISE, 1.0f },
    };
    int m_count = sizeof(mappings) / sizeof(Key_Mapping);

    for (int i = 0; i < argc; i++)
    {
        int equals_index;
        for (equals_index = 0; ; equals_index++)
        {
            if (argv[i][equals_index] == '\0')
                goto next_arg;

            if (argv[i][equals_index] == '=')
                break;
        }

        for (int j = 0; j < m_count; j++)
        {
            for (int c = 0; c < equals_index; c++)
            {
                if (mappings[j].name[c] != argv[i][c])
                    goto next_mapping;
            }

            *(float*)mappings[j].ptr = (float)atof(&argv[i][equals_index + 1]);
            printf("%s: %.2f\n", mappings[j].name, *(float*)mappings[j].ptr);

        next_mapping:
        }
    next_arg:
    }
    

    if (webcam_init(&fmt) == -1) 
        return -1;

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

    printf("{\n");
    timer_init();
    while (!escape)
    {
        timer_begin_measure(FRAME);

        for (int i = 0; i < key_count; i++)
            last_state[i] = state[i];
        SDL_PumpEvents();

        if (handle_keypresses(mappings, m_count, state, last_state) == 1)
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

    webcam_close(&fmt);
    return 0;
}


int main(int argc, const char *argv[])
{     
    printf("\n======Start=====================\n");
    int handlerOut = start_snatching(argc, argv);
    printf("\n======Quit======================\n");

    printf("\n------Out-----------------------\n");
    timer_conclude();
    printf("\nHandler Output: %i\n", handlerOut);
    printf("--------------------------------\n\n");

    return handlerOut;
}
