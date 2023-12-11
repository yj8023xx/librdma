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
int done = 0;

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
  if (!is_server) {
    // pre-post
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    // the receiver must allow enough space in the receive buffer for the GRH
    // for ud mode
    sge.length = 13 + sizeof(struct ibv_grh);
    sge.lkey = ctx->local_mr[0]->lkey;
    post_recv_async(ctx, 1, &sge);
  } else {
    strcpy(data_buf, "Hello World!");
  }
}

void app_on_connect_cb(struct conn_context *ctx) {
  if (is_server) {
    struct ibv_sge sge;
    sge.addr = ctx->local_mr[0]->addr;
    sge.length = 13;
    sge.lkey = ctx->local_mr[0]->lkey;
    post_send_ud_sync(ctx, 1, &sge);
  }
}

void app_on_complete_cb(struct conn_context *ctx, struct ibv_wc *wc) {
  switch (wc->opcode) {
    case IBV_WC_SEND:
      break;
    case IBV_WC_RECV: {
      if (!is_server) {  // client side
        char *str = ctx->local_mr[0]->addr + sizeof(struct ibv_grh);
        INFO_LOG("received data: %s [node_id:%d agent_id:%d].", str,
                 ctx->agent->node_id, ctx->agent->agent_id);
        done = 1;
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
    // setup server accept conn param
    struct agent_context *server = create_server(1, 1, NULL);

    // sockfd for multicast
    char *mcast_addr = "10.10.10.2";
    struct conn_param mcast_options = {
        .poll_mode = CQ_POLL_MODE_POLLING,
        .on_pre_connect_cb = app_on_pre_connect_cb,
        .on_connect_cb = app_on_connect_cb,
        .on_complete_cb = app_on_complete_cb};
    struct conn_context *mcast_ctx = add_connection_ud(
        server, NULL, mcast_addr, MCAST_SENDER, &mcast_options);

    // start run
    join_multicast_group(mcast_ctx);

    usleep(2000);

    // leave multicast group
    leave_multicast_group(mcast_ctx);

    // free resourses
    destroy_agent(server);
  } else {  // client side
    // create client
    struct agent_context *client = create_client(1, 1);

    char *mcast_addr = "10.10.10.2";
    struct conn_param mcast_options = {
        .poll_mode = CQ_POLL_MODE_POLLING,
        .on_pre_connect_cb = app_on_pre_connect_cb,
        .on_connect_cb = app_on_connect_cb,
        .on_complete_cb = app_on_complete_cb};
    struct conn_context *mcast_ctx = add_connection_ud(
        client, NULL, mcast_addr, MCAST_RECEIVER, &mcast_options);

    // start run
    join_multicast_group(mcast_ctx);

    // wait for results
    while (!done) {
      // wait
    }

    // leave multicast group
    leave_multicast_group(mcast_ctx);

    // free resources
    destroy_agent(client);
  }

  return 0;
}
