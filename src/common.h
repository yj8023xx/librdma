#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <linux/types.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <sys/time.h>
#include "json-c/json.h"
#include "uthash.h"
#include "utils.h"
#include "message.h"

#define IBV_INLINE_THRESHOLD 128 // whether to use IBV_SEND_INLINE
#define MAX_DEVICES 2            // max # of RDMA devices/ports
#define MAX_CONNECTIONS 1000     // max # of RDMA connections per peer
#define MAX_MR 5                 // max # of memory regions per connection (XXX keep this value at 2 or lower for now)
#define MR_NUM 5                 // default num of memory regions per connection
#define MAX_MR_SIZE 256 * 1024      // 1MB
#define MAX_MSG_SIZE 256 * 1024    // 128B
#define MAX_SEND_WR 16000        // depth of rdma send queue
#define MAX_RECV_WR 16000        // depth of rdma recv queue
#define MAX_SEND_SGE 4
#define MAX_RECV_SGE 4
#define MAX_CQE 5000

#define MAX_TABLE_SIZE 10000
#define MAX_KEY_SIZE 64
#define MAX_VALUE_SIZE 64

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
typedef void (*app_conn_cb_fn)(struct conn_context *ctx);
typedef void (*app_disc_cb_fn)(struct conn_context *ctx);
typedef void (*app_compl_cb_fn)(struct conn_context *ctx, struct ibv_wc *wc);

enum node_role
{
    CLIENT = 0,
    SERVER = 1
};

enum mcast_role
{
    MCAST_RECEIVER = 0,
    MCAST_SENDER = 1
};

enum srq_flag
{
    SRQ_DISABLE = 0,
    SRQ_ENABLE = 1
};

enum cq_poll_mode
{
    CQ_POLL_MODE_DISABLE = 0,
    CQ_POLL_MODE_REACTOR = 1,
    CQ_POLL_MODE_POLLING = 2
};

struct agent_context
{
    uint32_t node_id;

    uint32_t agent_id;

    int is_server;

    // event notification mechanism
    struct reactor_context *reactor;

    // indicates whether fds is available
    int *conn_bitmap;
    // mapping between fd and rdma_cm_id
    struct rdma_cm_id **conn_id_map;

    // mapping between key and lock
    pthread_rwlock_t *key_lock_map;
    // mapping between key and sockfd
    int *key_fd_map;

    // mapping between qpn and sockfd
    struct qp_fd_entry *qp_fd_hh;

    struct ibv_srq *srq;
    struct ibv_cq *srq_cq;

    // server conn param
    struct conn_param *options;
};

struct conn_param
{
    int num_mrs;
    struct mr_context *mr_ctxs;
    int poll_mode;
    int srq_flag;
    int create_flags;

    app_conn_cb_fn on_connect_cb;
    app_disc_cb_fn on_disconnect_cb;
    app_compl_cb_fn on_complete_cb;
};

/**
 * connection metadata
 */
struct conn_context
{
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

    // 0 disable 1 reactor 2 polling
    int poll_mode;

    /* polling mechanism */
    // background cq polling thread
    pthread_t cq_poll_thread;

    // thread for rdma event loop
    pthread_t rdma_event_thread;

    // work queue mr
    struct ibv_mr *wq_mr;

    // send/recv buffer
    void **send_buf;
    void **recv_buf;

    // send/recv mr
    struct ibv_mr **send_mr;
    struct ibv_mr **recv_mr;

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

    // mapping between wr and user data
    struct wr_data_entry *wr_data_hh;

    // QP buffers
    int sq_mr_idx; // last idx of sq mr
    int rq_mr_idx;
    void *sq_start;    // indicates the base address of the SQ
    size_t sq_wqe_cnt; // the number of wqe in the SQ
    uint32_t sq_wqe_shift;
    uint32_t sq_stride; // 1 << sq_wqe_stride
    uint64_t *sq_wrid;
    void *sq_end;       // indicates the end address of the SQ
    uint32_t scur_post; // the idx of next wqe

    // QP flags
    int srq_flag;     // whether to create shared receive queue flag
    int create_flags; // IBV_EXP_QP_CREATE_MANAGED_SEND: used for send enable

    // acqurie lock flag
    uint8_t *lock_flag;

    // callback for connection
    app_conn_cb_fn on_connect_cb;
    app_disc_cb_fn on_disconnect_cb;
    app_compl_cb_fn on_complete_cb;
};

struct wr_data_entry
{
    int wr_id;         /* key */
    void *data;        /* value */
    UT_hash_handle hh; /* makes this structure hashable */
};

struct qp_fd_entry
{
    int qpn;
    int sockfd;
    UT_hash_handle hh;
};

/**
 * memory region metadata
 */
struct mr_context
{
    // start address
    uint64_t addr;
    // length
    uint32_t length;
    // access keys
    uint32_t lkey;
    uint32_t rkey;
    // 1 if physical mr; otherwise virtual
    int physical;
};

/**
 * RC metadata: used for exchanging information
 */
struct private_data
{
    int num_mrs;
    uint64_t addr[3];
    uint32_t length[3];
    uint32_t rkey[3];
};

/**
 * MLX defs
 */
enum
{
    SEND_WQE_BB = 64,
    SEND_WQE_SHIFT = 6,
};

#ifdef MLX5
// contains control information for the WQE
struct wqe_ctrl_seg
{
    uint32_t opmod_idx_opcode;
    uint32_t qpn_ds;
    uint8_t signature;
    uint8_t rsvd[2]; // reserved field
    uint8_t fm_ce_se;
    uint32_t imm;
};
#else
struct wqe_ctrl_seg
{
    uint32_t owner_opcode;
    uint8_t reserved[3];
    uint8_t fence_size;
    /*
     * High 24 bits are SRC remote buffer; low 8 bits are flags:
     * [7]   SO (strong ordering)
     * [5]   TCP/UDP checksum
     * [4]   IP checksum
     * [3:2] C (generate completion queue entry)
     * [1]   SE (solicited event)
     * [0]   FL (force loopback)
     */
    union
    {
        uint32_t srcrb_flags;
        uint16_t srcrb_flags16[2];
    };
    /*
     * imm is immediate data for send/RDMA write w/ immediate;
     * also invalidation key for send with invalidate; input
     * modifier for WQEs on CCQs.
     */
    uint32_t imm;
};
#endif

// contains pointers and a byte count for the scatter/gather list
struct wqe_data_seg
{
    uint32_t byte_count;
    uint32_t lkey;
    uint64_t addr;
};

// contains pointers at remote side
struct wqe_raddr_seg
{
    uint64_t raddr;
    uint32_t rkey;
    uint32_t reserved;
};

// contains information about Atomic operations
struct wqe_atomic_seg
{
    uint64_t swap_add;
    uint64_t compare;
};

struct wqe_inl_data_seg
{
    uint32_t byte_count;
};

// contains wait information that how many WCs it waits from which QP
struct wqe_wait_en_seg
{
    uint8_t rsvd0[8];
    uint32_t pi;
    uint32_t obj_num;
};

#endif