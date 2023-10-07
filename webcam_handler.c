#include "webcam_handler.h"
#include "color_data.h"

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
#include <jpeglib.h>
#include <jerror.h>
#include <omp.h>


typedef struct V4L2_Container
{
    struct v4l2_format format;
    struct v4l2_requestbuffers buffer_request;
    struct v4l2_buffer query_buffer;
    struct v4l2_buffer write_buffer;
    struct v4l2_buffer queue_buffer;
} V4L2_Container;

Img_Format img_format;
Capture_Data capture_data;
static V4L2_Container v4l2_container;


// Boilerplate code for using jpeglib.
typedef struct 
{
    struct jpeg_source_mgr pub;

    JOCTET *buffer;
    boolean start_of_file;
} my_source_mgr;

typedef my_source_mgr * my_src_ptr;

static void jpg_memInitSource(j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr)cinfo->src;
    src->start_of_file = TRUE;
}

static boolean jpg_memFillInputBuffer(j_decompress_ptr cinfo)
{
    my_src_ptr src = (my_src_ptr)cinfo->src;
    src->start_of_file = FALSE;
    return TRUE;
}

static void jpg_memSkipInputData(j_decompress_ptr cinfo, long num_bytes)
{
    my_src_ptr src = (my_src_ptr)cinfo->src;
    if (num_bytes > 0) 
    {
        src->pub.next_input_byte += (size_t)num_bytes;
        src->pub.bytes_in_buffer -= (size_t)num_bytes;
    }
}

static void jpg_memTermSource(j_decompress_ptr cinfo) { }
// Boilerplate code for using jpeglib.


/// @brief Tells the camera device what video format to use.
int _set_supported_video_format()
{
    // Overwrite memory in format with 0.
    memset(&v4l2_container.format, 0, sizeof(v4l2_container.format));

    v4l2_container.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.format.fmt.pix.width = img_format.width;
    v4l2_container.format.fmt.pix.height = img_format.height;
    v4l2_container.format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    v4l2_container.format.fmt.pix.field = V4L2_FIELD_NONE;

    // Set the format in the camera device file.
    if (ioctl(capture_data.handle, VIDIOC_S_FMT, &v4l2_container.format) < 0)
    {
        printf("VIDIOC_S_FMT Video format set failed!\n");
        return -1;
    }

    return 0;
}

/// @brief Requests the driver to allocate space for two video capture buffers.
int _request_buffers()
{
    memset(&v4l2_container.buffer_request, 0, sizeof(v4l2_container.buffer_request));

    // Two buffers are requested so that one can be read and the other written to.
    // A single buffer would lead to either resource contention or race conditions.
    v4l2_container.buffer_request.count = 2;
    v4l2_container.buffer_request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.buffer_request.memory = V4L2_MEMORY_MMAP;

    if (ioctl(capture_data.handle, VIDIOC_REQBUFS, &v4l2_container.buffer_request) < 0)
    {
        printf("VIDIOC_REQBUFS failed!\n");
        return -1;
    }
    return 0;
}

/// @brief Queries & maps a requested buffer at a given index.
int _query_buffer(int i)
{
    memset(&v4l2_container.query_buffer, 0, sizeof(v4l2_container.query_buffer));

    v4l2_container.query_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.query_buffer.memory = V4L2_MEMORY_MMAP;
    v4l2_container.query_buffer.index = i;

    if (ioctl(capture_data.handle, VIDIOC_QUERYBUF, &v4l2_container.query_buffer) < 0)
    {
        printf("VIDIOC_QUERYBUF failed at index %i!\n", i);
        return -1;
    }

    capture_data.img_mem[i] = mmap(
        NULL,
        v4l2_container.query_buffer.length,
        PROT_READ,
        MAP_SHARED,
        capture_data.handle,
        v4l2_container.query_buffer.m.offset);

    return 0;
}

/// @brief Queues a buffer at a given index to be filled by the camera device.
int _queue_buffer_to_write(int i)
{
    memset(&v4l2_container.write_buffer, 0, sizeof(v4l2_container.write_buffer));

    v4l2_container.write_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.write_buffer.memory = V4L2_MEMORY_MMAP;
    v4l2_container.write_buffer.index = i;

    if (ioctl(capture_data.handle, VIDIOC_QBUF, &v4l2_container.write_buffer) < 0)
    {
        printf("VIDIOC_QBUF failed at index %i!\n", i);
        return -1;
    }
    return 0;
}

/// @brief Tells the camera device to begin streaming video data to queued buffers.
int _start_camera()
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(capture_data.handle, VIDIOC_STREAMON, &type) < 0)
    {
        printf("VIDIOC_STREAMON failed!\n");
        return -1;
    }
    return 0;
}

/// @brief Dequeues & processes a filled buffer.
int _dequeue_buffer()
{
    memset(&v4l2_container.queue_buffer, 0, sizeof(v4l2_container.queue_buffer));

    v4l2_container.queue_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.queue_buffer.memory = V4L2_MEMORY_MMAP;

    if (ioctl(capture_data.handle, VIDIOC_DQBUF, &v4l2_container.queue_buffer) < 0)
    {
        printf("VIDIOC_DQBUF failed!\n");
        return -1;
    }

    return 0;
}

/// @brief Requeues a dequeued buffer.
int _requeue_buffer()
{
    if (ioctl(capture_data.handle, VIDIOC_QBUF, &v4l2_container.queue_buffer) < 0)
    {
        printf("VIDIOC_QBUF failed!\n");
        return -1;
    }

    return 0;
}



int webcam_init()
{
    capture_data.handle = open("/dev/video0", O_RDWR, 0);

    if (_set_supported_video_format() == -1) 
        return -1;

    if (_request_buffers() == -1) 
        return -1;

    for (int i = 0; i < 2; i++)
        if (_query_buffer(i) == -1) 
            return -1;

    for (int i = 0; i < 2; i++)
        if (_queue_buffer_to_write(i) == -1) 
            return -1;

    return 0;
}

int next_frame()
{
    if (_start_camera() == -1) 
        return -1;

    if (_dequeue_buffer() == -1) 
        return -1;

    return 0;
}

int close_frame()
{
    if (_requeue_buffer() == -1) 
        return -1;

    return 0;
}

int mjpeg_to_rgb(RGB *rgb)
{
    unsigned char* mjpeg = capture_data.img_mem[v4l2_container.queue_buffer.index];
    int size = v4l2_container.queue_buffer.bytesused;

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
    
    src = (my_src_ptr)cinfo.src;
    src->buffer = (JOCTET*)mjpeg;

    src->pub.init_source = jpg_memInitSource;
    src->pub.fill_input_buffer = jpg_memFillInputBuffer;
    src->pub.skip_input_data = jpg_memSkipInputData;
    src->pub.resync_to_restart = jpeg_resync_to_restart;
    src->pub.term_source = jpg_memTermSource;
    src->pub.bytes_in_buffer = size;
    src->pub.next_input_byte = mjpeg;

    printf("1.");
    if (jpeg_read_header(&cinfo, FALSE) != 1)
    {
        printf("\nIt happened.\n");
        return -1;
    }
    printf("2.");
    jpeg_start_decompress(&cinfo);
    printf("3.");

    unsigned char* rgb_ptr = (unsigned char*)rgb;
    while (cinfo.output_scanline < cinfo.output_height) 
    {
        jpeg_read_scanlines(&cinfo, &rgb_ptr, 1);
        rgb_ptr += img_format.width * 3;
    }

    jpeg_finish_decompress(&cinfo); 
    jpeg_destroy_decompress(&cinfo);
    return 0;
}
