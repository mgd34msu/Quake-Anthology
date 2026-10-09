#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0602
#endif
#include "kex_discovery_native.h"
#include "qa/text.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
struct qa_kex_mdns_socket { SOCKET descriptor; uint8_t bytes[QA_KEX_MDNS_DATAGRAM_BYTES]; };
static bool socket_error(qa_error *e, const char *operation)
{
    qa_error_set(e, QA_ERROR_IO, 0, "KEX mDNS %s failed: Winsock %d", operation, WSAGetLastError());
    return false;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
struct qa_kex_mdns_socket { int descriptor; uint8_t bytes[QA_KEX_MDNS_DATAGRAM_BYTES]; };
static bool socket_error(qa_error *e, const char *operation)
{
    qa_error_set(e, QA_ERROR_IO, 0, "KEX mDNS %s failed: %s", operation, strerror(errno));
    return false;
}
#endif

static struct sockaddr_in multicast_address(void)
{
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(5353)};
    const uint8_t host[4] = {224, 0, 0, 251};
    memcpy(&to.sin_addr, host, 4);
    return to;
}

bool qa_kex_mdns_native_open(qa_kex_mdns_socket **out, qa_error *e)
{
    qa_kex_mdns_socket *s = malloc(sizeof(*s));
    if (!s) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating native KEX multicast socket"); return false; }
    struct sockaddr_in bind_to = {.sin_family = AF_INET, .sin_port = htons(5353)};
    struct sockaddr_in to = multicast_address();
    struct ip_mreq membership = {.imr_multiaddr = to.sin_addr};
#ifdef _WIN32
    WSADATA startup;
    int status = WSAStartup(MAKEWORD(2, 2), &startup);
    if (status) {
        free(s); qa_error_set(e, QA_ERROR_IO, 0, "KEX mDNS Winsock startup failed: %d", status); return false;
    }
    s->descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->descriptor == INVALID_SOCKET) { socket_error(e, "socket"); WSACleanup(); free(s); return false; }
    u_long nonblocking = 1;
    BOOL enabled = TRUE;
    DWORD ttl = 255, loop = 1;
    if (!SetHandleInformation((HANDLE)s->descriptor, HANDLE_FLAG_INHERIT, 0)) {
        qa_error_set(e, QA_ERROR_IO, 0, "KEX mDNS socket inheritance setup failed: Windows %lu", (unsigned long)GetLastError());
        closesocket(s->descriptor); WSACleanup(); free(s); return false;
    }
    bool ok = ioctlsocket(s->descriptor, FIONBIO, &nonblocking) == 0 &&
        setsockopt(s->descriptor, SOL_SOCKET, SO_REUSEADDR, (const char *)&enabled, sizeof(enabled)) == 0 &&
        bind(s->descriptor, (const struct sockaddr *)&bind_to, sizeof(bind_to)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&membership, sizeof(membership)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof(loop)) == 0;
    if (!ok) { socket_error(e, "socket setup"); closesocket(s->descriptor); WSACleanup(); free(s); return false; }
#else
    s->descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->descriptor < 0) { free(s); return socket_error(e, "socket"); }
    int enabled = 1, flags = fcntl(s->descriptor, F_GETFL, 0);
    unsigned char ttl = 255, loop = 1;
    bool ok = flags >= 0 && fcntl(s->descriptor, F_SETFL, flags | O_NONBLOCK) == 0 &&
        fcntl(s->descriptor, F_SETFD, FD_CLOEXEC) == 0 &&
        setsockopt(s->descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == 0 &&
        bind(s->descriptor, (const struct sockaddr *)&bind_to, sizeof(bind_to)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) == 0 &&
        setsockopt(s->descriptor, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) == 0;
    if (!ok) { socket_error(e, "socket setup"); close(s->descriptor); free(s); return false; }
#endif
    *out = s;
    return true;
}

bool qa_kex_mdns_native_send(qa_kex_mdns_socket *s, qa_bytes bytes, qa_error *e)
{
    struct sockaddr_in to = multicast_address();
#ifdef _WIN32
    int count;
    do { count = sendto(s->descriptor, (const char *)bytes.data, (int)bytes.size, 0,
                        (const struct sockaddr *)&to, sizeof(to)); }
    while (count == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    if (count == SOCKET_ERROR) return socket_error(e, "send");
#else
    ssize_t count;
    do { count = sendto(s->descriptor, bytes.data, bytes.size, 0, (const struct sockaddr *)&to, sizeof(to)); }
    while (count < 0 && errno == EINTR);
    if (count < 0) return socket_error(e, "send");
#endif
    if ((size_t)count == bytes.size) return true;
    qa_error_set(e, QA_ERROR_IO, 0, "KEX mDNS native send was incomplete");
    return false;
}

bool qa_kex_mdns_native_collect(qa_kex_mdns_socket *s, uint64_t now,
    qa_net_transport_event *out, qa_error *e)
{
    *out = (qa_net_transport_event){0};
    struct sockaddr_in from = {0};
    bool oversize = false;
#ifdef _WIN32
    int from_size = sizeof(from);
    int count;
    do { count = recvfrom(s->descriptor, (char *)s->bytes, (int)sizeof(s->bytes), 0,
                          (struct sockaddr *)&from, &from_size); }
    while (count == SOCKET_ERROR && WSAGetLastError() == WSAEINTR);
    if (count == SOCKET_ERROR) {
        int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) return true;
        if (code == WSAEMSGSIZE) { oversize = true; count = 0; }
        else return socket_error(e, "receive");
    }
#else
    struct iovec iov = {.iov_base = s->bytes, .iov_len = sizeof(s->bytes)};
    struct msghdr message = {.msg_name = &from, .msg_namelen = sizeof(from),
        .msg_iov = &iov, .msg_iovlen = 1};
    ssize_t count;
    do { count = recvmsg(s->descriptor, &message, 0); } while (count < 0 && errno == EINTR);
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return socket_error(e, "receive");
    }
    oversize = (message.msg_flags & MSG_TRUNC) != 0 || (size_t)count > sizeof(s->bytes);
#endif
    out->packet.kind = oversize ? QA_NET_POLL_OVERSIZE : QA_NET_POLL_PACKET;
    out->packet.from.kind = QA_NET_IPV4;
    out->packet.from.port = ntohs(from.sin_port);
    memcpy(out->packet.from.host.ipv4, &from.sin_addr, 4);
    out->packet.received_ns = now;
    if (!oversize) out->packet.payload = (qa_bytes){s->bytes, (size_t)count};
    return true;
}

bool qa_kex_mdns_native_close(qa_kex_mdns_socket *s, qa_error *e)
{
    if (!s) return true;
#ifdef _WIN32
    bool ok = closesocket(s->descriptor) == 0;
    if (!ok) socket_error(e, "close");
    if (WSACleanup() && ok) { socket_error(e, "Winsock cleanup"); ok = false; }
#else
    bool ok = close(s->descriptor) == 0;
    if (!ok) socket_error(e, "close");
#endif
    free(s);
    return ok;
}

bool qa_kex_mdns_native_hostname(char label[64], qa_error *e)
{
    size_t used = 0;
#ifdef _WIN32
    WCHAR hostname[256];
    if (GetHostNameW(hostname, 256)) return socket_error(e, "hostname");
    for (size_t i = 0; used < 63 && hostname[i]; ++i) {
        uint16_t c = hostname[i];
        label[used++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-') ? (char)c : '-';
    }
#else
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) < 0) return socket_error(e, "hostname");
    hostname[sizeof(hostname) - 1] = 0;
    qa_bytes name = {(const uint8_t *)hostname, strlen(hostname)};
    size_t cursor = 0;
    uint32_t c;
    while (used < 63 && qa_utf8_next(name, &cursor, &c)) {
        label[used++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-') ? (char)c : '-';
        if (c > 0xffff && used < 63) label[used++] = '-';
    }
#endif
    label[used] = 0;
    if (used) return true;
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Native KEX hostname is empty");
    return false;
}
