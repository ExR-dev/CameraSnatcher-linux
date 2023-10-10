// gcc -Wall -g main.c jpegutils.c webcam_handler.c color_data.c -o release -O0 -ljpeg -lm -lSDL2 -fopenmp -lpthread
// valgrind --leak-check=full --track-origins=yes -s ./release

// gcc -Wall -g main.c jpegutils.c webcam_handler.c color_data.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release

// gcc main.c jpegutils.c webcam_handler.c color_data.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release


#define USE_THREADS


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "color_data.h"
#include "webcam_handler.h"

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
#include <SDL2/SDL.h>
#include <omp.h>
#include <pthread.h>


#define PI 3.14159265358979323846

#define IMG_WIDTH 640
#define IMG_HEIGHT 360
#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT


typedef struct Timer_Data
{
    double frame_times[512];
    double yuyv_converts[256];

    double tot_time;

    unsigned short frame_count;
    unsigned char yuyv_convert_count;
} Timer_Data;

typedef struct Save_Img_Thread_Data
{
    unsigned int frame_num;
    RGB *rgb_copy;
} Save_Img_Thread_Data;

typedef struct AABB
{
    unsigned short n; // The y-value of the boxes northern edge.
    unsigned short e; // The x-value of the boxes eastern edge.
    unsigned short s; // The y-value of the boxes southern edge.
    unsigned short w; // The x-value of the boxes western edge.
} AABB;

bool AABB_intersect(AABB box1, AABB box2)
{
    if (box1.e < box2.w) return false;
    if (box1.w > box2.e) return false;
    if (box1.s < box2.n) return false;
    if (box1.n > box2.s) return false;
    return true;
}

AABB AABB_combine(AABB box1, AABB box2)
{
    AABB new_box;
    new_box.n = MIN(box1.n, box2.n);
    new_box.s = MAX(box1.s, box2.s);
    new_box.w = MIN(box1.w, box2.w);
    new_box.e = MAX(box1.e, box2.e);
    return new_box;
}


void col_manip_add_circle(RGB *rgb, int x, int y, int r, int w)
{
    for (float angle = 0.0f; angle < PI * 2.0f; angle += 1.0f / ((float)(r + w) * PI))
    {
        for (int j = 0; j < w; j++)
        {
            int x_offset = x + (int)(cosf(angle) * (float)(r + j));
            int y_offset = y + (int)(sinf(angle) * (float)(r + j));

            if (x_offset < 0 || x_offset >= IMG_WIDTH)
                continue;
            if (y_offset < 0 || y_offset >= IMG_HEIGHT)
                continue;

            int i = x_offset + y_offset * IMG_WIDTH;

            rgb[i].R = 255;
            rgb[i].G = 0;
            rgb[i].B = 0;
        }
    }
}


int process_image(RGB *rgb)
{
    int conversion_result = mjpeg_to_rgb(rgb);
    if (conversion_result == -1)
        return -1;

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


int start_snatching(Timer_Data *timer)
{
    img_format.width = IMG_WIDTH;
    img_format.height = IMG_HEIGHT;
    img_format.size = IMG_SIZE;

    if (webcam_init() == -1) return -1;

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

    printf("\n{\n");

    unsigned int frame_i = 0;
    unsigned char img_num = 0;

    double curr_frame_time = 0;
    double start_time = omp_get_wtime();

    bool escape = false;
    while (!escape)
    {
        frame_i++;

        for (int i = 0; i < key_count; i++)
            last_state[i] = state[i];
        SDL_PumpEvents();

        if (state[SDL_SCANCODE_Q] == 1)
        {
            printf("Pressed Quit.\n");
            escape = true;
        }

        if (next_frame() == -1) return -1;

        void *window_pixels;
        int pitch;
        if (SDL_LockTexture(g_stream_texture, NULL, &window_pixels, &pitch) < 0) 
        {
            printf("ERROR: %s\n", SDL_GetError());
            return -1;
        }


        if (timer->frame_count < 512)
            curr_frame_time = omp_get_wtime();

        // Begin image manipulation.
        RGB *rgb = (RGB*)window_pixels;
        if (process_image(rgb) == -1) 
            return -1;

        // Checks if space was pressed this frame
        if (state[SDL_SCANCODE_SPACE] == 1 && last_state[SDL_SCANCODE_SPACE] == 0)
        {
            printf("Saving Frame %d...\n", img_num);

        #ifdef USE_THREADS
            RGB *rgb_copy = malloc(IMG_SIZE * sizeof(RGB));
            for (int i = 0; i < IMG_SIZE; i++)
                rgb_copy[i] = rgb[i];
            
            pthread_t frame_capture_handle;
            Save_Img_Thread_Data t_data = (Save_Img_Thread_Data){img_num++, rgb_copy};
            
            // Create a detached thread that will try to save a copy of the current frame.
            // Failures are ignored.
            pthread_create(&frame_capture_handle, NULL, threaded_save_png, &t_data);
            pthread_detach(frame_capture_handle);
        #else
            int name_length = snprintf(NULL, 0, "Frame %d", img_num);
            char *img_name = malloc(name_length + 1);
            sprintf(img_name, "Frame %d", img_num++);

            int save_result = save_png(rgb, img_name);
            free(img_name);

            if (save_result == -1)
                return -1;
        #endif
        }
        // End image manipulation.

        if (timer->frame_count == 512 - 1)
            printf("cap reached.\n");
        if (timer->frame_count < 512)
            timer->frame_times[timer->frame_count++] = (omp_get_wtime() - curr_frame_time) * 1000.0;
            

        if (close_frame() == -1) return -1;

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
    }
    printf("}\n");
    timer->tot_time = (omp_get_wtime() - start_time) * 1000.0 / (double)frame_i;

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
    Timer_Data timer = (Timer_Data){ {}, {}, 0, 0 };
    int handlerOut = start_snatching(&timer);
    printf("\n======Quit=============\n");

    double tot_frame_time = 0;
    for (int i = 0; i < timer.frame_count; i++)
    {
        tot_frame_time += timer.frame_times[i];
    }
    printf("Avg Frame length across %d frames: ", (int)timer.frame_count);
    printf("%f ms\n", (float)tot_frame_time / (float)timer.frame_count);
    printf("Lifetime / Frame count: %f ms\n", (float)timer.tot_time);

    printf("Handler Output: %i\n", handlerOut);
    printf("=======================\n");
    return handlerOut;
}
