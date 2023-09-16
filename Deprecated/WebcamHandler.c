#ifndef MAINDEFINE
#define EXTERNAL
#endif

#ifndef WEBCAM_HANDLER_H
#define WEBCAM_HANDLER_H
#include "WebcamHandler.h"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#ifdef EXTERNAL
#include <jpeglib.h>
#include <jerror.h>
#endif


#define IMG_WIDTH 640
#define IMG_HEIGHT 480
    
#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) < (b)) ? (b) : (a))
#define CLAMP(x,a,b) (MAX(a, MIN(x, b)))


typedef struct Pixel_RGBA
{
    unsigned char R;
    unsigned char G;
    unsigned char B;
    unsigned char A;
} Pixel_RGBA;

struct Capture_Data
{
    int handle;
    unsigned char* imageMemory[2];
    char name;

    struct v4l2_format format;
    struct v4l2_requestbuffers buffer_request;
    struct v4l2_buffer query_buffer;
    struct v4l2_buffer write_buffer;
    struct v4l2_buffer queueBuf;

#ifdef EXTERNAL
    //struct jpeg_decompress_struct cinfo;
    //struct jpeg_error_mgr jerr;
#endif

    int out;
};

/// @brief Debug function for getting the name associated with a given output from v4l2_fourcc() in videodev2.h
/// @param code A number outputted from v4l2_fourcc()
/// @param out The four letter string that makes v4l2_fourcc() output code
void _GetName(unsigned int code, unsigned char* out) 
{
    out[0] = (unsigned char)((code) - ((code >> 8) << 8));
    out[1] = (unsigned char)((code >> 8) - ((code >> 16) << 8));
    out[2] = (unsigned char)((code >> 16) - ((code >> 24) << 8));
    out[3] = (unsigned char)(code >> 24);
    out[4] = '\0';
}


#ifdef EXTERNAL
typedef struct 
{
    struct jpeg_source_mgr pub;   // public fields

    JOCTET* buffer;               // start of buffer
    boolean start_of_file;        // have we gotten any data yet?
} my_source_mgr;

typedef my_source_mgr * my_src_ptr;

static void jpg_memInitSource(j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    src->start_of_file = TRUE;
}

static boolean jpg_memFillInputBuffer(j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    src->start_of_file = FALSE;
    return TRUE;
}

static void jpg_memSkipInputData(j_decompress_ptr cinfo, long num_bytes)
{
    my_src_ptr src = (my_src_ptr) cinfo->src;
    if (num_bytes > 0) 
    {
        src->pub.next_input_byte += (size_t) num_bytes;
        src->pub.bytes_in_buffer -= (size_t) num_bytes;
    }
}

static void jpg_memTermSource(j_decompress_ptr cinfo) { }


void mjpeg_to_rgba(struct Capture_Data* data, Pixel_RGBA* rgba)
{
    unsigned char* mjpeg = (*data).imageMemory[(*data).queueBuf.index];
    unsigned char rgb[IMG_SIZE * 3];
    int size = (*data).queueBuf.bytesused;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    my_src_ptr src;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    cinfo.src = (struct jpeg_source_mgr*)(*cinfo.mem->alloc_small) ((j_common_ptr) &cinfo, JPOOL_PERMANENT, sizeof(my_source_mgr));
    src = (my_src_ptr) cinfo.src;
    src->buffer = (JOCTET *)mjpeg;

    src->pub.init_source = jpg_memInitSource;
    src->pub.fill_input_buffer = jpg_memFillInputBuffer;
    src->pub.skip_input_data = jpg_memSkipInputData;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = jpg_memTermSource;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = mjpeg;

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    unsigned char* rgbPtr = (unsigned char*)rgb;
    while (cinfo.output_scanline < cinfo.output_height) 
    {
        jpeg_read_scanlines(&cinfo, &rgbPtr, 1);
        rgbPtr += IMG_WIDTH * 3;
    }

    for (int i = 0; i < IMG_SIZE * 3; i += 3)
    {
        rgba[i/3].R = rgb[i+0];
        rgba[i/3].G = rgb[i+1];
        rgba[i/3].B = rgb[i+2];
        rgba[i/3].A = 255;
    }

    jpeg_finish_decompress(&cinfo); // Do this even under error conditions
    jpeg_destroy_decompress(&cinfo); // Do this even under error conditions
}
#endif

static void _YUYVtoRGB(unsigned char y, unsigned char u, unsigned char v, Pixel_RGBA* rgba)
{
    int c = y-16;
    int d = u-128;
    int e = v-128;

    rgba->R = CLAMP((298 * c + 516 * d + 128) >> 8, 0, 255);
    rgba->G = CLAMP((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);
    rgba->B = CLAMP((298 * c + 409 * e + 128) >> 8, 0, 255);
    rgba->A = 255;
}


void set_supported_video_format(struct Capture_Data* data)
{
    memset(&((*data).format), 0, sizeof((*data).format));

    (*data).format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (*data).format.fmt.pix.width = IMG_WIDTH;
    (*data).format.fmt.pix.height = IMG_HEIGHT;
    (*data).format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    (*data).format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl((*data).handle, VIDIOC_S_FMT, &((*data).format)) < 0)
    {
        printf("VIDIOC_S_FMT Video format set failed!\n");
        (*data).out = -1;
        return;
    }

    printf("\nPixel Formats:\n");

    unsigned char result[5] = "NULL";

    _GetName(V4L2_PIX_FMT_MJPEG, result);
    printf("Desired: %s\n", result);

    _GetName((*data).format.fmt.pix.pixelformat, result);
    printf("Actual: %s\n", result);
}

void request_buffers(struct Capture_Data* data)
{
    memset(&((*data).buffer_request), 0, sizeof((*data).buffer_request));

    (*data).buffer_request.count = 2;
    (*data).buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (*data).buffer_request.memory = V4L2_MEMORY_MMAP;

    if (ioctl((*data).handle, VIDIOC_REQBUFS, &((*data).buffer_request)) < 0)
    {
        printf("VIDIOC_REQBUFS failed!\n");
        (*data).out = -1;
    }
}

void _query_created_buffers(struct Capture_Data* data, int i)
{
    memset(&((*data).query_buffer), 0, sizeof((*data).query_buffer));

    (*data).query_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (*data).query_buffer.memory = V4L2_MEMORY_MMAP;
    (*data).query_buffer.index = i;

    if (ioctl((*data).handle, VIDIOC_QUERYBUF, &((*data).query_buffer)) < 0)
    {
        printf("VIDIOC_QUERYBUF failed at index %i!\n", i);
        (*data).out = -1;
        return;
    }

    (*data).imageMemory[i] = mmap(
        NULL,
        (*data).query_buffer.length,
        PROT_READ,
        MAP_SHARED,
        (*data).handle,
        (*data).query_buffer.m.offset);
}

void _queue_buffers_to_write(struct Capture_Data* data, int i)
{
    memset(&((*data).write_buffer), 0, sizeof((*data).write_buffer));

    (*data).write_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (*data).write_buffer.memory = V4L2_MEMORY_MMAP;
    (*data).write_buffer.index = i;

    if (ioctl((*data).handle, VIDIOC_QBUF, &((*data).write_buffer)) < 0)
    {
        printf("VIDIOC_QBUF failed at index %i!\n", i);
        (*data).out = -1;
    }
}

void _start_camera(struct Capture_Data* data)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl((*data).handle, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON failed!\n");
        (*data).out = -1;
    }
}

void apply_color_manipulation(Pixel_RGBA* pixels)
{
    for (int x = 0; x < IMG_WIDTH; x++)
    {
        for (int y = 0; y < IMG_HEIGHT; y++)
        {
            // Utility color-manipulation values
            int i = x + y * IMG_WIDTH;

            float 
                u = (float)x / (float)IMG_WIDTH, 
                v = (float)y / (float)IMG_HEIGHT;

            float 
                r = (float)pixels[i].R / 255.0f,
                g = (float)pixels[i].G / 255.0f,
                b = (float)pixels[i].B / 255.0f;

            float 
                _R = 255.0f,
                _G = 255.0f,
                _B = 255.0f;
            // Utility color-manipulation values

            // Overwrite camera for color conversion testing
            if (FALSE)
            {
                float angle = (u + 0.5f) * 360.0f;
                if (u >= 0.5f) angle -= 360.0f;

                float chroma = 1.0f, saturation = 1.0f, lightness = 0.5f;

                float x = chroma * (1.0f - fabs(fmod(angle / 60.0f, 2.0f) - 1.0f));
                float m = lightness - (chroma / 2.0f);

                if (angle >= 0.0f && angle < 60.0f)         { r = chroma; g = x; b = 0.0f; }
                else if (angle >= 60.0f && angle < 120.0f)  { r = x; g = chroma; b = 0.0f; } 
                else if (angle >= 120.0f && angle < 180.0f) { r = 0.5f; g = 0.5f; b = 0.5f; } 
                else if (angle >= 180.0f && angle < 240.0f) { r = 0.0f; g = x; b = chroma; } 
                else if (angle >= 240.0f && angle < 300.0f) { r = x; g = 0.0f; b = chroma; } 
                else                                        { r = chroma; g = 0.0f; b = x; }

                if (v < 0.5) {
                    r = 1.0f + (r - 1.0f) * (2.0f * v);
                    g = 1.0f + (g - 1.0f) * (2.0f * v);
                    b = 1.0f + (b - 1.0f) * (2.0f * v);
                } else {
                    r = r + (0.0f - r) * (2.0f * v - 1.0f);
                    g = g + (0.0f - g) * (2.0f * v - 1.0f);
                    b = b + (0.0f - b) * (2.0f * v - 1.0f);
                }

                _R = CLAMP((r + m) * 255.0f, 0.0f, 255.0f);
                _G = CLAMP((g + m) * 255.0f, 0.0f, 255.0f);
                _B = CLAMP((b + m) * 255.0f, 0.0f, 255.0f);

                pixels[i].R = (int)_R;
                pixels[i].G = (int)_G;
                pixels[i].B = (int)_B;
            }
            // Overwrite camera for color conversion testing

            // Enhance reds
            if (x % 3 != 0 && y % 3 != 0)
            {
                r = (float)pixels[i].R / 255.0f;
                g = (float)pixels[i].G / 255.0f;
                b = (float)pixels[i].B / 255.0f;

                float maxCol = MAX(r, MAX(g, b));
                float minCol = MIN(r, MIN(g, b));
                float redness = 0.5f;

                //redness += (r * 2.0f - (g + b) * .15f);
                redness = r * 1.5f - sqrtf( g*g + b*b);
                //redness = redness/2.0f + r * (maxCol) / (1.0f + powf(b, 0.25f)) / (1.0f + powf(g, 0.25f));
                //redness = powf(CLAMP(redness, 0.0f, 1.0f), 2.0f);
                //redness += (r+g+b) / 3.0f;
                //redness /= (sqrtf(b+b) + 1.0f);
                //redness /= (sqrtf(g+g) + 1.0f);

                /*if (r < 0.65f)
                    redness -= 0.5f;
                if (r - 0.05f <= (r+g+b) / 3.0f)
                    redness -= 1.0f;
                if (r + g > 1.0f && b < 0.2f)
                    redness -= 0.5f;
                if (r + b > 1.0f && g < 0.2f)
                    redness -= 0.5f;*/

                /*if (g + 0.25f > r && r < 0.95f && r + g > 0.50f && b < 0.25f)
                    redness -= 0.5f;
                else if (b + 0.25f > r && r < 0.95f && r + b > 0.50f && g < 0.25f)
                    redness -= 0.5f;*/
                
                redness *= CLAMP(r + b/2.5f - g - 0.5f, 0.0f, 1.0f);
                redness *= CLAMP(r + g/2.5f - b - 0.5f, 0.0f, 1.0f);

                if (maxCol == r && r > 0.45f && g < 0.25f && b < 0.25f)
                    redness++;

                float redMask = CLAMP(ceilf(redness), 0.0f, 1.0f);
                //redMask = CLAMP(redness, 0.0f, 1.0f);
                //redMask = redness;
                pixels[i].R = (int)(redMask * 0.0f);
                pixels[i].G = (int)(redMask * 255.0f);
                pixels[i].B = (int)(redMask * 0.0f);
            }
            // Enhance reds

            // Add circle
            if (FALSE)
            {
                int centerDist = (powf(x - IMG_WIDTH / 2, 2) + powf(y - IMG_HEIGHT / 2, 2));

                if (centerDist > 38025 && centerDist < 40000) // sqrt() is expensive: 195² < centerDist < 200²
                {
                    pixels[i].R = 255;
                    pixels[i].G = 0;
                    pixels[i].B = 0;
                }
            }
            // Add circle

            // Add border
            if (FALSE)
            {
                int borderWidth = 2;
                if ((x < borderWidth || x > IMG_WIDTH - borderWidth) ||
                    (y < borderWidth || y > IMG_HEIGHT - borderWidth))
                {
                    pixels[i].R = 255;
                    pixels[i].G = 0;
                    pixels[i].B = 0;
                }
            }
            // Add border
        }
    }
}

int _process_image(struct Capture_Data* data)
{
    static Pixel_RGBA rgbaConversion[IMG_SIZE];
    
    char imgName[] = "_.___";
    imgName[0] = (*data).name;

#ifdef EXTERNAL
    mjpeg_to_rgba(&(*data), &(*rgbaConversion));
#else
    unsigned char* _yuv = (*data).imageMemory[(*data).queueBuf.index];
    int _size = (*data).queueBuf.bytesused;

    int rgbaIndex = 0;
    for (int i = 0; i < _size; i += 3)
    {
        unsigned char y1 = _yuv[i + 0];
        unsigned char u = _yuv[i + 1];
        unsigned char y2 = _yuv[i + 2];
        unsigned char v = _yuv[i + 3];

        _YUYVtoRGB(y1, u, v, &rgbaConversion[rgbaIndex++]);
        _YUYVtoRGB(y2, u, v, &rgbaConversion[rgbaIndex++]);
    }

    imgName[2] = 'j';
    imgName[3] = 'p';
    imgName[4] = 'g';
    FILE *f = fopen(imgName, "wb");
    fwrite(_yuv, sizeof(unsigned char), _size, f);
    fclose(f);

#endif

    apply_color_manipulation(&(*rgbaConversion));

    imgName[2] = 'p';
    imgName[3] = 'n';
    imgName[4] = 'g';
    stbi_write_png(imgName, 
        IMG_WIDTH, IMG_HEIGHT, 
        4, &rgbaConversion, 
        IMG_WIDTH * 4);

    return 0;
}

void _dequeue_buffers(struct Capture_Data* data)
{
    memset(&((*data).queueBuf), 0, sizeof((*data).queueBuf));

    (*data).queueBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (*data).queueBuf.memory = V4L2_MEMORY_MMAP;

    if (ioctl((*data).handle, VIDIOC_DQBUF, &((*data).queueBuf)) < 0)
    {
        printf("VIDIOC_DQBUF failed!\n");
        (*data).out = errno;
        return;
    }

    _process_image(data);

    if (ioctl((*data).handle, VIDIOC_QBUF, &((*data).queueBuf)) < 0)
    {
        printf("VIDIOC_QBUF failed!\n");
        (*data).out = -1;
    }
}


int StartWebcamHandler(void)
{
    struct Capture_Data data;
    data.handle = open("/dev/video0", O_RDWR, 0);
    data.out = 1;

    set_supported_video_format(&data);
    if (data.out != 1) return data.out;

    request_buffers(&data);
    if (data.out != 1) return data.out;

    for (int i = 0; i < 2; i++)
    {
        _query_created_buffers(&data, i);
        if (data.out != 1) return data.out;
    }

    for (int i = 0; i < 2; i++)
    {
        _queue_buffers_to_write(&data, i);
        if (data.out != 1) return data.out;
    }

    data.name = 'A';

    printf("\nTaking Pic... ");
    _start_camera(&data);
    if (data.out != 1) return data.out;

    _dequeue_buffers(&data);
    if (data.out != 1) return data.out;

    return data.out;
}