#ifndef RDMA_CONNECTION_H
#define RDMA_CONNECTION_H

#define __USE_XOPEN2K

#include <pthread.h>
#include <netdb.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "common.h"

#ifdef MLX5
#include <mlx5dv.h>
#include <mlx5.h>
#else
#include <mlx4.h>
#endif

#ifdef EXP_VERBS
#include <infiniband/verbs_exp.h>
#endif

enum rc_connection_state
{
    RC_CONNECTION_TERMINATED,
    RC_CONNECTION_READY
};

static inline char *qp_type_to_str(int qp_type)
{
    switch (qp_type)
    {
    case IBV_QPT_RC:
        return "IBV_QPT_RC";
        break;
    case IBV_QPT_UC:
        return "IBV_QPT_UC";
        break;
    case IBV_QPT_UD:
        return "IBV_QPT_UD";
        break;
    default:
        return "UNDEFINED";
    }
}

// connection management
void init_connection(struct agent_context *agent, struct rdma_cm_id *id, int sockfd, struct conn_param *options);
int setup_connection(struct conn_context *ctx);

// build cq, qp, srq
void build_cq_channel(struct conn_context *ctx);
#ifdef EXP_VERBS
void build_qp_attr(struct conn_context *ctx, struct ibv_exp_qp_init_attr *qp_attr);
#else
void build_qp_attr(struct conn_context *ctx, struct ibv_qp_init_attr *qp_attr);
#endif
void build_qp(struct conn_context *ctx);
void build_srq(struct conn_context *ctx);

void build_private_data(struct conn_context *ctx, struct private_data *data);
void build_rc_param(struct conn_context *ctx, struct rdma_conn_param *param, struct private_data *data);
int setup_rc_param(struct conn_context *ctx, struct rdma_conn_param *cm_params);

// connect event handling
void rdma_event_loop(struct conn_context *ctx, int exit_on_handle, int exit_on_connect, int exit_on_disconnect);

// request completions
void update_completions(struct conn_context *ctx, struct ibv_wc *wc);
void spin_till_completion(struct conn_context *ctx, uint32_t wr_id, int send);
void poll_cq(struct conn_context *ctx, struct ibv_wc *wc);
void *poll_cq_loop(void *data);

// event handler
void event_channel_handler(void *data);
void comp_channel_handler(void *data);

// helper functions
#ifdef EXP_VERBS
void query_device_cap(struct ibv_context *verbs);
#endif
struct conn_context *find_first_connection(struct agent_context *agent);
struct conn_context *find_next_connection(struct conn_context *ctx);
struct conn_context *get_connection(struct agent_context *agent, int sockfd);
int get_connection_count(struct agent_context *agent);
int get_next_connection(struct agent_context *agent, int cur);
char *get_connection_ip(struct agent_context *agent, int sockfd);
int get_connection_qpn(struct agent_context *agent, int sockfd);
uint32_t get_last_compl_wr_id(struct conn_context *ctx, int send);
void bind_fd_to_qp(struct agent_context *agent, int qpn, int sockfd);
int get_fd_by_qp(struct agent_context *agent, int qpn);
void bind_data_to_wr(struct conn_context *ctx, int wr_id, void *data);
void *get_data_by_wr(struct conn_context *ctx, int wr_id);
void release_entry(struct conn_context *ctx, int wr_id);

// state
void set_conn_state(struct conn_context *ctx, int new_state);
int is_ready(struct agent_context *agent, int sockfd);
int is_terminated(struct agent_context *agent, int sockfd);

// remove
void destroy_connection(struct conn_context *ctx);

#endif