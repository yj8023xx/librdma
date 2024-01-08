#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent.h"
#include "connection.h"
#include "log.h"
#include "timer.h"
#include "utils.h"
#include "verbs_wrap.h"

#define NUM_THREAD 16
int is_server;
int is_read;
long tot_count = 0;
long timeout = 5;      // s
int data_size = 1024;  // byte
int thread_count[NUM_THREAD];

void app_on_pre_connect_cb(struct conn_context *ctx) {
  void *data_buf;
  // allocate memory
  int ret;
  ret = posix_memalign((void **)&data_buf, sysconf(_SC_PAGESIZE), MAX_MR_SIZE);
  if (ret) {
    ERROR_LOG("failed to allocate memory.");
    exit(EXIT_FAILURE);
  }

  // num_local_mrs and mr_ctxs must be set
  ctx->num_local_mrs = 1;
  ctx->mr_ctxs = (struct mr_context *)calloc(ctx->num_local_mrs,
                                             sizeof(struct mr_context));
  ctx->mr_ctxs[0].addr = data_buf;
  ctx->mr_ctxs[0].length = MAX_MR_SIZE;

  // register memory
  register_mr(ctx, ctx->num_local_mrs, ctx->mr_ctxs);

  memset(data_buf, 0, MAX_MR_SIZE);
}

void app_on_connect_cb(struct conn_context *ctx) {
  if (!is_server) {  // client side
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    sge.length = data_size;  // data size
    sge.lkey = ctx->local_mr[0]->lkey;

    long diff = 0;
    struct timespec start = timer_start();
    long timeout_us = timeout * 1e6;  // us
    while (diff <= timeout_us) {
      if (is_read) {
        post_read_sync(ctx, 1, &sge, ctx->remote_mr[0]->addr,
                       ctx->remote_mr[0]->key);  // waiting for the results
      } else {
        post_write_sync(ctx, 1, &sge, ctx->remote_mr[0]->addr,
                        ctx->remote_mr[0]->key);
      }

      thread_count[ctx->sockfd]++;
      diff = timer_end(start) / 1000;  // us
    }

    // disconnect when done
    usleep(2000);
    disconnect(ctx);
  }
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    // if no command-line arguments are provided, give a prompt
    ERROR_LOG("Please provide an argument 's' for server or 'c' for client.");
    return 0;
  } else if (argc >= 2) {
    // if there is one command-line argument
    if (strcmp(argv[1], "s") == 0) {
      is_server = 1;
      INFO_LOG("Starting the server...");
    } else if (strcmp(argv[1], "c") == 0) {
      is_server = 0;
      INFO_LOG("Starting the client...");
    } else {
      ERROR_LOG("Invalid argument: %s.", argv[1]);
      return 0;
    }
    if (!is_server) {  // client side
      is_read = 1;
      if (argc >= 3) {
        if (strcmp(argv[2], "r") == 0) {
          is_read = 1;
        } else if (strcmp(argv[2], "w") == 0) {
          is_read = 0;
        } else {
          ERROR_LOG("Invalid argument: %s.", argv[2]);
          return 0;
        }
      }
      if (is_read) {
        INFO_LOG("Test RDMA Read bandwidth.");
      } else {
        INFO_LOG("Test RDMA Write bandwidth.");
      }
    }
  } else {
    // if the number of arguments is incorrect
    ERROR_LOG("Please provide the correct number of arguments.\n");
    return 0;
  }

  if (is_server) {  // server side
    // setup server accept conn param
    struct conn_param accept_options = {
        .poll_mode = CQ_POLL_MODE_POLLING,
        .on_pre_connect_cb = app_on_pre_connect_cb,
        .on_connect_cb = app_on_connect_cb};
    struct agent_context *server = create_server(1, 1, &accept_options);

    // sockfd for listening
    char *src_addr = "10.10.10.1";
    char *port = "12345";
    struct conn_context *listen_ctx = server_listen(server, src_addr, port);

    // start listening
    start_listen(listen_ctx);
  } else {  // client side
    // create client
    struct agent_context *client = create_client(1, 1);

    char *dst_addr = "10.10.10.1";
    char *port = "12345";
    struct conn_param rc_options = {.poll_mode = CQ_POLL_MODE_POLLING,
                                    .on_pre_connect_cb = app_on_pre_connect_cb,
                                    .on_connect_cb = app_on_connect_cb};

    pthread_t thread_id[NUM_THREAD];
    for (int i = 0; i < NUM_THREAD; i++) {
      struct conn_context *rc_ctx =
          add_connection_rc(client, dst_addr, port, &rc_options);

      // connect to server
      pthread_create(&thread_id[i], NULL, start_connect, rc_ctx);
    }

    for (int i = 0; i < NUM_THREAD; i++) {
      pthread_join(thread_id[i], NULL);
    }

    for (int i = 0; i < NUM_THREAD; i++) {
      tot_count += thread_count[i];
    }

    long qps = tot_count / timeout;
    INFO_LOG("RDMA %s QPS: %lu [tot_count:%d time:%ds data_size:%dB].",
             is_read ? "Read" : "Write", qps, tot_count, timeout, data_size);

    // free resources
    destroy_agent(client);
  }

  return 0;
}