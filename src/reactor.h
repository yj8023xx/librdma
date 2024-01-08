#ifndef RDMA_EVENT_LISTENER_H
#define RDMA_EVENT_LISTENER_H

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "common.h"

#define EPOLL_SIZE 1024
#define EVENT_SIZE 1024

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

typedef void (*event_handler)(void *data_ptr);

struct event_data {
  int fd;
  void *data_ptr;
  event_handler event_handler;
};

struct reactor_context {
  int epoll_fd;
  int *event_bitmap;
  struct event_data **event_map;
  bool stop;
  pthread_t epoll_thread;
  pthread_mutex_t mu;
};

/**
 * init reactor
 */
int init_reactor(struct reactor_context *ctx);

/**
 * destroy reactor
*/
void destroy_reactor(struct reactor_context *ctx);

/**
 * run reactor
 */
void *run_reactor(void *data);

/**
 * add epoll event
 */
int add_event_fd(struct reactor_context *ctx, int events, int fd,
                 void *data_ptr, event_handler event_handler);

/**
 * del epoll event
 */
int del_event_fd(struct reactor_context *ctx, int fd);

#endif