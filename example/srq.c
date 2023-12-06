#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent.h"
#include "connection.h"
#include "log.h"
#include "message.h"
#include "reactor.h"
#include "timer.h"
#include "utils.h"
#include "verbs_wrap.h"

#define MAX_INSTANCE_NUM 64

struct agent_context *agents[MAX_INSTANCE_NUM];
int is_server;

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
  if (is_server) {
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    sge.length = 13;
    sge.lkey = ctx->local_mr[0]->lkey;
    post_srq_recv(ctx, 1, &sge);
  } else {
    strcpy(data_buf, "Hello World!");
  }
}

void app_on_connect_cb(struct conn_context *ctx) {
   if (!is_server) {
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    sge.length = 13;
    sge.lkey = ctx->local_mr[0]->lkey;
    post_send_sync(ctx, 1, &sge, 0);

    // disconnect when done
    usleep(2000);
    rdma_disconnect(ctx->id);
  }
}

void app_on_complete_cb(struct conn_context *ctx, struct ibv_wc *wc) {
  switch (wc->opcode) {
    case IBV_WC_SEND:
      break;
    case IBV_WC_RECV: {
      // Note: this is in the callback of listen_ctx
      if (is_server) {
        struct conn_context *server_ctx = get_connection(ctx->agent, 1);
        char *str = server_ctx->local_mr[0]->addr;
        INFO_LOG("received data: %s", str);
      }
      break;
    }
    case IBV_WC_RECV_RDMA_WITH_IMM:
      break;
    case IBV_WC_RDMA_WRITE:
      break;
    case IBV_WC_RDMA_READ:
      break;
    case IBV_WC_COMP_SWAP:
      break;
    case IBV_WC_FETCH_ADD:
      break;
    default:
      INFO_LOG("unknown opcode:%i, wr_id:%lu.", wc->opcode, wc->wr_id);
      break;
  }
}

// client side
pthread_t create_instance(int node_id, int inst_id) {
  // create client
  struct agent_context *client = create_client(node_id, inst_id);
  agents[inst_id] = client;

  char *dst_addr = "10.10.10.2";
  char *port = "12345";
  struct conn_param rc_options = {.poll_mode = CQ_POLL_MODE_POLLING,
                                  .on_pre_connect_cb = app_on_pre_connect_cb,
                                  .on_connect_cb = app_on_connect_cb,
                                  .on_complete_cb = app_on_complete_cb};
  int sockfd = add_connection_rc(client, dst_addr, port, &rc_options);
  struct conn_context *rc_ctx = get_connection(client, sockfd);

  // start run
  pthread_create(&rc_ctx->rdma_event_thread, NULL, client_loop, rc_ctx);

  return rc_ctx->rdma_event_thread;
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    // if no command-line arguments are provided, give a prompt
    ERROR_LOG("Please provide an argument 's' for server or 'c' for client.");
    return 0;
  } else if (argc == 2) {
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
  } else {
    // if the number of arguments is incorrect
    ERROR_LOG("Please provide the correct number of arguments.\n");
    return 0;
  }

  if (is_server) {  // server side
    struct agent_context *server = create_server(1, 1, NULL);

    // sockfd for listening
    char *src_addr = "10.10.10.2";
    char *port = "12345";
    int listen_fd = server_listen(server, src_addr, port);
    struct conn_context *listen_ctx = get_connection(server, listen_fd);
    struct rdma_event_channel *listen_channel = listen_ctx->id->channel;

    // use the listen_fd to create srq since it is already setup
    build_srq(listen_ctx);
    // create srq_cq
    build_cq_channel(listen_ctx);
    // setup complete callback for srq_cq
    listen_ctx->on_complete_cb = app_on_complete_cb;

    // setup server accept conn param
    struct conn_param accept_options = {
        .poll_mode = CQ_POLL_MODE_POLLING,
        .srq_flag = SRQ_ENABLE,
        // if srq_flag==SRQ_ENABLE, srq and srq_cq must be set
        .srq = listen_ctx->id->srq,
        .srq_cq = listen_ctx->cq,
        .on_pre_connect_cb = app_on_pre_connect_cb,
        .on_connect_cb = app_on_connect_cb,
        .on_complete_cb = app_on_complete_cb};
    // setup conn param
    server->options = &accept_options;

    pthread_create(&listen_ctx->rdma_event_thread, NULL, server_loop,
                   listen_ctx);
    pthread_join(listen_ctx->rdma_event_thread, NULL);

    // free resourses
    destroy_agent(server);
  } else {  // client side
    int num_instances = 1;
    pthread_t thread[num_instances];
    int node_id = 1;

    // create client instance
    for (int i = 0; i < num_instances; i++) {
      thread[i] = create_instance(node_id, i);
    }

    for (int i = 0; i < num_instances; i++) {
      pthread_join(thread[i], NULL);
    }

    // free resources
    for (int i = 0; i < num_instances; i++) {
      destroy_agent(agents[i]);
    }
  }

  return 0;
}