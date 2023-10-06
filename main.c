// gcc main.c webcam_handler.c color_data.c -o release -ljpeg -lm -lSDL2
// ./release

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


#define PI 3.14159265358979323846

#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT


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


int save_png(RGB *rgb, const char *name)
{
    unsigned char extension[] = ".png";
    unsigned char file_name[strlen(name) + sizeof(extension)];

    strcpy(file_name, name);
    strcat(file_name, extension);

    int i = stbi_write_png(file_name, 
        IMG_WIDTH, IMG_HEIGHT, 
        3, rgb, 
        IMG_WIDTH * 3);

    if (i != 1)
    {
        printf("Warning: stbi_write_png returned %d, expected 1.\n", i);
        return -1;
    }

    return 0;
}

int process_image(RGB *rgb)
{    
    if (mjpeg_to_rgb(rgb) == -1)
        return -1;

    return 0;
}


int begin_snatching()
{
    format.width = IMG_WIDTH;
    format.height = IMG_HEIGHT;
    format.size = IMG_SIZE;

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
    
    const Uint8 *state = SDL_GetKeyboardState(NULL);

    bool escape = false;
    while (!escape)
    {
        SDL_PumpEvents();
        if (state[SDL_SCANCODE_Q] == 1)
        {
            printf("Escaping...\n");
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
        RGB *rgb = (RGB*)window_pixels;
        if (process_image(rgb) == -1) 
            return -1;

        if (state[SDL_SCANCODE_SPACE] == 1)
        {
            printf("Taking Picture...\n");
            if (save_png(rgb, "Picture") == -1) 
                return -1;
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

        //SDL_RenderDrawLine(g_renderer, 8, 32, 100, 150);

        SDL_RenderPresent(g_renderer);
    }

    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    if (SDL_GetError() == "Invalid window")
    {
        printf("ERROR: Invalid window\n");
        return -1;
    }
    SDL_Quit();
    return 0;
}


int main(int argc, const char** argv)
{     
    printf("\n======Start============\n");

    int handlerOut = begin_snatching();
    printf("\nHandler Output: %i\n", handlerOut);

    printf("======Close============\n\n");
    return handlerOut;
}
