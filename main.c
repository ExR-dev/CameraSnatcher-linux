//#define DEBUG

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

// My camera can only output in MJPEG format which as far as I could 
// find meant that I'd have to use some external library. 
// (This was written before the module addressing this was published)
#include <jpeglib.h>
#include <jerror.h>


//#define COL_MANIP_OVERWRITE_CAM
#define COL_MANIP_MASK_REDS
#define COL_MANIP_ADD_RECTS
//#define COL_MANIP_ADD_CIRCLE


#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))
#define CLAMP(x, a, b) (MAX(a, MIN(x, b)))
#define LERP(a, b, t) (a * (1.0 - t) + (b * t))


#define PI 3.14159265358979323846f

#ifdef COL_MANIP_OVERWRITE_CAM
#define IMG_WIDTH 1792 //1280
#define IMG_HEIGHT 512 //768
#else
#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#endif

#define IMG_SIZE IMG_WIDTH * IMG_HEIGHT


typedef struct AABB
{
    unsigned short n;
    unsigned short e;
    unsigned short s;
    unsigned short w;
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


typedef struct Color_RGBA
{
    __uint8_t R;
    __uint8_t G;
    __uint8_t B;
    __uint8_t A;
} Color_RGBA;

typedef struct Color_HSV
{
    double H;
    double S;
    double V;
} Color_HSV;

Color_HSV rgba_to_hsv(Color_RGBA rgb) 
{
    double 
        r = rgb.R / 255.0, 
        g = rgb.G / 255.0, 
        b = rgb.B / 255.0;

    double cmax = MAX(r, MAX(g, b));
    double cmin = MIN(r, MIN(g, b));

    double diff = cmax - cmin;

    double h, s, v;

    if (cmax == cmin)
       h = 0.0;
    else if (cmax == r)
       h = fmod((60.0 * ((g - b) / diff) + 360.0), 360.0);
    else if (cmax == g)
       h = fmod((60.0 * ((b - r) / diff) + 120.0), 360.0);
    else if (cmax == b)
       h = fmod((60.0 * ((r - g) / diff) + 240.0), 360.0);

    if (cmax == 0.0)
        s = 0.0;
    else
        s = (diff / cmax) * 100.0;

    v = cmax * 100.0;

    return (Color_HSV){.H = h, .S = s, .V = v};
}

Color_RGBA hsv_to_rgba(Color_HSV hsv)
{
    double 
        r = 0.0, 
        g = 0.0, 
        b = 0.0;

	if (hsv.S == 0.0)
	{
		r = hsv.V;
		g = hsv.V;
		b = hsv.V;
	}
	else
	{
		int i;
		double f, p, q, t;

		if (hsv.H == 360.0)
			hsv.H = 0.0;
		else
			hsv.H = hsv.H / 60.0;

		i = (int)trunc(hsv.H);
		f = hsv.H - i;

		p = hsv.V * (1.0 - hsv.S);
		q = hsv.V * (1.0 - (hsv.S * f));
		t = hsv.V * (1.0 - (hsv.S * (1.0 - f)));

		switch (i)
		{
		case 0:
			r = hsv.V;
			g = t;
			b = p;
			break;

		case 1:
			r = q;
			g = hsv.V;
			b = p;
			break;

		case 2:
			r = p;
			g = hsv.V;
			b = t;
			break;

		case 3:
			r = p;
			g = q;
			b = hsv.V;
			break;

		case 4:
			r = t;
			g = p;
			b = hsv.V;
			break;

		default:
			r = hsv.V;
			g = p;
			b = q;
			break;
		}

	}

	Color_RGBA rgba = {
        .R = (r * 255.0),
        .G = (g * 255.0),
        .B = (b * 255.0),
        .A = 255
    };

	return rgba;
}


typedef struct Capture_Data
{
    int handle;
    __uint8_t file_id;
    __uint8_t* img_mem[2];

    struct v4l2_format format;
    struct v4l2_requestbuffers buffer_request;
    struct v4l2_buffer query_buffer;
    struct v4l2_buffer write_buffer;
    struct v4l2_buffer queue_buffer;
} Capture_Data;


#ifdef DEBUG
/// @brief Debug function for getting the name associated with a given output from v4l2_fourcc() in videodev2.h.
/// @param code A number outputted from v4l2_fourcc().
/// @param out The four letter string that makes v4l2_fourcc() output code.
void _get_v4l2_code_name(__uint8_t code, __uint8_t* out) 
{
    out[0] = (__uint8_t)((code) - ((code >> 8) << 8));
    out[1] = (__uint8_t)((code >> 8) - ((code >> 16) << 8));
    out[2] = (__uint8_t)((code >> 16) - ((code >> 24) << 8));
    out[3] = (__uint8_t)(code >> 24);
    out[4] = '\0';
}
#endif


// Boilerplate code for using jpeglib.
typedef struct 
{
    struct jpeg_source_mgr pub;

    JOCTET* buffer;
    boolean start_of_file;
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

void mjpeg_to_rgba(Capture_Data* data, Color_RGBA* rgba)
{
    __uint8_t* mjpeg = data->img_mem[data->queue_buffer.index];
    __uint8_t rgb[IMG_SIZE * 3];
    int size = data->queue_buffer.bytesused;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    my_src_ptr src;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    cinfo.src = (struct jpeg_source_mgr*)(*cinfo.mem->alloc_small)(
        (j_common_ptr)&cinfo, 
        JPOOL_PERMANENT, 
        sizeof(my_source_mgr)
        );
    
    src = (my_src_ptr) cinfo.src;
    src->buffer = (JOCTET*)mjpeg;

    src->pub.init_source = jpg_memInitSource;
    src->pub.fill_input_buffer = jpg_memFillInputBuffer;
    src->pub.skip_input_data = jpg_memSkipInputData;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = jpg_memTermSource;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = mjpeg;

    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    __uint8_t* rgb_ptr = (__uint8_t*)rgb;
    while (cinfo.output_scanline < cinfo.output_height) 
    {
        jpeg_read_scanlines(&cinfo, &rgb_ptr, 1);
        rgb_ptr += IMG_WIDTH * 3;
    }

    for (int i = 0; i < IMG_SIZE * 3; i += 3)
    {
        rgba[i/3].R = rgb[i+0];
        rgba[i/3].G = rgb[i+1];
        rgba[i/3].B = rgb[i+2];
        rgba[i/3].A = 255;
    }

    jpeg_finish_decompress(&cinfo); 
    jpeg_destroy_decompress(&cinfo);
}
// Boilerplate code for using jpeglib.


void col_manip_overwrite_cam(Color_RGBA* rgba)
{
    for (int x = 0; x < IMG_WIDTH; x++)
    {
        for (int y = 0; y < IMG_HEIGHT; y++)
        {
            // Utility color-manipulation values
            int i = x + y * IMG_WIDTH;

            Color_HSV hsv = {.H = 0.0, .S = 1.0, .V = 1.0};

            float 
                u = (float)x / (float)IMG_WIDTH, 
                v = (float)y / (float)IMG_HEIGHT;

            float 
                r = (float)rgba[i].R / 255.0f,
                g = (float)rgba[i].G / 255.0f,
                b = (float)rgba[i].B / 255.0f;

            float 
                _R = 255.0f,
                _G = 255.0f,
                _B = 255.0f;

            _R = (float)rgba[i].R / 255.0f;
            _G = (float)rgba[i].G / 255.0f;
            _B = (float)rgba[i].B / 255.0f;
            // Utility color-manipulation values

            float angle = (u + 0.5f) * 360.0f;

            if (u >= 0.5f) 
                angle -= 360.0f;

            float val1 = 1.0f * (1.0f - fabs(fmod(angle / 60.0f, 2.0f) - 1.0f));
            float val2 = 0.5f - (1.0f / 2.0f);

            if      (angle >= 0.0f && angle < 60.0f)       { r = 1.0f;   g = val1;   b = 0.0f; }
            else if (angle >= 60.0f && angle < 120.0f)     { r = val1;   g = 1.0f;   b = 0.0f; } 
            else if (angle >= 120.0f && angle < 180.0f)    { r = 0.5f;   g = 0.5f;   b = 0.5f; } 
            else if (angle >= 180.0f && angle < 240.0f)    { r = 1.0f;   g = 0.0f;   b = 0.0f; } 
            else if (angle >= 240.0f && angle < 300.0f)    { r = val1;   g = 0.0f;   b = 1.0f; } 
            else                                           { r = 1.0f;   g = 0.0f;   b = val1; }

            if (angle >= 120.0f && angle < 180.0f) 
            {
                float t = (angle - 120.0f) / 60.0f;
                r = r + t * (1.0f - r); 
                g = g + t * (0.0f - g); 
                b = b + t * (0.0f - b); 
            }
            else if (angle >= 180.0f && angle < 240.0f) 
            {
                if (v < 0.5) 
                {
                    r = 1.0f + (r - 1.0f) * (2.0f * v);
                    g = 0.5f + (g - 0.5f) * (2.0f * v);
                    b = 0.0f + (b - 0.0f) * (2.0f * v);
                } 
                else 
                {
                    r = r + (1.0f - r) * (2.0f * v - 1.0f);
                    g = g + (0.0f - g) * (2.0f * v - 1.0f);
                    b = b + (0.5f - b) * (2.0f * v - 1.0f);
                }
            }
            
            if (angle >= 180.0f && angle < 210.0f)
            {
                float t = 1.0f - (angle - 180.0f) / 30.0f;
                r = r + t * (0.75f - r); 
                g = g + t * (0.75f - g); 
                b = b + t * (0.75f - b);
            }
            else if (angle >= 210.0f && angle < 240.0f)
            {
                float t = (angle - 210.0f) / 30.0f;
                r = r + t * (0.5f - r); 
                g = g + t * (0.5f - g); 
                b = b + t * (0.5f - b);
            }
            else if (v < 0.5)
            {
                r = 1.0f + (r - 1.0f) * (2.0f * v);
                g = 1.0f + (g - 1.0f) * (2.0f * v);
                b = 1.0f + (b - 1.0f) * (2.0f * v);
            } 
            else 
            {
                r = r + (0.0f - r) * (2.0f * v - 1.0f);
                g = g + (0.0f - g) * (2.0f * v - 1.0f);
                b = b + (0.0f - b) * (2.0f * v - 1.0f);
            }

            _R = CLAMP(r + val2, 0.0f, 1.0f);
            _G = CLAMP(g + val2, 0.0f, 1.0f);
            _B = CLAMP(b + val2, 0.0f, 1.0f);

            rgba[i].R = (_R * 255.0f);
            rgba[i].G = (_G * 255.0f);
            rgba[i].B = (_B * 255.0f);
            rgba[i].A = 255;
        

            /*hsv.H = fmod(LERP(300.0, 420.0, (double)u), 360.0);

            if (y < 256)
            {
                hsv.S = LERP(0.0, 1.0, fmod((double)v * 3.0, 1.0));
                hsv.V = 1.0;
            }
            else if (y < 512)
            {

                hsv.S = LERP(0.0, 1.0, fmod((double)v * 3.0, 1.0));
                hsv.V = LERP(0.5, 1.0, fmod((double)v * 3.0, 1.0));
            }
            else
            {
                hsv.S = 1.0;
                hsv.V = LERP(0.0, 1.0, fmod((double)v * 3.0, 1.0));
            }

            rgba[i] = hsv_to_rgba(hsv);*/
        }
    }
}

bool* col_manip_mask_reds(Color_RGBA* rgba, bool* mask)
{
    for (int x = 0; x < IMG_WIDTH; x++)
    {
        for (int y = 0; y < IMG_HEIGHT; y++)
        {
            // Utility color-manipulation values
            int i = x + y * IMG_WIDTH;

            Color_HSV hsv = rgba_to_hsv(rgba[i]);

            float 
                r = (float)rgba[i].R / 255.0f,
                g = (float)rgba[i].G / 255.0f,
                b = (float)rgba[i].B / 255.0f;
            // Utility color-manipulation values
            
            float is_red = 1;

            float max_col = MAX(r, MAX(g, b));
            float min_col = MIN(r, MIN(g, b));

            float hue = 60.0f * (fmodf(g - b, 6.0f)) / (max_col - min_col);

            if (hue < -20.0f || hue > 12.0f)
                is_red = 0;
            else
                is_red = 1;

            if (r + (r+g+b)/5 - 0.75f < (g + b) / 2)
                is_red = 0;

            if (r / (g + b) < 0.75f)
                is_red = 0;
                
            mask[i] = is_red > 0;

            /*bool is_red = true;

            double hue = fmod(hsv.H + 180.0, 360.0) - 180.0;

            if (hue < -20.0 || hue > 5.0)
                is_red = false;

            if (hsv.S < 10.0)
                is_red = false;

            if (hsv.V + hsv.S * 0.75 < 105.0)
                is_red = false;

            if (hsv.V + hsv.S < 95.0 && hsv.V < 50.0)
                is_red = false;

            if (hsv.V < 50.0)
                is_red = false;

            mask[i] = is_red;*/
        }
    }
}

int scan_for_hotspot(bool* mask, AABB* clusters, int size)
{
    int cluster_count = 0;
    int width = 3;
    int reach = 3;

    for (int y = reach; y < IMG_HEIGHT - width - reach; y += width)
    {
        for (int x = reach; x < IMG_WIDTH - width - reach; x += width)  
        {
            // The image is scanned by getting the sum of 3x3 squares of pixels.
            // This is done to prevent rogue pixels from being detected.
            int density = 0;

            for (int yb = y; yb < y + width; yb++)
                for (int xb = x; xb < x + width; xb++)  
                    density += mask[xb + yb*IMG_WIDTH];

            if (density >= 3)
            {
                AABB box = { 
                    .w = x - reach,  
                    .n = y - reach, 
                    .e = x + reach + width, 
                    .s = y + reach + width
                };
                bool did_combine = false;

                for (int i = 0; i < cluster_count; i++)
                {
                    if (AABB_intersect(box, clusters[i]))
                    {
                        clusters[i] = AABB_combine(box, clusters[i]);
                        did_combine = true;
                        break;
                    }
                }

                if (!did_combine && cluster_count < size)
                    clusters[cluster_count++] = box;
            }
        }
    }

    // Loop through all clusters and check for new intersections caused by previous merges.
    // Repeat until no new intersections are found.
    bool doContinue = true;
    while (doContinue)
    {
        doContinue = false;
        for (int i = 0; i < cluster_count; i++)
        {
            for (int j = 0; j < cluster_count; j++)
            {
                if (i == j) continue;

                if (AABB_intersect(clusters[i], clusters[j]))
                {
                    doContinue = true;

                    clusters[i] = AABB_combine(clusters[i], clusters[j]);
                    
                    for (int n = j + 1; n < cluster_count; n++)
                        clusters[n-1] = clusters[n];
                    cluster_count--;
                }
            }
        }
    }

    /*AABB largest_box;
    int largest_area = 0;

    for (int i = 0; i < cluster_count; i++)
    {
        AABB current_box = found_clusters[i];

        int current_area = (current_box.e - current_box.w) 
                         * (current_box.s - current_box.n);

        if (current_area > largest_area)
        {
            largest_box = current_box;
            largest_area = current_area;
        }
    }*/
    
    return cluster_count;
}

void col_manip_add_circle(Color_RGBA* rgba, int x, int y, int r, int w)
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

            rgba[i].R = 255;
            rgba[i].G = 0;
            rgba[i].B = 0;
        }
    }
}

void col_manip_add_rect(Color_RGBA* rgba, AABB box, Color_RGBA fill_color)
{
    if (box.n >= 0 && box.n < IMG_HEIGHT)
        for (int x = box.w; x < box.e; x++)
        {
            if (x < 0 || x >= IMG_WIDTH)
                continue;

            int i = x + box.n * IMG_WIDTH;

            rgba[i] = fill_color;
        }

    if (box.s >= 0 && box.s < IMG_HEIGHT)
        for (int x = box.w; x < box.e; x++)
        {
            if (x < 0 || x >= IMG_WIDTH)
                continue;

            int i = x + box.s * IMG_WIDTH;

            rgba[i] = fill_color;
        }

    if (box.w >= 0 && box.w < IMG_WIDTH)
        for (int y = box.n; y < box.s; y++)
        {
            if (y < 0 || y >= IMG_HEIGHT)
                continue;

            int i = box.w + y * IMG_WIDTH;

            rgba[i] = fill_color;
        }

    if (box.e >= 0 && box.e < IMG_WIDTH)
        for (int y = box.n; y < box.s + 1; y++)
        {
            if (y < 0 || y >= IMG_HEIGHT)
                continue;

            int i = box.e + y * IMG_WIDTH;

            rgba[i] = fill_color;
        }
}

void apply_color_manipulation(Color_RGBA* rgba)
{
#ifdef COL_MANIP_OVERWRITE_CAM
    col_manip_overwrite_cam(rgba);
#endif      

#ifdef COL_MANIP_MASK_REDS
    bool red_mask[IMG_SIZE];
    
    col_manip_mask_reds(rgba, red_mask);

    for (int i = 0; i < IMG_HEIGHT; i++)
        for (int j = 0; j < IMG_WIDTH; j++)
            {
                bool is_red = red_mask[j + i * IMG_WIDTH];

                rgba[j + i * IMG_WIDTH].R /= 1 + (1 - (int)is_red);
                rgba[j + i * IMG_WIDTH].G /= 1 + (1 - (int)is_red);
                rgba[j + i * IMG_WIDTH].B /= 1 + (1 - (int)is_red);
            }

#ifdef COL_MANIP_ADD_RECTS
    int max_boxes = 512;
    AABB boxes[max_boxes];
    int count = scan_for_hotspot(red_mask, boxes, max_boxes);

    Color_RGBA fill = {.R = 0, .G = 255, .B = 0, .A = 255};
    for (int i = 0; i < count; i++)
        col_manip_add_rect(rgba, boxes[i], fill);
#endif
#endif

#ifdef COL_MANIP_ADD_CIRCLE
    col_manip_add_circle(rgba, 320, 180, 100, 5);
#endif        
}

void process_image(Capture_Data* data)
{
    static Color_RGBA rgba[IMG_SIZE];
    
    mjpeg_to_rgba(data, rgba);

    apply_color_manipulation(rgba);

    __uint8_t file_name[] = "_.png";
    file_name[0] = data->file_id;
    stbi_write_png(file_name, 
        IMG_WIDTH, IMG_HEIGHT, 
        4, &rgba, 
        IMG_WIDTH * 4);
}


/// @brief Tells the camera device what video format to use.
int set_supported_video_format(Capture_Data* data)
{
    // Overwrite memory in format with 0.
    memset(&data->format, 0, sizeof(data->format));

    data->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    data->format.fmt.pix.width = IMG_WIDTH;
    data->format.fmt.pix.height = IMG_HEIGHT;
    data->format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    data->format.fmt.pix.field = V4L2_FIELD_NONE;

    // Set the format in the camera device file.
    if (ioctl(data->handle, VIDIOC_S_FMT, &data->format) < 0)
    {
        printf("VIDIOC_S_FMT Video format set failed!\n");
        return -1;
    }

#ifdef DEBUG
    printf("\nPixel Formats:\n");
    __uint8_t result[5] = "NULL";

    _GetName(V4L2_PIX_FMT_MJPEG, result);
    printf("Desired: %s\n", result);

    _GetName(data->format.fmt.pix.pixelformat, result);
    printf("Actual: %s\n", result);
#endif
    
    return 1;
}

/// @brief Requests the driver to allocate space for two video capture buffers.
int request_buffers(Capture_Data* data)
{
    memset(&data->buffer_request, 0, sizeof(data->buffer_request));

    // Two buffers are requested so that one can be read and the other written to.
    // A single buffer would lead to either resource contention or race conditions.
    data->buffer_request.count = 2;
    data->buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    data->buffer_request.memory = V4L2_MEMORY_MMAP;

    if (ioctl(data->handle, VIDIOC_REQBUFS, &data->buffer_request) < 0)
    {
        printf("VIDIOC_REQBUFS failed!\n");
        return -1;
    }
    return 1;
}

/// @brief Queries & maps a requested buffer at a given index.
int query_buffer(Capture_Data* data, int i)
{
    memset(&data->query_buffer, 0, sizeof(data->query_buffer));

    data->query_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    data->query_buffer.memory = V4L2_MEMORY_MMAP;
    data->query_buffer.index = i;

    if (ioctl(data->handle, VIDIOC_QUERYBUF, &data->query_buffer) < 0)
    {
        printf("VIDIOC_QUERYBUF failed at index %i!\n", i);
        return -1;
    }

    data->img_mem[i] = mmap(
        NULL,
        data->query_buffer.length,
        PROT_READ,
        MAP_SHARED,
        data->handle,
        data->query_buffer.m.offset);

    return 1;
}

/// @brief Queues a buffer at a given index to be filled by the camera device.
int queue_buffer_to_write(Capture_Data* data, int i)
{
    memset(&data->write_buffer, 0, sizeof(data->write_buffer));

    data->write_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    data->write_buffer.memory = V4L2_MEMORY_MMAP;
    data->write_buffer.index = i;

    if (ioctl(data->handle, VIDIOC_QBUF, &data->write_buffer) < 0)
    {
        printf("VIDIOC_QBUF failed at index %i!\n", i);
        return -1;
    }
    return 1;
}

/// @brief Tells the camera device to begin streaming video data to queued buffers.
int start_camera(Capture_Data* data)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(data->handle, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON failed!\n");
        return -1;
    }
    return 1;
}

/// @brief Dequeues & processes a filled buffer, then writes to file and requeues buffer.
int dequeue_buffers(Capture_Data* data)
{
    memset(&data->queue_buffer, 0, sizeof(data->queue_buffer));

    data->queue_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    data->queue_buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(data->handle, VIDIOC_DQBUF, &data->queue_buffer) < 0)
    {
        printf("VIDIOC_DQBUF failed!\n");
        //return errno;
        return -1;
    }

    process_image(data);

    if (ioctl(data->handle, VIDIOC_QBUF, &data->queue_buffer) < 0)
    {
        printf("VIDIOC_QBUF failed!\n");
        return -1;
    }

    return 1;
}


int begin_snatching(void)
{
#ifdef COL_MANIP_OVERWRITE_CAM
    Color_RGBA rgba[IMG_SIZE];
    apply_color_manipulation(rgba);

    stbi_write_png("A.png", 
        IMG_WIDTH, IMG_HEIGHT, 
        4, &rgba, 
        IMG_WIDTH * 4);
    return 1;
#endif

    Capture_Data data;
    data.handle = open("/dev/video0", O_RDWR, 0);

    if (set_supported_video_format(&data) == -1) 
        return -1;

    if (request_buffers(&data) == -1) 
        return -1;

    for (int i = 0; i < 2; i++)
        if (query_buffer(&data, i) == -1) 
            return -1;

    for (int i = 0; i < 2; i++)
        if (queue_buffer_to_write(&data, i) == -1) 
            return -1;

    printf("\nTaking picture...\n");

    data.file_id = 'A';
    if (start_camera(&data) == -1) 
        return -1;
    
    if (dequeue_buffers(&data) == -1) 
        return -1;

    return 1;
}

int main(void)
{
    printf("\n======Start============\n");

    int handlerOut = begin_snatching();

    printf("\n\nHandler Output: %i\n", handlerOut);
    printf("======Close============\n\n");
    return handlerOut;
}
