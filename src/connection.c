#include "connection.h"

#include "verbs_wrap.h"

extern inline int cmp_counters(uint32_t a, uint32_t b);
extern inline int diff_counters(uint32_t a, uint32_t b);
extern inline int find_first_empty_bit_and_set(int bitmap[], int n);
extern inline int find_first_empty_bit(int bitmap[], int n);
extern inline int find_next_empty_bit(int idx, int bitmap[], int n);
extern inline int find_first_set_bit_and_empty(int bitmap[], int n);
extern inline int find_first_set_bit(int bitmap[], int n);
extern inline int find_next_set_bit(int idx, int bitmap[], int n);
extern inline int find_bitmap_weight(int bitmap[], int n);
extern inline struct sockaddr_in *copy_ipv4_sockaddr(
    struct sockaddr_storage *in);

void init_connection(struct agent_context *agent, struct rdma_cm_id *id,
                     int sockfd, struct conn_param *options) {
  struct conn_context *ctx =
      (struct conn_context *)calloc(1, sizeof(struct conn_context));
  ctx->mcast_addr = (struct sockaddr *)malloc(sizeof(struct sockaddr));
  ctx->local_mr = (struct ibv_mr **)calloc(MAX_MR, sizeof(struct ibv_mr *));
  ctx->remote_mr =
      (struct mr_context **)calloc(MAX_MR, sizeof(struct mr_context *));
  ctx->poll_mode = CQ_POLL_MODE_POLLING;  // default

  if (options) {
    ctx->poll_mode = options->poll_mode;

    ctx->srq_flag = options->srq_flag;
    ctx->srq = options->srq;
    ctx->srq_cq = options->srq_cq;

    ctx->on_pre_connect_cb = options->on_pre_connect_cb;
    ctx->on_connect_cb = options->on_connect_cb;
    ctx->on_disconnect_cb = options->on_disconnect_cb;
    ctx->on_complete_cb = options->on_complete_cb;
  }

  ctx->last_send = 0;
  ctx->last_send_compl = 0;
  ctx->last_recv = 0;
  ctx->last_recv_compl = 0;

  id->context = ctx;
  ctx->id = id;
  ctx->agent = agent;
  ctx->sockfd = sockfd;
  ctx->is_server = agent->is_server;
}

int setup_connection(struct conn_context *ctx) {
  build_cq_channel(ctx);
  build_qp(ctx);
}

void build_cq_channel(struct conn_context *ctx) {
  int ret;
  ctx->cq = ibv_create_cq(ctx->id->verbs, MAX_CQE, NULL, ctx->comp_channel,
                          0); /* cqe=10 is arbitrary */

  if (!ctx->cq) {
    ERROR_LOG("failed to create CQ, errno: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  // create completion queue channel
  ctx->comp_channel = ibv_create_comp_channel(ctx->id->verbs);

  if (!ctx->comp_channel) {
    ERROR_LOG("failed to create competion channel, errno: %s.",
              strerror(errno));
    exit(EXIT_FAILURE);
  }

  // make sure that we get notified on the first completion
  ret = ibv_req_notify_cq(ctx->cq, 0);
  if (ret) {
    ERROR_LOG("failed to notify CQ, error: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("created CQ for socket #%d.", ctx->sockfd);

  if (ctx->poll_mode == CQ_POLL_MODE_POLLING) {
    DEBUG_LOG("created thread for CQ of sockfd #%d.", ctx->sockfd);
    pthread_create(&ctx->cq_poll_thread, NULL, poll_cq_loop, ctx);
  }
}

void build_qp_attr(struct conn_context *ctx, struct ibv_qp_init_attr *qp_attr) {
  memset(qp_attr, 0, sizeof(*qp_attr));

  qp_attr->send_cq = ctx->cq;
  qp_attr->recv_cq = ctx->cq;

  if (ctx->srq_flag) {
    if (!ctx->srq || !ctx->srq_cq) {
      ERROR_LOG("srq or srq_cq is null.");
      exit(EXIT_FAILURE);
    }
    qp_attr->srq = ctx->srq;
    qp_attr->recv_cq = ctx->srq_cq;
  }

  qp_attr->sq_sig_all = 0;
  qp_attr->qp_type = ctx->id->qp_type;  // IBV_QPT_RC, IBV_QPT_UD
  qp_attr->cap.max_send_wr = MAX_SEND_WR;
  qp_attr->cap.max_recv_wr = MAX_RECV_WR;
  qp_attr->cap.max_send_sge = MAX_SEND_SGE;
  qp_attr->cap.max_recv_sge = MAX_RECV_SGE;
}

void build_qp(struct conn_context *ctx) {
  struct rdma_cm_id *id = ctx->id;
  struct ibv_qp_init_attr qp_attr;

  build_qp_attr(ctx, &qp_attr);

  if (!id->verbs) {
    ERROR_LOG("ibv_context is null.");
    exit(EXIT_FAILURE);
  }

  if (!ctx->srq_flag) {
    id->pd = ibv_alloc_pd(id->verbs);
    if (!id->pd) {
      ERROR_LOG("failed to allocate pd, errno: %s.", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  if (rdma_create_qp(id, id->pd, &qp_attr)) {
    ERROR_LOG("failed create QP, errno: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG(
      "created QP for socket #%d [qp_num:%u qp_type:%s max_send_wr:%d "
      "max_recv_wr:%d max_send_sge:%d max_recv_sge:%d].",
      ctx->sockfd, ctx->id->qp->qp_num, qp_type_to_str(ctx->id->qp->qp_type),
      qp_attr.cap.max_send_wr, qp_attr.cap.max_recv_wr,
      qp_attr.cap.max_send_sge, qp_attr.cap.max_recv_sge);

  bind_fd_to_qp(ctx->agent, ctx->id->qp->qp_num, ctx->sockfd);
}

void build_srq(struct conn_context *ctx) {
  /* create shared receive queue */
  struct ibv_srq_init_attr srq_attr;
  memset(&srq_attr, 0, sizeof(srq_attr));
  srq_attr.attr.max_wr = MAX_RECV_WR;
  srq_attr.attr.max_sge = MAX_RECV_SGE;
  srq_attr.attr.srq_limit = 0;
  /* assign it to QPs later */
  int ret = rdma_create_srq(ctx->id, NULL, &srq_attr);
  if (ret) {
    ERROR_LOG("failed to create SRQ, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("create SRQ for sockfd #%d.", ctx->sockfd);
}

void build_private_data(struct conn_context *ctx, struct private_data *data) {
  data->num_mrs = ctx->num_local_mrs;
  for (int i = 0; i < data->num_mrs; i++) {
    data->addr[i] = (uint64_t)ctx->local_mr[i]->addr;
    data->length[i] = ctx->local_mr[i]->length;
    data->rkey[i] = ctx->local_mr[i]->rkey;

    DEBUG_LOG(
        "built private data #%d for sockfd #%d [addr:%p len:%lu rkey:%lu].", i,
        ctx->sockfd, data->addr[i], data->length[i], data->rkey[i]);
  }
}

void build_rc_param(struct conn_context *ctx, struct rdma_conn_param *rc_param,
                    struct private_data *data) {
  if (!ctx) {
    ERROR_LOG("conn context is null.");
    exit(EXIT_FAILURE);
  }

  memset(rc_param, 0, sizeof(*rc_param));

  rc_param->initiator_depth = 1;
  rc_param->responder_resources = 1;
  rc_param->rnr_retry_count = 7; /* infinite retry */

  // user-controlled data buffer
  rc_param->private_data = data;
  rc_param->private_data_len = sizeof(*data);

  if (sizeof(*data) > 56) {
    ERROR_LOG("user data length larger than max allowed size.");
    exit(EXIT_FAILURE);
  }
}

int setup_rc_param(struct conn_context *ctx, struct rdma_conn_param *rc_param) {
  struct private_data *data = NULL;

  if (rc_param) {
    data = (struct rc_meta *)rc_param->private_data;
  }

  // modify connection parameters using metadata exchanged between client and
  // server
  if (data) {
    DEBUG_LOG("setuping private_data for sockfd #%d [given:%d expected:%lu].",
              ctx->sockfd, rc_param->private_data_len,
              sizeof(struct private_data));
    if (sizeof(struct private_data) > rc_param->private_data_len) {
      ERROR_LOG("invalid connection param length.");
      exit(EXIT_FAILURE);
    }
    update_remote_mr(ctx, data->num_mrs, data->addr, data->length, data->rkey);
  }
}

void rdma_event_loop(struct conn_context *ctx, int exit_on_handle,
                     int exit_on_connect, int exit_on_disconnect) {
  struct agent_context *agent = ctx->agent;
  struct rdma_event_channel *ec = ctx->id->channel;
  struct rdma_cm_event *event = NULL;
  struct rdma_conn_param rc_param;
  struct private_data data;
  int ret;

  while (rdma_get_cm_event(ec, &event) == 0) {
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    DEBUG_LOG("received event[%d]: %s.", event_copy.event,
              rdma_event_str(event_copy.event));

    rdma_ack_cm_event(event);  // frees the communication event

    if (event_copy.event ==
        RDMA_CM_EVENT_ADDR_RESOLVED) {  // generated on the client (active) side
                                        // in response to rdma_resolve_addr()
      if (ctx->id->qp_type == IBV_QPT_UD) {  // ud mode
        setup_connection(ctx);               // equals event_copy.id->context
        on_pre_connect(ctx);
        /* join the multicast group */
        ret = rdma_join_multicast(event_copy.id, ctx->mcast_addr, NULL);
        if (ret) {
          ERROR_LOG("failed to join multicast group, errno: %s.",
                    strerror(errno));
          exit(EXIT_FAILURE);
        }
      } else {  // rc mode
        ret = rdma_resolve_route(event_copy.id, TIMEOUT_IN_MS);
        if (ret) {
          ERROR_LOG("failed to resolve route, errno: %s.", strerror(errno));
          exit(EXIT_FAILURE);
        }
      }
    } else if (event_copy.event == RDMA_CM_EVENT_MULTICAST_JOIN) {
      ctx->remote_qpn = event_copy.param.ud.qp_num;
      ctx->remote_qkey = event_copy.param.ud.qkey;
      DEBUG_LOG("remote_qpn:%u, remote_qkey:%u.", ctx->remote_qpn,
                ctx->remote_qkey);
      if (ctx->is_sender) {
        /* create an address handle for the sender */
        ctx->ah = ibv_create_ah(ctx->id->pd, &event_copy.param.ud.ah_attr);
        if (!ctx->ah) {
          ERROR_LOG("failed to create address handle, errno: %s.",
                    strerror(errno));
          exit(EXIT_FAILURE);
        }
      }
      // connect callback
      on_connect(ctx);

      if (exit_on_connect) {
        break;
      }
    } else if (event_copy.event == RDMA_CM_EVENT_MULTICAST_ERROR) {
      ERROR_LOG("failed to join multicast group.");
      exit(EXIT_FAILURE);
    } else if (event_copy.event ==
               RDMA_CM_EVENT_ROUTE_RESOLVED) {   // generated on the
                                                 // client(active) side in
                                                 // response to
                                                 // rdma_resolve_route()
      setup_connection(event_copy.id->context);  // equals ctx
      on_pre_connect(event_copy.id->context);

      int ret = rdma_connect(event_copy.id, NULL);
      if (ret) {
        ERROR_LOG("failed to connect, errno: %s.", strerror(errno));
        exit(EXIT_FAILURE);
      }
    } else if (event_copy.event ==
               RDMA_CM_EVENT_CONNECT_REQUEST) {  //  generated on the server
                                                 //  side
      // event_copy.id is a newly created rdma_cm_id
      // If the event type is RDMA_CM_EVENT_CONNECT_REQUEST, then this
      // references a new id for that communication
      accept_connection(agent, event_copy.id, agent->options);
      setup_connection(event_copy.id->context);

      // pre connect callback
      on_pre_connect(event_copy.id->context);

      build_private_data(event_copy.id->context, &data);
      build_rc_param(event_copy.id->context, &rc_param, &data);

      ret = rdma_accept(event_copy.id, &rc_param);
      if (ret) {
        ERROR_LOG("failed to accept, errno: %s.", strerror(errno));
        exit(EXIT_FAILURE);
      }
    } else if (event_copy.event ==
               RDMA_CM_EVENT_ESTABLISHED) {  // generated on both sides
      setup_rc_param(event_copy.id->context, &event_copy.param.conn);

      // connect callback
      on_connect(event_copy.id->context);

      if (exit_on_connect) {
        on_disconnect(event_copy.id->context);
        break;
      }
    } else if (event_copy.event ==
               RDMA_CM_EVENT_DISCONNECTED) {  // generated on both sides
      on_disconnect(event_copy.id->context);
      rdma_disconnect(event_copy.id);
      destroy_connection(event_copy.id->context);
      if (exit_on_disconnect) {
        break;
      }
    } else if (event_copy.event ==
               RDMA_CM_EVENT_REJECTED) {  // generated on the client (active)
                                          // side
      ERROR_LOG("rejection reason: %d.", event_copy.status);
      ERROR_LOG("connection failure. exiting...");
      exit(EXIT_FAILURE);
    } else if (event_copy.event ==
               RDMA_CM_EVENT_TIMEWAIT_EXIT) {  // generated when the QP
                                               // associated with the connection
                                               // has exited its timewait state
      // this event indicates that the recently destroyed queue pair is ready to
      // be reused at this point, clean up any allocated memory for connection
      if (exit_on_disconnect) {
        break;
      }
    } else {
      ERROR_LOG("unknown event.");
      exit(EXIT_FAILURE);
    }
    if (exit_on_handle) {
      break;
    }
  }
}

void update_completions(struct conn_context *ctx, struct ibv_wc *wc) {
  // signal any threads blocking on wr.id
  if (wc->opcode & IBV_WC_RECV) {
    DEBUG_LOG("COMPLETION --> RECV [wr_id:%lu qp_num:%u sockfd:%d].", wc->wr_id,
              ctx->id->qp ? ctx->id->qp->qp_num : -1, ctx->sockfd);
    ctx->last_recv_compl = wc->wr_id;
  } else {
    DEBUG_LOG("COMPLETION --> SEND [wr_id:%lu qp_num:%u sockfd:%d opcode:%s].",
              wc->wr_id, ctx->id->qp ? ctx->id->qp->qp_num : -1, ctx->sockfd,
              wc_opcode_to_str(wc->opcode));
    ctx->last_send_compl = wc->wr_id;
  }
}

// spin till we receive a completion with wr_id (overrides poll_cq loop)
void spin_till_completion(struct conn_context *ctx, uint32_t wr_id, int send) {
  struct ibv_wc wc;

  DEBUG_LOG(
      "spinning till wr #%u completes for sockfd #%d [last_completed_wr:%u].",
      wr_id, ctx->sockfd, get_last_compl_wr_id(ctx, send));

  while (cmp_counters(wr_id, get_last_compl_wr_id(ctx, send)) > 0) {
    ibw_cpu_relax();
  }
}

void poll_cq_once(struct conn_context *ctx, struct ibv_wc *wc) {
  while (ibv_poll_cq(ctx->cq, 1, wc)) {
    if (wc->status == IBV_WC_SUCCESS) {
      update_completions(ctx, wc);
      on_complete(ctx, wc);
    } else {
      const char *descr;
      descr = ibv_wc_status_str(wc->status);
      WARN_LOG(
          "completion failure on sockfd #%d [opcode:%s wr_id:%lu agent_id:%d], "
          "status[%d]: %s.",
          ctx->sockfd, wc_opcode_to_str(wc->opcode), wc->wr_id, wc->status,
          descr, ctx->agent->agent_id);
    }
  }
}

// func for polling
void *poll_cq_loop(void *data) {
  struct conn_context *ctx = (struct conn_context *)data;
  struct ibv_wc wc;
  ctx->running = 1;
  do {
    poll_cq_once(ctx, &wc);
  } while (ctx->running);
  return NULL;
}

// func for reactor
void event_channel_handler(void *data) { rdma_event_loop(data, 1, 0, 1); }

// func for reactor
void comp_channel_handler(void *data) {
  struct conn_context *ctx = (struct conn_context *)data;
  struct ibv_cq *cq;
  struct ibv_wc wc;
  void *ev_ctx;

  // if the solicited_only flag is set, then only CQEs for WRs that had the
  // solicited flag set will trigger the notification
  ibv_get_cq_event(ctx->comp_channel, &cq, &ev_ctx);
  ibv_ack_cq_events(cq, 1);
  ibv_req_notify_cq(cq, 0);
  poll_cq_once(ctx, &wc);
}

struct conn_context *find_first_connection(struct agent_context *agent) {
  int i = find_first_set_bit(agent->conn_bitmap, MAX_CONNECTIONS);
  if (i >= 0) {
    return get_connection(agent, i);
  } else {
    return NULL;
  }
}

struct conn_context *find_next_connection(struct conn_context *ctx) {
  struct agent_context *agent = ctx->agent;

  int i = find_next_set_bit(ctx->sockfd, agent->conn_bitmap, MAX_CONNECTIONS);

  if (i >= 0) {
    return get_connection(agent, i);
  } else {
    return NULL;
  }
}

struct conn_context *get_connection(struct agent_context *agent, int sockfd) {
  if (sockfd < 0) {
    return NULL;
  }
  if (sockfd > MAX_CONNECTIONS) {
    ERROR_LOG("invalid sockfd; must be less than MAX_CONNECTIONS.");
    exit(EXIT_FAILURE);
  }
  if (agent->conn_bitmap[sockfd]) {
    return agent->conn_id_map[sockfd]->context;
  } else {
    return NULL;
  }
}

int get_connection_count(struct agent_context *agent) {
  return find_bitmap_weight(agent->conn_bitmap, MAX_CONNECTIONS);
}

int get_next_connection(struct agent_context *agent, int cur) {
  int i = 0;

  if (cur < 0) {
    i = find_first_set_bit(agent->conn_bitmap, MAX_CONNECTIONS);
  } else {
    i = find_next_set_bit(cur, agent->conn_bitmap, MAX_CONNECTIONS);
  }

  if (i >= 0) {
    return i;
  } else {
    return -1;
  }
}

char *get_connection_ip(struct agent_context *agent, int sockfd) {
  if (get_connection(agent, sockfd)) {
    struct sockaddr_in *addr_in =
        copy_ipv4_sockaddr(&agent->conn_id_map[sockfd]->route.addr.dst_storage);
    char *s = malloc(sizeof(char) * INET_ADDRSTRLEN);
    s = inet_ntoa(addr_in->sin_addr);
    return s;
  } else {
    return NULL;
  }
}

int get_connection_qpn(struct agent_context *agent, int sockfd) {
  struct conn_context *ctx = get_connection(agent, sockfd);
  if (ctx) {
    return ctx->id->qp->qp_num;
  } else {
    return -1;
  }
}

uint32_t get_last_compl_wr_id(struct conn_context *ctx, int send) {
  // we maintain seperate wr_ids for send/recv queues since
  // there is no ordering between their work requests
  if (send) {
    return ctx->last_send_compl;
  } else {
    return ctx->last_recv_compl;
  }
}

void bind_fd_to_qp(struct agent_context *agent, int qpn, int sockfd) {
  struct qp_fd_entry *entry = malloc(sizeof(struct qp_fd_entry));
  entry->qpn = qpn;
  entry->sockfd = sockfd;
  HASH_ADD_INT(agent->qp_fd_hh, qpn, entry);

  DEBUG_LOG("bound fd #%d to qpn #%d.", sockfd, qpn);
}

int get_fd_by_qp(struct agent_context *agent, int qpn) {
  struct qp_fd_entry *entry = NULL;
  HASH_FIND_INT(agent->qp_fd_hh, qpn, entry);
  if (!entry) {
    return -1;
  }
  return entry->sockfd;
}

void set_conn_state(struct conn_context *ctx, int new_state) {
  if (ctx->state == new_state) {
    return;
  }

  DEBUG_LOG("modified state for socket #%d from %d to %d.", ctx->sockfd,
            ctx->state, new_state);
  ctx->state = new_state;
}

int is_ready(struct agent_context *agent, int sockfd) {
  struct conn_context *ctx = get_connection(agent, sockfd);
  if (ctx) {
    if (ctx->state == CONNECTION_READY) {
      return 1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

int is_terminated(struct agent_context *agent, int sockfd) {
  struct conn_context *ctx = get_connection(agent, sockfd);
  if (ctx) {
    if (ctx->state == CONNECTION_TERMINATED) {
      return 1;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

void destroy_connection(struct conn_context *ctx) {
  int sockfd = ctx->sockfd;
  struct agent_context *agent = ctx->agent;

  if (!is_terminated(agent, sockfd)) {
    ERROR_LOG(
        "can't clear resources for non-terminated connection [sockfd:%d].",
        sockfd);
    exit(EXIT_FAILURE);
  }

  DEBUG_LOG("clearing connection resources for socket #%d.", sockfd);
  agent->conn_bitmap[sockfd] = 0;
  agent->conn_id_map[sockfd] = NULL;

  ctx->running = 0;

  if (ctx->ah) {
    DEBUG_LOG("destroying ah.");
    ibv_destroy_ah(ctx->ah);
  }

  DEBUG_LOG("destroying qp.");
  if (ctx->id->qp) {
    rdma_destroy_qp(ctx->id);
  }

  if (ctx->cq) {
    DEBUG_LOG("destroying cq.");
    ibv_destroy_cq(ctx->cq);
  }

  if (ctx->comp_channel) {
    DEBUG_LOG("destroying comp channel.");
    ibv_destroy_comp_channel(ctx->comp_channel);
  }

  DEBUG_LOG("deregistering local mr.");
  for (int i = 0; i < ctx->num_local_mrs; i++) {
    ibv_dereg_mr(ctx->local_mr[i]);
  }

  if (ctx->id->pd) {
    DEBUG_LOG("deallocing pd.");
    ibv_dealloc_pd(ctx->id->pd);
  }

  if (ctx->id->srq) {
    DEBUG_LOG("destroying srq.");
    rdma_destroy_srq(ctx->id);
  }

  DEBUG_LOG("destroying rdma cm id.");
  rdma_destroy_id(ctx->id);

  DEBUG_LOG("freeing memory resources.");
  for (int i = 0; i < ctx->num_local_mrs; i++) {
    free(ctx->mr_ctxs[i].addr);
  }
  for (int i = 0; i < ctx->num_remote_mrs; i++) {
    free(ctx->remote_mr[i]);
  }
  free(ctx->mr_ctxs);
  free(ctx->mcast_addr);
  free(ctx->local_mr);
  free(ctx->remote_mr);
  free(ctx);

  DEBUG_LOG("destroyed conn_context [sockfd:%d].", sockfd);
}
