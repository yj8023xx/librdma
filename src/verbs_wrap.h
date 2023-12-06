#ifndef RDMA_VERBS_WRAP_H
#define RDMA_VERBS_WRAP_H

#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>
#include <limits.h>
#include <rdma/rdma_cma.h>

#include "common.h"

static inline char *opcode_to_str(int opcode) {
  switch (opcode) {
    case IBV_WR_RDMA_WRITE:
      return "IBV_WR_RDMA_WRITE";
      break;
    case IBV_WR_RDMA_WRITE_WITH_IMM:
      return "IBV_WR_RDMA_WRITE_WITH_IMM";
      break;
    case IBV_WR_SEND:
      return "IBV_WR_SEND";
      break;
    case IBV_WR_SEND_WITH_IMM:
      return "IBV_WR_SEND_WITH_IMM";
      break;
    case IBV_WR_RDMA_READ:
      return "IBV_WR_RDMA_READ";
      break;
    case IBV_WR_ATOMIC_FETCH_AND_ADD:
      return "IBV_WR_ATOMIC_FETCH_AND_ADD";
      break;
    case IBV_WR_ATOMIC_CMP_AND_SWP:
      return "IBV_WR_ATOMIC_CMP_AND_SWP";
      break;
    default:
      return "UNDEFINED";
  }
}

static inline char *wc_opcode_to_str(int opcode) {
  switch (opcode) {
    case IBV_WC_SEND:
      return "IBV_WC_SEND";
      break;
    case IBV_WC_RDMA_WRITE:
      return "IBV_WC_RDMA_WRITE";
      break;
    case IBV_WC_RDMA_READ:
      return "IBV_WC_RDMA_READ";
      break;
    case IBV_WC_COMP_SWAP:
      return "IBV_WC_COMP_SWAP";
      break;
    case IBV_WC_FETCH_ADD:
      return "IBV_WC_FETCH_ADD";
      break;
    case IBV_WC_BIND_MW:
      return "IBV_WC_BIND_MW";
      break;
    case IBV_WC_LOCAL_INV:
      return "IBV_WC_LOCAL_INV";
      break;
    default:
      return "UNDEFINED";
  }
}

static inline int op_one_sided(int opcode) {
  if ((opcode == IBV_WR_RDMA_READ) || (opcode == IBV_WR_RDMA_WRITE) ||
      (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)) {
    return 1;
  } else {
    return 0;
  }
}

// increments last work request id for a specified connection
// send == 0 --> wr type is receive
// send == 1 --> wr type is send
static inline uint32_t next_wr_id(struct conn_context *ctx, int send) {
  // we maintain seperate wr_ids for send/recv queues since
  // there is no ordering between their work requests
  if (send) {
    return __sync_add_and_fetch(&ctx->last_send, 0x00000001);
  } else {
    return __sync_add_and_fetch(&ctx->last_recv, 0x00000001);
  }
  return 0;
}

// basic post operations
uint32_t post_send_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list, uint32_t imm);
void post_send_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list, uint32_t imm);
uint32_t post_send_ud_async(struct conn_context *ctx, int num_sge,
                            struct ibv_sge *sg_list);
void post_send_ud_sync(struct conn_context *ctx, int num_sge,
                       struct ibv_sge *sg_list);

uint32_t post_wr_async(struct conn_context *ctx, struct ibv_send_wr *wr);
void post_wr_sync(struct conn_context *ctx, struct ibv_send_wr *wr);

uint32_t post_recv_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list);
void post_recv_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list);
uint32_t post_srq_recv(struct conn_context *ctx, int num_sge,
                       struct ibv_sge *sg_list, uint32_t wr_id);

uint32_t post_read_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list, uint64_t remote_addr,
                         uint32_t rkey);
void post_read_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list, uint64_t remote_addr,
                    uint32_t rkey);
uint32_t post_write_async(struct conn_context *ctx, int num_sge,
                          struct ibv_sge *sg_list, uint64_t remote_addr,
                          uint32_t rkey);
void post_write_sync(struct conn_context *ctx, int num_sge,
                     struct ibv_sge *sg_list, uint64_t remote_addr,
                     uint32_t rkey);

uint32_t post_cas_async(struct conn_context *ctx, uint64_t local_addr,
                        uint32_t lkey, uint64_t remote_addr, uint32_t rkey,
                        uint64_t compare_add, uint64_t swap, int fence);
void post_cas_sync(struct conn_context *ctx, uint64_t local_addr, uint32_t lkey,
                   uint64_t remote_addr, uint32_t rkey, uint64_t compare_add,
                   uint64_t swap, int fence);
uint32_t post_fetch_add_async(struct conn_context *ctx, uint64_t local_addr,
                              uint32_t lkey, uint64_t remote_addr,
                              uint32_t rkey, uint64_t compare_add, int fence);
void post_fetch_add_sync(struct conn_context *ctx, uint64_t local_addr,
                         uint32_t lkey, uint64_t remote_addr, uint32_t rkey,
                         uint64_t compare_add, int fence);

uint32_t post_noop_async(struct conn_context *ctx, int signaled);
void post_noop_sync(struct conn_context *ctx, int signaled);

#endif
