#ifndef RDMA_TIMER_H
#define RDMA_TIMER_H

struct timespec timer_start();
long timer_end(struct timespec start_time);

#endif