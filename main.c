// gcc -Wall -g main.c jpegutils.c timer.c img_data.c webcam_handler.c img_processing.c -o release -O0 -ljpeg -lm -lSDL2 -fopenmp -lpthread
// valgrind --leak-check=full --track-origins=yes -s ./release

// gcc -Wall -g main.c jpegutils.c timer.c img_data.c webcam_handler.c img_processing.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release

// gcc main.c jpegutils.c timer.c img_data.c webcam_handler.c img_processing.c -o release -ljpeg -lm -lSDL2 -fopenmp -lpthread
// ./release


#define USE_THREADS


#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "timer.h"
#include "img_data.h"
#include "webcam_handler.h"
#include "img_processing.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
//#include <math.h>

//#include <unistd.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <sys/ioctl.h>
//#include <sys/stat.h>
//#include <sys/mman.h>

//#include <linux/videodev2.h>
#include <SDL2/SDL.h>
#include <omp.h>
#include <pthread.h>



#define IMG_WIDTH 1280 // 320 640 800 1280
#define IMG_HEIGHT 720 // 240 480 600 720
#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT


typedef struct Save_Img_Thread_Data
{
    unsigned int frame_num;
    Color *rgb_copy;
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


int process_image(const Img_Format *format, Color *rgb)
{
    unsigned char *mjpeg = NULL;
    unsigned int mjpeg_size;

    int result = get_frame(&mjpeg, &mjpeg_size);
    if (result == -1)
        return -1;

    result = mjpeg_to_rgb(mjpeg, mjpeg_size, format, rgb);
    if (result == -1)
        return -1;

    result = apply_img_effects(format, rgb);
    if (result == -1)
        return -1;

    return 0;
}

int save_png(Color *rgb, char *name)
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


int start_snatching()
{
    Img_Format format = (Img_Format){ IMG_WIDTH, IMG_HEIGHT, IMG_SIZE };

    if (webcam_init(&format) == -1) return -1;

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

        // Begin image manipulation.        
        Color rgb[IMG_SIZE];
        if (process_image(&format, rgb) == -1) 
            return -1;

        // Checks if space was pressed this frame
        if (state[SDL_SCANCODE_SPACE] == 1 && last_state[SDL_SCANCODE_SPACE] == 0)
        {
            printf("Saving Frame %d...\n", img_num);

        #ifdef USE_THREADS
            Color *rgb_copy = malloc(IMG_SIZE * sizeof(Color));
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
        
        for (int i = 0; i < IMG_SIZE; i++)
        {
            Color *window_rgb = &((Color*)window_pixels)[i];
            *window_rgb = rgb[i];
        }
        
        // End image manipulation.

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

        if (timer_end_measure(FRAME) == 1)
        {
            printf("cap reached.\n");
            escape = true;
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
