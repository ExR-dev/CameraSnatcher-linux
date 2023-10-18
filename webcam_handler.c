
#define USE_THREADS

#include "include/webcam_handler.h"
#include "include/img_data.h"

#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/videodev2.h>


typedef struct Capture_Data
{
    int handle;
    unsigned char *img_mem[2];
} Capture_Data;

static Capture_Data capture_data;


typedef struct V4L2_Container
{
    struct v4l2_format format;
    struct v4l2_requestbuffers buffer_request;
    struct v4l2_buffer query_buffer;
    struct v4l2_buffer write_buffer;
    struct v4l2_buffer queue_buffer;
} V4L2_Container;

static V4L2_Container v4l2_container;


/// @brief Tells the camera device what video format to use.
int _set_supported_video_format(const Img_Fmt *format)
{
    // Overwrite memory in format with 0.
    memset(&v4l2_container.format, 0, sizeof(v4l2_container.format));

    v4l2_container.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_container.format.fmt.pix.width = format->width;
    v4l2_container.format.fmt.pix.height = format->height;
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


int webcam_init(const Img_Fmt *format)
{
    capture_data.handle = open("/dev/video0", O_RDWR, 0);

    if (_set_supported_video_format(format) == -1) 
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

int get_frame(unsigned char **mjpeg, unsigned int *mjpeg_size)
{
    *mjpeg = capture_data.img_mem[v4l2_container.queue_buffer.index];
    *mjpeg_size = v4l2_container.queue_buffer.bytesused;
    return 0;
}

int close_frame()
{
    if (_requeue_buffer() == -1) 
        return -1;

    return 0;
}


int webcam_close(const Img_Fmt *format)
{
    if (close(capture_data.handle) == -1)
        printf("ERROR: close() returned -1.");
        
    if (munmap(capture_data.img_mem[0], v4l2_container.query_buffer.length) == -1)
        printf("ERROR: munmap() for buffer 0 returned -1.");
    if (munmap(capture_data.img_mem[1], v4l2_container.query_buffer.length) == -1)
        printf("ERROR: munmap() for buffer 1 returned -1.");

    return 0;
}
