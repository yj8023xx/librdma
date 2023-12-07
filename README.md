# librdma

## Overview

**librdma** is a powerful library designed to simplify the development process of Remote Direct Memory Access (RDMA) applications. It abstracts away the complexities of underlying implementation details, providing a straightforward and user-friendly interface for RDMA development. This library aims to streamline the RDMA development experience, making it accessible to a broader audience and facilitating the creation of high-performance, low-latency networking applications.



## Getting Started

To start using librdma in your project, follow these simple steps:

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/yj8023xx/librdma.git
   ```

2. **Build the Library:**

   ```bash
   cd librdma
   make
   ```

3. **Integrate into Your Project:**
   Link against the librdma library in your project and include the necessary headers

If you want to generate debugging information, please add a flag in the Makefile: `RDMA_FLAGS += -DDEBUG`

## Example

**server side**

```c
// setup server accept conn param
struct conn_param accept_options = {
    .poll_mode = CQ_POLL_MODE_POLLING,
    .on_pre_connect_cb = app_on_pre_connect_cb,
    .on_connect_cb = app_on_connect_cb};
struct agent_context *server = create_server(1, 1, &accept_options);

// sockfd for listening
char *src_addr = "10.10.10.2";
char *port = "12345";
int listen_fd = server_listen(server, src_addr, port);
struct conn_context *listen_ctx = get_connection(server, listen_fd);
    
// start listening
start_listen(listen_ctx);
```

**client side**

```c
// create client
struct agent_context *client = create_client(1, 1);

char *dst_addr = "10.10.10.2";
char *port = "12345";
struct conn_param rc_options = {.poll_mode = CQ_POLL_MODE_POLLING,
                                .on_pre_connect_cb = app_on_pre_connect_cb,
                                .on_connect_cb = app_on_connect_cb};
int sockfd = add_connection_rc(client, dst_addr, port, &rc_options);
struct conn_context *rc_ctx = get_connection(client, sockfd);

// start run
pthread_create(&rc_ctx->rdma_event_thread, NULL, client_loop, rc_ctx);

// waiting for thread to finish executing
pthread_join(rc_ctx->rdma_event_thread, NULL);

// free resources
destroy_agent(client);
```

**callback**

```c
void app_on_pre_connect_cb(struct conn_context *ctx) {
  // write your code, e.g, register mr
}
void app_on_connect_cb(struct conn_context *ctx) {
  // write your code
}
void app_on_complete_cb(struct conn_context *ctx) {
  // write your code to handle complete events
}
void app_on_disconnect_cb(struct conn_context *ctx) {
  // write your code
}
```



## Contact

For any inquiries or feedback, please contact the maintainers of librdma at [913660289@qq.com](mailto:913660289@qq.com). We appreciate your input and participation in making librdma a valuable tool for RDMA development.
