#include "agent.h"

struct agent_context *create_agent(int node_id, int agent_id, int node_role) {
  struct agent_context *agent =
      (struct agent_context *)malloc(sizeof(struct agent_context));

  agent->node_id = node_id;
  agent->agent_id = agent_id;

  agent->is_server = (node_role == SERVER);

  // reactor for CQ
  agent->reactor =
      (struct reactor_context *)malloc(sizeof(struct reactor_context));
  init_reactor(agent->reactor);

  // connection resources
  pthread_mutex_init(&agent->mu, NULL);
  agent->conn_bitmap = (int *)calloc(MAX_CONNECTIONS, sizeof(int));
  agent->conn_fd_map = (struct conn_context **)calloc(
      MAX_CONNECTIONS, sizeof(struct conn_context *));

  return agent;
}

struct agent_context *create_server(int node_id, int agent_id,
                                    struct conn_param *options) {
  struct agent_context *server = create_agent(node_id, agent_id, SERVER);
  server->options = options;
  return server;
}

struct agent_context *create_client(int node_id, int agent_id) {
  return create_agent(node_id, agent_id, CLIENT);
}

void destroy_agent(struct agent_context *agent) {
  destroy_reactor(agent->reactor);
  pthread_mutex_destroy(&agent->mu);
  free(agent->reactor);
  free(agent->conn_bitmap);
  free(agent->conn_fd_map);
  free(agent);

  DEBUG_LOG("destroyed agent.");
}

int get_sockfd(struct agent_context *agent) {
  pthread_mutex_lock(&agent->mu);
  int sockfd =
      find_first_empty_bit_and_set(agent->conn_bitmap, MAX_CONNECTIONS);

  if (sockfd < 0) {
    ERROR_LOG(
        "can't open new connection; number of open sockets == "
        "MAX_CONNECTIONS.");
    exit(EXIT_FAILURE);
  }
  pthread_mutex_unlock(&agent->mu);
  return sockfd;
}

void free_sockfd(struct agent_context *agent, int sockfd) {
  pthread_mutex_lock(&agent->mu);
  DEBUG_LOG("clearing connection resources for socket #%d.", sockfd);
  agent->conn_bitmap[sockfd] = 0;
  agent->conn_fd_map[sockfd] = NULL;
  pthread_mutex_unlock(&agent->mu);
}

struct conn_context *add_connection_rc(struct agent_context *agent,
                                       char *dst_addr, char *port,
                                       struct conn_param *options) {
  DEBUG_LOG("attempting to add reliable connection to %s:%s.", dst_addr, port);

  int ret;
  struct rdma_addrinfo *rai;
  struct rdma_addrinfo hints;
  struct rdma_cm_id *id = NULL;
  struct rdma_event_channel *ec;

  memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_TCP;
  ret = rdma_getaddrinfo(dst_addr, port, &hints, &rai);
  if (ret) {
    ERROR_LOG("failed to get address info, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ec = rdma_create_event_channel();
  ret = rdma_create_id(
      ec, &id, NULL,
      RDMA_PS_TCP);  // rdma_port_space: RDMA_PS_TCP or RDMA_PS_UDP
  if (ret) {
    ERROR_LOG("failed to create rdma_cm_id, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ret = rdma_resolve_addr(id, NULL, rai->ai_dst_addr, TIMEOUT_IN_MS);
  if (ret) {
    ERROR_LOG("failed to resolve address, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  rdma_freeaddrinfo(rai);

  int sockfd = get_sockfd(agent);
  struct conn_context *ctx = init_connection(agent, id, sockfd, options);

  agent->conn_bitmap[sockfd] = 1;
  agent->conn_fd_map[sockfd] = ctx;

  DEBUG_LOG("created reliable connection to %s:%s on sockfd #%d.", dst_addr,
            port, sockfd);

  return ctx;
}

struct conn_context *add_connection_ud(struct agent_context *agent,
                                       char *bind_addr, char *mcast_addr,
                                       int is_sender,
                                       struct conn_param *options) {
  DEBUG_LOG("attempting to add unreliable connection to %s.", mcast_addr);

  int ret;
  struct rdma_addrinfo *bind_rai = NULL;
  struct rdma_addrinfo *mcast_rai = NULL;
  struct rdma_addrinfo hints;
  struct rdma_cm_id *id = NULL;
  struct rdma_event_channel *ec;

  memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_UDP;
  /* If we are bound to an address, then a PD was already allocated
   * to the CM ID */
  if (bind_addr) {
    hints.ai_flags = RAI_PASSIVE;
    ret = rdma_getaddrinfo(bind_addr, NULL, &hints, &bind_rai);
    if (ret) {
      ERROR_LOG("failed to get address info, errno: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  hints.ai_flags = 0;
  ret = rdma_getaddrinfo(mcast_addr, NULL, &hints, &mcast_rai);
  if (ret) {
    ERROR_LOG("failed to get address info, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (bind_addr) {
    ret = rdma_bind_addr(id, bind_rai->ai_src_addr);
    if (ret) {
      ERROR_LOG("failed to bind address, errno: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  ec = rdma_create_event_channel();
  ret = rdma_create_id(ec, &id, NULL, RDMA_PS_UDP);
  if (ret) {
    ERROR_LOG("failed to create rdma_cm_id, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ret = rdma_resolve_addr(id, (bind_rai) ? bind_rai->ai_src_addr : NULL,
                          mcast_rai->ai_dst_addr, TIMEOUT_IN_MS);
  if (ret) {
    ERROR_LOG("failed to resolve address, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  int sockfd = get_sockfd(agent);
  struct conn_context *ctx = init_connection(agent, id, sockfd, options);

  agent->conn_bitmap[sockfd] = 1;
  agent->conn_fd_map[sockfd] = ctx;

  memcpy(ctx->mcast_addr, mcast_rai->ai_dst_addr, sizeof(struct sockaddr));
  ctx->is_sender = is_sender;

  rdma_freeaddrinfo(bind_rai);
  rdma_freeaddrinfo(mcast_rai);

  DEBUG_LOG("created unreliable connection to %s on sockfd #%d.", mcast_addr,
            sockfd);

  return ctx;
}

/**
 * server side
 */
struct conn_context *accept_connection(struct agent_context *server,
                                       struct rdma_cm_id *id,
                                       struct conn_param *options) {
  DEBUG_LOG("attempting to accept reliable connection.");

  int sockfd = get_sockfd(server);
  struct conn_context *ctx = init_connection(server, id, sockfd, options);

  server->conn_bitmap[sockfd] = 1;
  server->conn_fd_map[sockfd] = ctx;

  DEBUG_LOG("acceptted reliable connection on sockfd #%d.", sockfd);

  return ctx;
}

struct conn_context *server_listen(struct agent_context *server, char *src_addr,
                                   char *port) {
  DEBUG_LOG("attempting to listen on port %s for connections.", port);

  int ret;
  struct rdma_addrinfo *rai;
  struct rdma_addrinfo hints;
  struct rdma_cm_id *id = NULL;
  struct rdma_event_channel *ec;

  memset(&hints, 0, sizeof(hints));
  hints.ai_port_space = RDMA_PS_TCP;
  hints.ai_flags = RAI_PASSIVE;
  ret = rdma_getaddrinfo(src_addr, port, &hints, &rai);
  if (ret) {
    ERROR_LOG("failed to get address info, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ec = rdma_create_event_channel();
  ret = rdma_create_id(
      ec, &id, NULL,
      RDMA_PS_TCP);  // rdma_port_space: RDMA_PS_TCP or RDMA_PS_UDP
  if (ret) {
    ERROR_LOG("failed to create rdma_cm_id, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  ret = rdma_bind_addr(id, rai->ai_src_addr);
  if (ret) {
    ERROR_LOG("failed to bind address, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  rdma_freeaddrinfo(rai);

  int listen_fd = get_sockfd(server);
  struct conn_context *ctx = init_connection(server, id, listen_fd, NULL);

  server->conn_bitmap[listen_fd] = 1;
  server->conn_fd_map[listen_fd] = ctx;

  return ctx;
}

void join_multicast_group(struct conn_context *ctx) {
  rdma_event_loop(ctx, 0, 1, 0);
  DEBUG_LOG("joined multicast group successfully.");
}

void leave_multicast_group(struct conn_context *ctx) {
  on_disconnect(ctx);

  int ret = rdma_leave_multicast(ctx->id, ctx->mcast_addr);
  if (ret) {
    ERROR_LOG("failed to leave multicast group [sockfd:%d].", ctx->sockfd);
    exit(EXIT_FAILURE);
  }

  destroy_connection(ctx);

  DEBUG_LOG("left multicast group.");
}

/**
 * data: conn_context
 */
void *client_loop(void *data) {
  struct conn_context *ctx = (struct conn_context *)data;
  rdma_event_loop(ctx, 0, 0, 1);  // exit upon disconnect

  DEBUG_LOG("exited client_loop.");
  return NULL;
}

/**
 * data: conn_context
 */
void *server_loop(void *data) {
  struct conn_context *ctx = (struct conn_context *)data;
  struct rdma_event_channel *ec = ctx->id->channel;
  int ret = rdma_listen(ctx->id, 100); /* backlog=10 is arbitrary */
  if (ret) {
    ERROR_LOG("failed to listen, errno: %s.", strerror(errno));
    exit(EXIT_FAILURE);
  }

  rdma_event_loop(ctx, 0, 0, 0);  // do not exit upon disconnect

  // destroy resources
  destroy_connection(ctx);
  rdma_destroy_event_channel(ec);

  DEBUG_LOG("exited server_loop.");
  return 0;
}

void start_listen(struct conn_context *listen_ctx) { server_loop(listen_ctx); }

void start_connect(struct conn_context *ctx) {
  rdma_event_loop(ctx, 0, 1, 1);  // exit upon connect
}

void disconnect(struct conn_context *ctx) {
  on_disconnect(ctx);
  rdma_disconnect(ctx->id);
  // destroy resources
  struct rdma_event_channel *ec = ctx->id->channel;
  destroy_connection(ctx);
  rdma_destroy_event_channel(
      ec);  // Note: all rdma_cm_id's associated with the
            // event channel must be destroyed, and all returned events must be
            // acked before calling this function
}

void on_pre_connect(struct conn_context *ctx) {
  if (ctx->on_pre_connect_cb) {
    DEBUG_LOG("sockfd #%d triggered pre-connect callback.", ctx->sockfd);
    ctx->on_pre_connect_cb(ctx);
  }
}

void on_connect(struct conn_context *ctx) {
  set_conn_state(ctx, CONNECTION_READY);

  if (ctx->poll_mode == CQ_POLL_MODE_REACTOR) {
    add_event_fd(ctx->agent->reactor, EPOLLIN, ctx->comp_channel->fd, ctx,
                 comp_channel_handler);
  }

  DEBUG_LOG("connection established [sockfd:%d qpnum:%d].", ctx->sockfd,
            ctx->id->qp->qp_num);

  if (ctx->on_connect_cb) {
    DEBUG_LOG("sockfd #%d triggered connect callback.", ctx->sockfd);
    ctx->on_connect_cb(ctx);
  }
}

void on_disconnect(struct conn_context *ctx) {
  set_conn_state(ctx, CONNECTION_TERMINATED);
  DEBUG_LOG("connection terminated [sockfd:%d].", ctx->sockfd);

  if (ctx->on_disconnect_cb) {
    DEBUG_LOG("sockfd #%d triggered disconnect callback.", ctx->sockfd);
    ctx->on_disconnect_cb(ctx);
  }
}

void on_complete(struct conn_context *ctx, struct ibv_wc *wc) {
  if (ctx->on_complete_cb) {
    DEBUG_LOG("sockfd #%d triggered complete callback.", ctx->sockfd);
    ctx->on_complete_cb(ctx, wc);
  }
}