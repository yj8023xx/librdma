#include "reactor.h"

/**
 * data: reactor_context
 */
void *run_reactor(void *data) {
  struct epoll_event events[EPOLL_SIZE];
  struct event_data *event_data_ptr;
  struct reactor_context *ctx = (struct reactor_context *)data;
  int i, num_events = 0;

  ctx->stop = false;
  DEBUG_LOG("running reactor.");
  while (1) {
    num_events = epoll_wait(ctx->epoll_fd, events, ARRAY_SIZE(events), -1);
    if (num_events > 0) {
      for (i = 0; i < num_events; i++) {
        event_data_ptr = (struct event_data *)events[i].data.ptr;
        event_data_ptr->event_handler(event_data_ptr->data_ptr);
      }
    }
    if (ctx->stop) {
      break;
    }
  }

  DEBUG_LOG("stopped reactor.");
  return NULL;
}

int init_reactor(struct reactor_context *ctx) {
  int ret = 0;
  ctx->epoll_fd = epoll_create(EPOLL_SIZE);
  if (ctx->epoll_fd < 0) {
    ERROR_LOG("failed to create epoll fd.");
    return -1;
  }

  ret = pthread_create(&ctx->epoll_thread, NULL, run_reactor, ctx);
  if (ret) {
    ERROR_LOG("failed to create pthread.");
    close(ctx->epoll_fd);
  }

  return ret;
}

int add_event_fd(struct reactor_context *ctx, int events, int fd, void *data,
                 event_handler event_handler) {
  struct epoll_event ee;
  struct event_data *event_data_ptr;
  int ret = 0;

  event_data_ptr = (struct event_data *)malloc(sizeof(struct event_data));
  if (!event_data_ptr) {
    ERROR_LOG("failed to allocate memory.");
    return -1;
  }

  event_data_ptr->fd = fd;
  event_data_ptr->data_ptr = data;
  event_data_ptr->event_handler = event_handler;

  memset(&ee, 0, sizeof(ee));
  ee.events = events;
  ee.data.ptr = event_data_ptr;

  ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, fd, &ee);
  if (ret) {
    ERROR_LOG("failed to add event fd.");
    free(event_data_ptr);
  }

  DEBUG_LOG("added event fd successfully.");

  return ret;
}

int del_event_fd(struct reactor_context *ctx, int fd) {
  int ret = 0;
  ret = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
  if (ret < 0) {
    ERROR_LOG("failed to del event fd.");
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("successfully deleted event fd.");

  return ret;
}