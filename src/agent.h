#ifndef RDMA_AGENT_H
#define RDMA_AGENT_H

#include "common.h"
#include "connection.h"
#include "mr.h"
#include "reactor.h"

/**
 * create server
 */
struct agent_context *create_server(int node_id, int agent_id,
                                    struct conn_param *param);

/**
 * create client
 */
struct agent_context *create_client(int node_id, int agent_id);

/**
 *  destroy agent
 */
void destroy_agent(struct agent_context *agent);

/**
 * common
*/
int add_connection_rc(struct agent_context *agent, char *dst_addr, char *port,
                      struct conn_param *options);

int add_connection_ud(struct agent_context *agent, char *bind_addr,
                      char *mcast_addr, int is_sender,
                      struct conn_param *options);

/**
 * server side
*/
int server_listen(struct agent_context *server, char *src_addr, char *port);

int accept_connection(struct agent_context *server, struct rdma_cm_id *id,
                      struct conn_param *options);

/**
 * multicast
 */
void join_multicast_group(struct conn_context *ctx);
void leave_multicast_group(struct conn_context *ctx);

/**
 * data: conn_context
 */
void *client_loop(void *data);
/**
 * data: conn_context
 */
void *server_loop(void *data);

/**
 * agent callback
 */
void on_pre_connect(struct conn_context *ctx);
void on_connect(struct conn_context *ctx);
void on_disconnect(struct conn_context *ctx);
void on_complete(struct conn_context *ctx, struct ibv_wc *wc);

#endif