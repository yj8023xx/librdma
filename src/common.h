#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <assert.h>
#include <fcntl.h>
#include <linux/types.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "message.h"
#include "uthash.h"
#include "utils.h"

#define IBV_INLINE_THRESHOLD 128  // whether to use IBV_SEND_INLINE
#define MAX_DEVICES 2             // max # of RDMA devices/ports
#define MAX_CONNECTIONS 1000      // max # of RDMA connections per peer
#define MAX_MR \
  5  // max # of memory regions per connection (XXX keep this value at 2 or
     // lower for now)
#define MAX_MR_SIZE 1024 * 1024  // 1MB
#define MAX_SEND_WR 1024         // depth of rdma send queue
#define MAX_RECV_WR 1024         // depth of rdma recv queue
#define MAX_SEND_SGE 4
#define MAX_RECV_SGE 4
#define MAX_CQE 100

#define TIMEOUT_IN_MS 500
#define DEFAULT_PORT 12345

#define max(a, b) (a > b ? a : b)
#define min(a, b) (a > b ? b : a)

#ifndef bool
#define bool char
#define true 1
#define false 0
#endif

// agent callbacks
typedef void (*app_pre_conn_cb_fn)(struct conn_context *ctx);
typedef void (*app_conn_cb_fn)(struct conn_context *ctx);
typedef void (*app_disc_cb_fn)(struct conn_context *ctx);
typedef void (*app_compl_cb_fn)(struct conn_context *ctx, struct ibv_wc *wc);

enum node_role { CLIENT = 0, SERVER = 1 };

enum mcast_role { MCAST_RECEIVER = 0, MCAST_SENDER = 1 };

enum srq_flag { SRQ_DISABLE = 0, SRQ_ENABLE = 1 };

enum connection_state { CONNECTION_TERMINATED, CONNECTION_READY };

enum cq_poll_mode {
  CQ_POLL_MODE_POLLING = 0,  // default
  CQ_POLL_MODE_REACTOR = 1
};

struct agent_context {
  uint32_t node_id;

  uint32_t agent_id;

  int is_server;

  // event notification mechanism
  struct reactor_context *reactor;

  // indicates whether fds is available
  int *conn_bitmap;
  // mapping between fd and conn context
  struct conn_context **conn_fd_map;

  // server conn param
  struct conn_param *options;
};

/**
 * param for connection
 */
struct conn_param {
  int poll_mode;

  int srq_flag;
  // if SRQ is enabled, the following two items cannot be null
  struct ibv_srq *srq;
  struct ibv_cq *srq_cq;

  app_pre_conn_cb_fn on_pre_connect_cb;
  app_conn_cb_fn on_connect_cb;
  app_disc_cb_fn on_disconnect_cb;
  app_compl_cb_fn on_complete_cb;
};

/**
 * connection metadata
 */
struct conn_context {
  // unique connection (socket) descriptor
  int sockfd;

  // connection id
  struct rdma_cm_id *id;

  // the agent corresponding to the connection
  struct agent_context *agent;

  int is_server;

  // connection state
  int state;

  // multicast addr
  struct sockaddr *mcast_addr;

  // multicast sender
  int is_sender;

  // address handle
  struct ibv_ah *ah;

  // remote qpn
  uint32_t remote_qpn;

  // remote qkey
  uint32_t remote_qkey;

  // completion queue
  struct ibv_cq *cq;

  // completion channel
  struct ibv_comp_channel *comp_channel;

  // 0 polling(default) 1 reactor
  int poll_mode;

  // flag for whether to create shared receive queue
  int srq_flag;
  // if SRQ is enabled, the following two items cannot be null
  struct ibv_srq *srq;
  struct ibv_cq *srq_cq;

  /* polling mechanism */
  // background cq polling thread
  pthread_t cq_poll_thread;

  // control whether cq_poll_thread execute
  int running;

  // thread for rdma event loop
  pthread_t rdma_event_thread;

  /* custom memory regions */
  // the number of local mrs/remote mrs
  int num_local_mrs;
  int num_remote_mrs;

  // mr contexts
  struct mr_context *mr_ctxs;

  // local mrs/remote mrs
  struct ibv_mr **local_mr;
  struct mr_context **remote_mr;

  // number of posted (signaled) send/recv operations
  uint32_t n_posted_ops;

  // synchronization
  uint32_t last_send;
  uint32_t last_send_compl;
  uint32_t last_recv;
  uint32_t last_recv_compl;

  // callback for connection
  app_pre_conn_cb_fn on_pre_connect_cb;
  app_conn_cb_fn on_connect_cb;
  app_disc_cb_fn on_disconnect_cb;
  app_compl_cb_fn on_complete_cb;
};

/**
 * memory region metadata
 */
struct mr_context {
  // start address
  uint64_t addr;
  // length
  uint32_t length;
  // access keys
  uint32_t key;
};

/**
 * RC metadata: used for exchanging information
 */
struct private_data {
  int num_mrs;
  uint64_t addr[3];
  uint32_t length[3];
  uint32_t rkey[3];
};

#endif
