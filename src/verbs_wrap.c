#include "verbs_wrap.h"

#include "connection.h"

//----- basic post operations ------
uint32_t post_send_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list, uint32_t imm) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;

  /* prepare the send work request */
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = sg_list;
  sr.num_sge = num_sge;
  sr.send_flags = IBV_SEND_SIGNALED;

  if (imm) {
    sr.imm_data = htonl(imm);
    sr.opcode = IBV_WR_SEND_WITH_IMM;
  } else {
    sr.opcode = IBV_WR_SEND;
  }

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> SEND [wr_id:%lu qp_num:%u sockfd:%d].", sr.wr_id,
            ctx->id->qp->qp_num, ctx->sockfd);

  return sr.wr_id;
}

void post_send_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list, uint32_t imm) {
  uint32_t wr_id = post_send_async(ctx, num_sge, sg_list, imm);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_send_ud_async(struct conn_context *ctx, int num_sge,
                            struct ibv_sge *sg_list) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;

  /* prepare the send work request */
  /* Multicast requires that the message is sent with immediate data
   * and that the QP number is the contents of the immediate data */
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = sg_list;
  sr.num_sge = num_sge;
  sr.send_flags = IBV_SEND_SIGNALED;
  sr.imm_data = htonl(ctx->id->qp->qp_num);
  sr.opcode = IBV_WR_SEND_WITH_IMM;

  sr.wr.ud.ah = ctx->ah;
  sr.wr.ud.remote_qpn = ctx->remote_qpn;
  sr.wr.ud.remote_qkey = ctx->remote_qkey;

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG(
      "POST --> SEND UD [wr_id:%lu qp_num:%u sockfd:%d remote_qpn:%u "
      "remote_qkey:%u].",
      sr.wr_id, ctx->id->qp->qp_num, ctx->sockfd, ctx->remote_qpn,
      ctx->remote_qkey);

  return sr.wr_id;
}

void post_send_ud_sync(struct conn_context *ctx, int num_sge,
                       struct ibv_sge *sg_list) {
  uint32_t wr_id = post_send_ud_async(ctx, num_sge, sg_list);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_wr_async(struct conn_context *ctx, struct ibv_send_wr *wr) {
  int ret;
  struct ibv_send_wr *bad_wr;
  struct ibv_send_wr *cur_wr;
  uint32_t last_wr_id;
  int cnt = 0;

  cur_wr = wr;

  do {
    last_wr_id = cur_wr->wr_id;
    ctx->n_posted_ops++;
    cur_wr = cur_wr->next;
    cnt++;
  } while (cur_wr && cur_wr != wr);  // prevent loops

  ret = ibv_post_send(ctx->id->qp, wr, &bad_wr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> %d WRs [last_wr_id:%u qp_num:%u sockfd:%d].", cnt,
            last_wr_id, ctx->id->qp->qp_num, ctx->sockfd);

  return last_wr_id;
}

void post_wr_sync(struct conn_context *ctx, struct ibv_send_wr *wr) {
  uint32_t wr_id = post_wr_async(ctx, wr);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_recv_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list) {
  int ret;
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *bad_wr;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = next_wr_id(ctx, 0);
  wr.sg_list = sg_list;
  wr.num_sge = num_sge;

  if (wr.num_sge == 0 || wr.num_sge > MAX_RECV_SGE) {
    ERROR_LOG("invalid number of sge entries for receive.");
    exit(EXIT_FAILURE);
  }

  ret = ibv_post_recv(ctx->id->qp, &wr, &bad_wr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> RECV [wr_id:%lu qp_num:%u sockfd:%d].", wr.wr_id,
            ctx->id->qp->qp_num, ctx->sockfd);

  return wr.wr_id;
}

void post_recv_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list) {
  uint32_t wr_id = post_recv_async(ctx, num_sge, sg_list);
  spin_till_completion(ctx, wr_id, 0);
}

uint32_t post_srq_recv(struct conn_context *ctx, int num_sge,
                       struct ibv_sge *sg_list) {
  int ret;
  struct ibv_recv_wr wr;
  struct ibv_recv_wr *bad_wr;

  memset(&wr, 0, sizeof(wr));

  wr.wr_id = next_wr_id(ctx, 0);
  wr.sg_list = sg_list;
  wr.num_sge = num_sge;

  if (wr.num_sge == 0 || wr.num_sge > MAX_RECV_SGE) {
    ERROR_LOG("invalid number of sge entries for receive.");
    exit(EXIT_FAILURE);
  }

  ret = ibv_post_srq_recv(ctx->srq, &wr, &bad_wr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> SRQ RECV [wr_id:%lu sockfd:%d].", wr.wr_id, ctx->sockfd);

  return wr.wr_id;
}

uint32_t post_read_async(struct conn_context *ctx, int num_sge,
                         struct ibv_sge *sg_list, uint64_t remote_addr,
                         uint32_t rkey) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;
  uint32_t sr_id;

  // prepare the send work request
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = sg_list;
  sr.num_sge = num_sge;

  sr.wr.rdma.remote_addr = remote_addr;
  sr.wr.rdma.rkey = rkey;
  sr.send_flags = IBV_SEND_SIGNALED;

  sr.opcode = IBV_WR_RDMA_READ;

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  sr_id = sr.wr_id;
  DEBUG_LOG("POST --> READ [wr_id:%lu remote_addr:%p qp_num:%u sockfd:%d].",
            sr_id, remote_addr, ctx->id->qp->qp_num, ctx->sockfd);

  return sr_id;
}

void post_read_sync(struct conn_context *ctx, int num_sge,
                    struct ibv_sge *sg_list, uint64_t remote_addr,
                    uint32_t rkey) {
  uint32_t wr_id = post_read_async(ctx, num_sge, sg_list, remote_addr, rkey);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_write_async(struct conn_context *ctx, int num_sge,
                          struct ibv_sge *sg_list, uint64_t remote_addr,
                          uint32_t rkey) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;

  // prepare the send work request
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = sg_list;
  sr.num_sge = num_sge;

  sr.wr.rdma.remote_addr = remote_addr;
  sr.wr.rdma.rkey = rkey;
  sr.send_flags = IBV_SEND_SIGNALED;

  sr.opcode = IBV_WR_RDMA_WRITE;

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> WRITE [wr_id:%lu remote_addr:%p qp_num:%u sockfd:%d].",
            sr.wr_id, remote_addr, ctx->id->qp->qp_num, ctx->sockfd);

  return sr.wr_id;
}

void post_write_sync(struct conn_context *ctx, int num_sge,
                     struct ibv_sge *sg_list, uint64_t remote_addr,
                     uint32_t rkey) {
  uint32_t wr_id = post_write_async(ctx, num_sge, sg_list, remote_addr, rkey);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_cas_async(struct conn_context *ctx, uint64_t local_addr,
                        uint32_t lkey, uint64_t remote_addr, uint32_t rkey,
                        uint64_t compare_add, uint64_t swap, int fence) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;
  struct ibv_sge sge;

  memset(&sge, 0, sizeof(struct ibv_sge));
  sge.addr = local_addr;
  sge.length = sizeof(uint64_t);
  sge.lkey = lkey;

  // prepare the send work request
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = &sge;
  sr.num_sge = 1;

  sr.wr.atomic.compare_add = compare_add;
  sr.wr.atomic.swap = swap;
  sr.wr.atomic.remote_addr = remote_addr;
  sr.wr.atomic.rkey = rkey;
  sr.send_flags = IBV_SEND_SIGNALED;

  if (fence) {
    sr.send_flags |= IBV_SEND_FENCE;
  }

  sr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG(
      "POST --> CAS [wr_id:%lu qp_num:%u sockfd:%d local_addr:%p "
      "remote_addr:%p swap:%lu].",
      sr.wr_id, ctx->id->qp->qp_num, ctx->sockfd, local_addr, remote_addr,
      swap);

  return sr.wr_id;
}

void post_cas_sync(struct conn_context *ctx, uint64_t local_addr, uint32_t lkey,
                   uint64_t remote_addr, uint32_t rkey, uint64_t compare_add,
                   uint64_t swap, int fence) {
  uint32_t wr_id = post_cas_async(ctx, local_addr, lkey, remote_addr, rkey,
                                  compare_add, swap, fence);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_fetch_add_async(struct conn_context *ctx, uint64_t local_addr,
                              uint32_t lkey, uint64_t remote_addr,
                              uint32_t rkey, uint64_t compare_add, int fence) {
  int ret = 0;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;
  struct ibv_sge sge;

  memset(&sge, 0, sizeof(struct ibv_sge));
  sge.addr = local_addr;
  sge.length = sizeof(uint64_t);
  sge.lkey = lkey;

  // prepare the send work request
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = &sge;
  sr.num_sge = 1;

  sr.wr.atomic.compare_add = compare_add;
  sr.wr.atomic.swap = 0;
  sr.wr.atomic.remote_addr = remote_addr;
  sr.wr.atomic.rkey = rkey;
  sr.send_flags = IBV_SEND_SIGNALED;
  sr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;

  if (fence) {
    sr.send_flags |= IBV_SEND_FENCE;
  }

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG(
      "POST --> FA [wr_id:%lu qp_num:%u sockfd:%d local_addr:%p remote_addr:%p "
      "compare_add:%lu].",
      sr.wr_id, ctx->id->qp->qp_num, ctx->sockfd, local_addr, remote_addr,
      compare_add);

  return sr.wr_id;
}

void post_fetch_add_sync(struct conn_context *ctx, uint64_t local_addr,
                         uint32_t lkey, uint64_t remote_addr, uint32_t rkey,
                         uint64_t compare_add, int fence) {
  uint32_t wr_id = post_fetch_add_async(ctx, local_addr, lkey, remote_addr,
                                        rkey, compare_add, fence);
  spin_till_completion(ctx, wr_id, 1);
}

uint32_t post_noop_async(struct conn_context *ctx, int signaled) {
  int ret;
  struct ibv_send_wr sr;
  struct ibv_send_wr *bad_sr;
  struct ibv_sge sge;

  memset(&sge, 0, sizeof(struct ibv_sge));

  // prepare the send work request
  memset(&sr, 0, sizeof(sr));
  sr.next = NULL;
  sr.wr_id = next_wr_id(ctx, 1);
  sr.sg_list = &sge;
  sr.num_sge = 0;
  if (signaled) {
    sr.send_flags = IBV_SEND_SIGNALED;
  }
  sr.opcode = 0;

  ctx->n_posted_ops++;

  ret = ibv_post_send(ctx->id->qp, &sr, &bad_sr);
  if (ret) {
    ERROR_LOG("failed to post rdma operation, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("POST --> NOOP [wr_id:%lu].", sr.wr_id);

  return sr.wr_id;
}

void post_noop_sync(struct conn_context *ctx, int signaled) {
  if (!signaled) {
    ERROR_LOG("cannot synchronously execute unsignaled verb.");
    exit(EXIT_FAILURE);
  }
  uint32_t wr_id = post_noop_async(ctx, signaled);
  spin_till_completion(ctx, wr_id, 1);
}
