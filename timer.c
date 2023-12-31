
#include "include/timer.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <omp.h>


#define TIMED_FRAMES 256


typedef struct Timer_Data
{
    double start_time;
    double stop_time;


    double frame_times[TIMED_FRAMES];
    double manipulation_times[TIMED_FRAMES];

    double conversion_times[TIMED_FRAMES];
    double t_conversion_times[TIMED_FRAMES];
    
    double scan_times[TIMED_FRAMES];
    double t_scan_times[TIMED_FRAMES];


    unsigned short frame_count;
    unsigned short manipulation_count;
    unsigned short conversion_count;
    unsigned short t_conversion_count;
    unsigned short scan_count;
    unsigned short t_scan_count;


    bool initialized;
    bool stopped;
} Timer_Data;

static Timer_Data timer;


int timer_begin_measure(enum timer_type type)
{
    if (!timer.initialized || timer.stopped)
        return -1;

    unsigned short *count;
    double *times;

    switch (type)
    {
    case FRAME:
        count = &timer.frame_count;
        times = timer.frame_times;
        break;

    case MANIPULATION:
        count = &timer.manipulation_count;
        times = timer.manipulation_times;
        break;

    case CONVERSION:
        count = &timer.conversion_count;
        times = timer.conversion_times;
        break;

    case T_CONVERSION:
        count = &timer.t_conversion_count;
        times = timer.t_conversion_times;
        break;

    case SCAN:
        count = &timer.scan_count;
        times = timer.scan_times;
        break;

    case T_SCAN:
        count = &timer.t_scan_count;
        times = timer.t_scan_times;
        break;

    default: return -1;
    }

    if (*count >= TIMED_FRAMES)
        return 1;

    times[*count] = omp_get_wtime();
    return 0;
}

int timer_end_measure(enum timer_type type)
{
    if (!timer.initialized || timer.stopped)
        return -1;

    unsigned short *count;
    double *times;

    switch (type)
    {
    case FRAME:
        count = &timer.frame_count;
        times = timer.frame_times;
        break;

    case MANIPULATION:
        count = &timer.manipulation_count;
        times = timer.manipulation_times;
        break;

    case CONVERSION:
        count = &timer.conversion_count;
        times = timer.conversion_times;
        break;

    case T_CONVERSION:
        count = &timer.t_conversion_count;
        times = timer.t_conversion_times;
        break;

    case SCAN:
        count = &timer.scan_count;
        times = timer.scan_times;
        break;

    case T_SCAN:
        count = &timer.t_scan_count;
        times = timer.t_scan_times;
        break;

    default: return -1;
    }

    if (*count >= TIMED_FRAMES)
        return 1;

    times[*count] = (omp_get_wtime() - times[*count]) * 1000.0;
    (*count)++;

    if (*count >= TIMED_FRAMES)
        printf("Timer cap reached.\n");

    return 0;
}


int timer_init()
{
    if (timer.initialized || timer.stopped)
        return -1;

    timer.initialized = true;
    timer.stopped = false;

    timer.start_time = omp_get_wtime();
    return 0;
}

int timer_quit()
{
    if (!timer.initialized || timer.stopped)
        return -1;

    timer.stop_time = omp_get_wtime();
    timer.stopped = true;
    return 0;
}

int timer_conclude()
{
    if (!timer.initialized || !timer.stopped)
        return -1;

    double tot_frame_time = 0;
    for (int i = 0; i < timer.frame_count; i++)
    {
        tot_frame_time += timer.frame_times[i];
    }
    double avg_frame_time = (float)tot_frame_time / (float)timer.frame_count;

    double tot_manipulation_time = 0;
    for (int i = 0; i < timer.manipulation_count; i++)
    {
        tot_manipulation_time += timer.manipulation_times[i];
    }
    double avg_manipulation_time = (float)tot_manipulation_time / (float)timer.manipulation_count;


    double tot_conversion_time = 0;
    for (int i = 0; i < timer.conversion_count; i++)
    {
        tot_conversion_time += timer.conversion_times[i];
    }
    double avg_conversion_time = (float)tot_conversion_time / (float)timer.conversion_count;

    double tot_t_conversion_time = 0;
    for (int i = 0; i < timer.t_conversion_count; i++)
    {
        tot_t_conversion_time += timer.t_conversion_times[i];
    }
    double avg_t_conversion_time = (float)tot_t_conversion_time / (float)timer.t_conversion_count;


    double tot_scan_time = 0;
    for (int i = 0; i < timer.scan_count; i++)
    {
        tot_scan_time += timer.scan_times[i];
    }
    double avg_scan_time = (float)tot_scan_time / (float)timer.scan_count;

    double tot_t_scan_time = 0;
    for (int i = 0; i < timer.t_scan_count; i++)
    {
        tot_t_scan_time += timer.t_scan_times[i];
    }
    double avg_t_scan_time = (float)tot_t_scan_time / (float)timer.t_scan_count;


    printf("Runtime Duration: %.0f ms\n", (float)((timer.stop_time - timer.start_time) * 1000.0));
    printf("Frames Tracked: %d\n\n", (int)timer.frame_count);
    
    printf("Avg. Frame: %.2f ms (%.2f fps)\n\n", avg_frame_time, (float)(1000.0 / avg_frame_time));

    printf("Avg. Manipulation: %.2f ms\n\n", avg_manipulation_time);

    printf("Avg. Conversion: \nst: %.3f ms\nmt: %.3f ms\n\n", avg_conversion_time, avg_t_conversion_time);

    printf("Avg. Scan: \nst: %.3f ms\nmt: %.3f ms\n\n", avg_scan_time, avg_t_scan_time);

    printf("(st = single-threaded, mt = multi-threaded)\n");
    return 0;
}
