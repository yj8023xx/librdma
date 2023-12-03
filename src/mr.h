#ifndef RDMA_MR_H
#define RDMA_MR_H

#include "common.h"
#include "connection.h"

void register_local_mr(struct conn_context *ctx, int num_mrs, struct mr_context *mrs);
void register_msg_mr(struct conn_context *ctx, int num_mrs);
void register_wq(struct conn_context *ctx);
void update_remote_mr(struct conn_context *ctx, int num_mrs, uint64_t *addr, uint32_t *length, uint32_t *rkey);

#endif