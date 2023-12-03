#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include "timer.h"

// call this function to start a nanosecond-resolution timer
struct timespec timer_start()
{
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    return start_time;
}

// call this function to end a timer, returning nanoseconds elapsed as a long
long timer_end(struct timespec start_time)
{
    struct timespec end_time;
    long sec_diff, nsec_diff, nanoseconds_elapsed;

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    sec_diff = end_time.tv_sec - start_time.tv_sec;
    nsec_diff = end_time.tv_nsec - start_time.tv_nsec;

    if (nsec_diff < 0)
    {
        sec_diff--;
        nsec_diff += (long)1e9;
    }

    nanoseconds_elapsed = sec_diff * (long)1e9 + nsec_diff;
    return nanoseconds_elapsed;
}