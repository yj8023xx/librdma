#ifndef RDMA_MESSAGE_H
#define RDMA_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#define MAX_KEY_SIZE 64

enum msg_type { MSG_MR, MSG_CUSTOM };

struct message {
  enum msg_type type;
  int len;
  char *data;
};

static inline char *msg_type_to_str(int type) {
  switch (type) {
    case MSG_MR:
      return "MSG_MR";
      break;
    case MSG_CUSTOM:
      return "MSG_CUSTOM";
    default:
      return "UNDEFINED";
  }
}

#endif