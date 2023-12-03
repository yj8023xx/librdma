
#include "mlx_wrap.h"
#include "verbs_wrap.h"
#include "log.h"

int ibv_post_send_wrapper(struct conn_context *ctx, struct ibv_qp *qp, struct ibv_send_wr *wr,
                          struct ibv_send_wr **bad_wr)
{
    update_scur_post(ctx, (struct ibv_exp_send_wr *)wr);
    return ibv_post_send(qp, wr, bad_wr);
}

int ibv_exp_post_send_wrapper(struct conn_context *ctx, struct ibv_qp *qp, struct ibv_exp_send_wr *wr,
                              struct ibv_exp_send_wr **bad_wr)
{
    update_scur_post(ctx, wr);
    return ibv_exp_post_send(qp, wr, bad_wr);
}

__attribute__((visibility("hidden")))
int update_scur_post(struct conn_context *ctx, struct ibv_exp_send_wr *wr)
{
    uint32_t idx;
    int size;
    int nreq;
    for (nreq = 0; wr; ++nreq, wr = wr->next)
    {
        idx = ctx->scur_post & (ctx->sq_wqe_cnt - 1);
        ctx->sq_wrid[idx] = wr->wr_id;
        size = sizeof(struct wqe_ctrl_seg) / 16;
        switch (wr->exp_opcode)
        {
        case IBV_EXP_WR_RDMA_READ:
        case IBV_EXP_WR_RDMA_WRITE:
        case IBV_EXP_WR_RDMA_WRITE_WITH_IMM:
            // XXX disabling validity checks for now
#if 0
		if (unlikely(exp_send_flags & IBV_EXP_SEND_WITH_CALC)) {

			if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
			    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
			    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
			    !mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
						[wr->op.calc.data_type].valid)
				return EINVAL;

			opmod = mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
							  [wr->op.calc.data_type].opmod;

		}
#endif
            size += sizeof(struct wqe_raddr_seg) / 16;
            break;

        case IBV_EXP_WR_ATOMIC_CMP_AND_SWP:
        case IBV_EXP_WR_ATOMIC_FETCH_AND_ADD:
#if 0
		if (unlikely(!qp->enable_atomics)) {
			mlx5_dbg(fp, MLX5_DBG_QP_SEND, "atomics not allowed\n");
			return EINVAL;
		}
#endif
            size += (sizeof(struct wqe_raddr_seg) + sizeof(struct wqe_atomic_seg)) / 16;
            break;
        case IBV_EXP_WR_SEND:
            // XXX disabling validity and opmod checks
#if 0
		if (unlikely(exp_send_flags & IBV_EXP_SEND_WITH_CALC)) {

			if ((uint32_t)wr->op.calc.data_size >= IBV_EXP_CALC_DATA_SIZE_NUMBER ||
			    (uint32_t)wr->op.calc.calc_op >= IBV_EXP_CALC_OP_NUMBER ||
			    (uint32_t)wr->op.calc.data_type >= IBV_EXP_CALC_DATA_TYPE_NUMBER ||
			    !mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
						[wr->op.calc.data_type].valid)
				return EINVAL;

			opmod = mlx5_calc_ops_table[wr->op.calc.data_size][wr->op.calc.calc_op]
							  [wr->op.calc.data_type].opmod;
		}
#endif
            break;

        case IBV_EXP_WR_CQE_WAIT:
        {
            size += sizeof(struct wqe_wait_en_seg) / 16;
        }
        break;

        case IBV_EXP_WR_SEND_ENABLE:
        case IBV_EXP_WR_RECV_ENABLE:
        {
            size += sizeof(struct wqe_wait_en_seg) / 16;
        }
        break;

        case IBV_EXP_WR_NOP:
            break;
        default:
            break;
        }

        for (int i = 0; i < wr->num_sge; i++)
        {
            if (wr->sg_list[i].length > 0)
            {
                size += sizeof(struct wqe_data_seg) / 16;
            }
        }

#ifdef MLX5
        DEBUG_LOG("updating scur_post #%u by %d [wqe_size:%dB].", ctx->scur_post, DIV_ROUND_UP(size * 16, SEND_WQE_BB), size * 16);
        ctx->scur_post += DIV_ROUND_UP(size * 16, SEND_WQE_BB);
#else
        DEBUG_LOG("updating scur_post #%u by %d [wqe_size:%dB].", ctx->scur_post, 1, size * 16);
        ctx->scur_post += 1; // equals sq.head
#endif
    }

    return 0;
}