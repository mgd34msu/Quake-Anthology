#ifndef QA_NETWORK_SOCKET_PRIVATE_H
#define QA_NETWORK_SOCKET_PRIVATE_H

#include "qa/common.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET qa_socket;
typedef int qa_socklen;
#define QA_SOCKET_INVALID INVALID_SOCKET
static inline bool qa_socket_begin(void) {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
static inline void qa_socket_end(void) { WSACleanup(); }
static inline void qa_socket_close(qa_socket fd) { closesocket(fd); }
static inline bool qa_socket_nonblocking(qa_socket fd) {
    u_long enabled = 1;
    return ioctlsocket(fd, FIONBIO, &enabled) == 0;
}
static inline int qa_socket_error(void) { return WSAGetLastError(); }
static inline bool qa_socket_again(int code) {
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEALREADY;
}
static inline bool qa_socket_interrupted(int code) { return code == WSAEINTR; }
static inline bool qa_socket_truncated(int code) { return code == WSAEMSGSIZE; }
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int qa_socket;
typedef socklen_t qa_socklen;
#define QA_SOCKET_INVALID (-1)
static inline bool qa_socket_begin(void) { return true; }
static inline void qa_socket_end(void) {}
static inline void qa_socket_close(qa_socket fd) { (void)close(fd); }
static inline bool qa_socket_nonblocking(qa_socket fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int descriptor_flags = fcntl(fd, F_GETFD, 0);
    return flags >= 0 && descriptor_flags >= 0 &&
           fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
           fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0;
}
static inline int qa_socket_error(void) { return errno; }
static inline bool qa_socket_again(int code) {
    return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS || code == EALREADY;
}
static inline bool qa_socket_interrupted(int code) { return code == EINTR; }
static inline bool qa_socket_truncated(int code) { return code == EMSGSIZE; }
#endif

/* Zero-time readiness only; the application supplies the handshake deadline. */
static inline int qa_socket_connect_ready(qa_socket fd) {
#if defined(_WIN32)
    fd_set write_set, error_set;
    struct timeval timeout = {0, 0};
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(fd, &write_set);
    FD_SET(fd, &error_set);
    return select(0, NULL, &write_set, &error_set, &timeout);
#else
    struct pollfd descriptor = {fd, POLLOUT, 0};
    return poll(&descriptor, 1, 0);
#endif
}

#endif
