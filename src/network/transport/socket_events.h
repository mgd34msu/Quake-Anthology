#ifndef QA_NETWORK_SOCKET_EVENTS_H
#define QA_NETWORK_SOCKET_EVENTS_H
#include "../socket_private.h"
#include "qa/common.h"
bool qa_net_socket_read(qa_socket, uint8_t *, size_t, size_t *, bool *eof, qa_error *);
bool qa_net_socket_connected(qa_socket, bool *ready, qa_error *);
#endif
