#ifndef RDMA_MLX_WRAP_H
#define RDMA_MLX_WRAP_H

#include <string.h>
#include <stdint.h>
#include <mlx4.h>
#include "common.h"

#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a)-1)) == 0)

#ifdef __cplusplus
#define ALIGN(x, a) ALIGN_MASK((x), ((__typeof__(x))(a)-1))
#else
#define ALIGN(x, a) ALIGN_MASK((x), ((typeof(x))(a)-1))
#endif
#define ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))

#if __BIG_ENDIAN__
#define htonll(x) (x)
#define ntohll(x) (x)
#else
#define htonll(x) ((((uint64_t)htonl(x & 0xFFFFFFFF)) << 32) + htonl(x >> 32))
#define ntohll(x) ((((uint64_t)ntohl(x & 0xFFFFFFFF)) << 32) + ntohl(x >> 32))
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    int ibv_post_send_wrapper(struct conn_context *ctx, struct ibv_qp *qp, struct ibv_send_wr *wr,
                              struct ibv_send_wr **bad_wr);

    int ibv_exp_post_send_wrapper(struct conn_context *ctx, struct ibv_qp *qp, struct ibv_exp_send_wr *wr,
                                  struct ibv_exp_send_wr **bad_wr);

    int update_scur_post(struct conn_context *ctx, struct ibv_exp_send_wr *wr);

#ifdef MLX5
    static inline void *get_send_wqe(struct conn_context *ctx, int n)
    {
        return ctx->sq_start + (n << SEND_WQE_SHIFT);
    }
#else
    static inline void *get_send_wqe(struct conn_context *ctx, int n)
    {
        return ctx->sq_start + (n << ctx->sq_wqe_shift);
    }
#endif

    static inline uint32_t get_wr_idx(struct conn_context *ctx, int pos)
    {
        uint32_t idx = pos & (ctx->sq_wqe_cnt - 1);
        return idx;
    }

    static inline void set_atomic_seg(struct wqe_atomic_seg *aseg,
                                      enum ibv_wr_opcode opcode,
                                      uint64_t swap,
                                      uint64_t compare_add)
    {
        if (opcode == IBV_WR_ATOMIC_CMP_AND_SWP)
        {
            aseg->swap_add = htonll(swap);
            aseg->compare = htonll(compare_add);
        }
        else
        {
            aseg->swap_add = htonll(compare_add);
            aseg->compare = 0;
        }
    }

    static inline void set_raddr_seg(struct wqe_raddr_seg *rseg,
                                     uint64_t remote_addr, uint32_t rkey)
    {
        rseg->raddr = htonll(remote_addr);
        rseg->rkey = htonl(rkey);
        rseg->reserved = 0;
    }

    static inline void set_wait_en_seg(void *wqe_seg, uint32_t obj_num, uint32_t count)
    {
        struct wqe_wait_en_seg *seg = (struct wqe_wait_en_seg *)wqe_seg;

        seg->pi = htonl(count);
        seg->obj_num = htonl(obj_num);

        return;
    }

#ifdef __cplusplus
}
#endif

#endif
