#ifndef RDMA_MESSAGE_H
#define RDMA_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

#define MAX_KEY_SIZE 64

enum msg_type
{
    MSG_ACQUIRE_LOCK,
    MSG_RELEASE_LOCK,
    MSG_UPDATE_LOCK,
    MSG_LOCK_SUCCESS,
    MSG_UNLOCK_SUCCESS,
    MSG_LOCK_FAIL,
    MSG_UNLOCK_FAIL,
    MSG_NOTIFY
};

struct message
{
    enum msg_type type;
    char key[MAX_KEY_SIZE];
    int shared;
    uint64_t lock;
};

static inline char *msg_type_to_str(int type)
{
    switch (type)
    {
    case MSG_ACQUIRE_LOCK:
        return "MSG_ACQUIRE_LOCK";
        break;
    case MSG_RELEASE_LOCK:
        return "MSG_RELEASE_LOCK";
        break;
    case MSG_UPDATE_LOCK:
        return "MSG_UPDATE_LOCK";
        break;
    case MSG_LOCK_SUCCESS:
        return "MSG_LOCK_SUCCESS";
        break;
    case MSG_UNLOCK_SUCCESS:
        return "MSG_UNLOCK_SUCCESS";
        break;
    case MSG_LOCK_FAIL:
        return "MSG_LOCK_FAIL";
        break;
    case MSG_UNLOCK_FAIL:
        return "MSG_UNLOCK_FAIL";
        break;
    case MSG_NOTIFY:
        return "MSG_NOTIFY";
        break;
    default:
        return "UNDEFINED";
    }
}

#endif