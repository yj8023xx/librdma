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
    sge.length = 13; // the length of "Hello World!"
    sge.lkey = ctx->local_mr[0]->lkey;
    // use sockfd as the wr_id in order to get the corresponding connection
    // later
    post_srq_recv(ctx, 1, &sge, ctx->sockfd);
  } else {
    strcpy(data_buf, "Hello World!");
  }
}

void app_on_connect_cb(struct conn_context *ctx) {
  if (!is_server) {
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    sge.length = 13; // the length of "Hello World!"
    sge.lkey = ctx->local_mr[0]->lkey;
    post_send_sync(ctx, 1, &sge, 0);

    // disconnect when done
    usleep(2000);
    disconnect(ctx);
  }
}

void app_on_complete_cb(struct conn_context *ctx, struct ibv_wc *wc) {
  switch (wc->opcode) {
    case IBV_WC_SEND:
      break;
    case IBV_WC_RECV: {
      if (is_server) {
        // Note: this is in the callback of listen_ctx
        // get the corresponding connection
        struct conn_context *server_ctx = get_connection(ctx->agent, wc->wr_id);
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

    start_listen(listen_ctx);
  } else {  // client side
    // create client
    struct agent_context *client = create_client(1, 1);

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

    // waiting for thread to finish executing
    pthread_join(rc_ctx->rdma_event_thread, NULL);

    // free resources
    destroy_agent(client);
  }

  return 0;
}
