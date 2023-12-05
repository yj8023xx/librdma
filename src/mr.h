#ifndef RDMA_MR_H
#define RDMA_MR_H

#include "common.h"
#include "connection.h"

void register_mr(struct conn_context *ctx, int num_mrs,
                       struct mr_context *mrs);
void update_remote_mr(struct conn_context *ctx, int num_mrs, uint64_t *addr,
                      uint32_t *length, uint32_t *rkey);

#endif