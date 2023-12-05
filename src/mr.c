#include "mr.h"

#include <stdlib.h>

void register_mr(struct conn_context *ctx, int num_mrs,
                 struct mr_context *mr_ctxs) {
  DEBUG_LOG("registering %d local mrs for sockfd #%d.", num_mrs, ctx->sockfd);

  int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                     IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ;

  for (int i = 0; i < num_mrs; i++) {
    if (i > MAX_MR - 1) {
      ERROR_LOG("the number of memory region exceeds MAX_MR.");
      exit(EXIT_FAILURE);
    }

    // only register memory once
    if (!ctx->local_mr[i]) {
      ctx->local_mr[i] = ibv_reg_mr(ctx->id->pd, (void *)mr_ctxs[i].addr,
                                    mr_ctxs[i].length, access_flags);
    }

    if (!ctx->local_mr[i]) {
      ERROR_LOG("failed to register memory, errno: %s.", strerror(errno));
      exit(EXIT_FAILURE);
    }
    DEBUG_LOG("registered local_mr #%d [addr:%p len:%lu lkey:%u rkey:%u].", i,
              (uint64_t)ctx->local_mr[i]->addr, ctx->local_mr[i]->length,
              ctx->local_mr[i]->lkey, ctx->local_mr[i]->rkey);
  }
}

void update_remote_mr(struct conn_context *ctx, int num_mrs, uint64_t *addr,
                      uint32_t *length, uint32_t *rkey) {
  DEBUG_LOG("updating %d remote mrs for sockfd #%d.", num_mrs, ctx->sockfd);
  ctx->num_remote_mrs = num_mrs;
  for (int i = 0; i < num_mrs; i++) {
    ctx->remote_mr[i] = (struct mr_context *)malloc(sizeof(struct mr_context));
    ctx->remote_mr[i]->addr = addr[i];
    ctx->remote_mr[i]->length = length[i];
    ctx->remote_mr[i]->key = rkey[i];
    DEBUG_LOG(
        "updated remote mr #%d [addr:%p len:%lu rkey:%lu] for sockfd #%d.", i,
        addr[i], length[i], rkey[i], ctx->sockfd);
  }
}