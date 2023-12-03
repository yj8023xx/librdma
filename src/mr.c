#include <stdlib.h>
#include "mr.h"
#include "connection.h"
#include "log.h"

void register_local_mr(struct conn_context *ctx, int num_mrs, struct mr_context *mr_ctxs)
{
    DEBUG_LOG("registering %d local mrs for sockfd #%d.", num_mrs, ctx->sockfd);

    int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ;

    for (int i = 0; i < num_mrs; i++)
    {
        if (i > MAX_MR - 1)
        {
            ERROR_LOG("the number of memory region exceeds MAX_MR.");
            exit(EXIT_FAILURE);
        }

        // only register memory once
        if (!ctx->local_mr[i])
        {
            if (mr_ctxs[i].physical)
            {
#ifdef EXP_VERBS
                DEBUG_LOG("mr #%d set as physical memory.", i);
                struct ibv_exp_reg_mr_in in = {0};
                in.pd = ctx->id->pd;

                in.addr = (void *)mr_ctxs[i].addr;
                in.length = mr_ctxs[i].length;

                in.exp_access = (access_flags | IBV_EXP_ACCESS_PHYSICAL_ADDR);
                ctx->local_mr[i] = ibv_exp_reg_mr(&in);
#else
                ERROR_LOG("physical MRs not supported.");
                exit(EXIT_FAILURE);
#endif
            }
            else
            {
                ctx->local_mr[i] = ibv_reg_mr(ctx->id->pd, (void *)mr_ctxs[i].addr, mr_ctxs[i].length, access_flags);
            }
        }

        if (!ctx->local_mr[i])
        {
            ERROR_LOG("failed to register memory, errno: %s.", strerror(errno));
            exit(EXIT_FAILURE);
        }
        DEBUG_LOG("registered local_mr #%d [addr:%p len:%lu lkey:%u rkey:%u].", i,
                  (uint64_t)ctx->local_mr[i]->addr, ctx->local_mr[i]->length,
                  ctx->local_mr[i]->lkey, ctx->local_mr[i]->rkey);
    }
}

void register_msg_mr(struct conn_context *ctx, int num_mrs)
{
    DEBUG_LOG("registering %d send/recv mrs for sockfd #%d.", num_mrs, ctx->sockfd);

    int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ;

    /* the receiver must allow enough space in the receive buffer for the GRH for ud mode */
    // int buf_size = sizeof(struct message) + sizeof(struct ibv_grh);
    int buf_size = MAX_MSG_SIZE;

    for (int i = 0; i < num_mrs; i++)
    {
        if (posix_memalign((void **)&ctx->send_buf[i], sysconf(_SC_PAGESIZE), buf_size))
        {
            ERROR_LOG("failed to allocate memory.");
            exit(EXIT_FAILURE);
        }

        ctx->send_mr[i] = ibv_reg_mr(ctx->id->pd, ctx->send_buf[i], buf_size, access_flags);

        if (posix_memalign((void **)&ctx->recv_buf[i], sysconf(_SC_PAGESIZE), buf_size))
        {
            ERROR_LOG("failed to allocate memory.");
            exit(EXIT_FAILURE);
        }

        ctx->recv_mr[i] = ibv_reg_mr(ctx->id->pd, ctx->recv_buf[i], buf_size, access_flags);
    }
}

void register_wq(struct conn_context *ctx)
{
    struct rdma_cm_id *wq_id = ctx->id;

    ctx->wq_mr = ibv_reg_mr(wq_id->pd, ctx->sq_start, (ctx->sq_wqe_cnt * ctx->sq_stride),
                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_READ);

    if (!ctx->wq_mr)
    {
        ERROR_LOG("failed to register memory, errno: %s.", strerror(errno));
        exit(EXIT_FAILURE);
    }

    DEBUG_LOG("registered wq_mr [addr:%p len:%lx rkey:%u lkey:%u].",
              (uint64_t)ctx->wq_mr->addr, ctx->wq_mr->length, ctx->wq_mr->rkey, ctx->wq_mr->lkey);
}

void update_remote_mr(struct conn_context *ctx, int num_mrs, uint64_t *addr, uint32_t *length, uint32_t *rkey)
{
    DEBUG_LOG("updating %d remote mrs for sockfd #%d.", num_mrs, ctx->sockfd);
    ctx->num_remote_mrs = num_mrs;
    for (int i = 0; i < num_mrs; i++)
    {
        ctx->remote_mr[i] = (struct mr_context *)malloc(sizeof(struct mr_context));
        ctx->remote_mr[i]->addr = addr[i];
        ctx->remote_mr[i]->length = length[i];
        ctx->remote_mr[i]->rkey = rkey[i];
        DEBUG_LOG("updated remote mr #%d [addr:%p len:%lu rkey:%lu] for sockfd #%d.", i, addr[i], length[i], rkey[i], ctx->sockfd);
    }
}