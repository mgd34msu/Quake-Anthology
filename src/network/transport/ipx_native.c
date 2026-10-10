#include "ipx_native.h"
#include "../socket_private.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <wsipx.h>
#define QA_NATIVE_IPX 1
#elif defined(__linux__) && defined(__has_include)
#if __has_include(<netipx/ipx.h>)
#include <netipx/ipx.h>
#define QA_NATIVE_IPX 1
#endif
#endif

#if defined(QA_NATIVE_IPX)
typedef struct ipx_native {
    qa_socket fd;
    uint8_t packet_type;
    uint8_t *receive_buffer;
    size_t limit;
} ipx_native;

static struct sockaddr_ipx ipx_native_address(const qa_net_address *address, uint8_t packet_type) {
    struct sockaddr_ipx native;
    memset(&native, 0, sizeof(native));
#if defined(_WIN32)
    uint32_t network = address->host.ipx.network;
    uint8_t wire[4] = {(uint8_t)(network >> 24), (uint8_t)(network >> 16),
                       (uint8_t)(network >> 8), (uint8_t)network};
    native.sa_family = AF_IPX;
    memcpy(native.sa_netnum, wire, sizeof(wire));
    memcpy(native.sa_nodenum, address->host.ipx.node, 6);
    native.sa_socket = htons(address->port);
    (void)packet_type;
#else
    native.sipx_family = AF_IPX;
    native.sipx_network = htonl(address->host.ipx.network);
    native.sipx_port = htons(address->port);
    memcpy(native.sipx_node, address->host.ipx.node, 6);
    native.sipx_type = packet_type;
#endif
    return native;
}

static qa_net_address ipx_native_from(const struct sockaddr_ipx *native) {
    qa_net_address address = {.kind = QA_NET_IPX};
#if defined(_WIN32)
    const unsigned char *net = (const unsigned char *)native->sa_netnum;
    address.host.ipx.network = (uint32_t)net[0] << 24 | (uint32_t)net[1] << 16 |
                               (uint32_t)net[2] << 8 | net[3];
    address.port = ntohs(native->sa_socket);
    memcpy(address.host.ipx.node, native->sa_nodenum, 6);
#else
    address.host.ipx.network = ntohl(native->sipx_network);
    address.port = ntohs(native->sipx_port);
    memcpy(address.host.ipx.node, native->sipx_node, 6);
#endif
    return address;
}

static qa_net_send_result ipx_native_send(void *opaque, const qa_net_address *to, qa_bytes bytes, qa_error *error) {
    ipx_native *ipx = opaque;
    if (!to || to->kind != QA_NET_IPX || !to->port || bytes.size > ipx->limit ||
        (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native IPX datagram");
        return false;
    }
    struct sockaddr_ipx destination = ipx_native_address(to, ipx->packet_type);
    const uint8_t empty = 0;
    const uint8_t *data = bytes.size ? bytes.data : &empty;
#if defined(_WIN32)
    int count = sendto(ipx->fd, (const char *)data, (int)bytes.size, 0,
                       (const struct sockaddr *)&destination, (int)sizeof(destination));
#else
    ssize_t count = sendto(ipx->fd, data, bytes.size, 0,
                           (const struct sockaddr *)&destination, (socklen_t)sizeof(destination));
#endif
    if (count >= 0 && (size_t)count == bytes.size) return true;
    qa_error_set(error, QA_ERROR_IO, 0, "Native IPX send failed");
    return false;
}

static bool ipx_native_collect(void *opaque, uint64_t now_ns, qa_net_transport_event *event, qa_error *error) {
    *event = (qa_net_transport_event){0};
    qa_net_datagram *out = &event->packet;
    ipx_native *ipx = opaque;
    struct sockaddr_ipx source;
    memset(&source, 0, sizeof(source));
    qa_socklen length = (qa_socklen)sizeof(source);
#if defined(_WIN32)
    int count = recvfrom(ipx->fd, (char *)ipx->receive_buffer, (int)(ipx->limit + 1), 0,
                         (struct sockaddr *)&source, &length);
#else
    ssize_t count = recvfrom(ipx->fd, ipx->receive_buffer, ipx->limit + 1, 0,
                             (struct sockaddr *)&source, &length);
#endif
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    if (count < 0) {
        int code = qa_socket_error();
        if (qa_socket_again(code) || qa_socket_interrupted(code)) return true;
        if (qa_socket_truncated(code)) {
            out->kind = QA_NET_POLL_OVERSIZE;
            out->received_ns = now_ns;
            if (length == (qa_socklen)sizeof(source)) out->from = ipx_native_from(&source);
            return true;
        }
        qa_error_set(error, QA_ERROR_IO, 0, "Native IPX receive failed");
        return false;
    }
    if (length != (qa_socklen)sizeof(source)) return true;
    out->kind = (size_t)count > ipx->limit ? QA_NET_POLL_OVERSIZE : QA_NET_POLL_PACKET;
    out->from = ipx_native_from(&source);
    out->received_ns = now_ns;
    if (out->kind == QA_NET_POLL_PACKET) out->payload = (qa_bytes){ipx->receive_buffer, (size_t)count};
    return true;
}

static void ipx_native_close(void *opaque) {
    ipx_native *ipx = opaque;
    qa_socket_close(ipx->fd);
    qa_socket_end();
    free(ipx->receive_buffer);
    free(ipx);
}
#endif

bool qa_net_ipx_native_transport_open(const qa_net_address *local, qa_net_limits limits,
                           uint8_t packet_type, qa_net_transport **out, qa_error *error) {
#if defined(QA_NATIVE_IPX)
    if (!qa_socket_begin()) {
        qa_error_set(error, QA_ERROR_IO, 0, "Native socket initialization failed");
        return false;
    }
#if defined(_WIN32)
    qa_socket fd = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
#else
    qa_socket fd = socket(AF_IPX, SOCK_DGRAM, 0);
#endif
    if (fd == QA_SOCKET_INVALID) {
        int code = qa_socket_error();
#if defined(_WIN32)
        bool unavailable = code == WSAEAFNOSUPPORT || code == WSAEPROTONOSUPPORT ||
                           code == WSAESOCKTNOSUPPORT;
#else
        bool unavailable = code == EAFNOSUPPORT || code == EPROTONOSUPPORT ||
                           code == ESOCKTNOSUPPORT;
#endif
        qa_socket_end();
        if (unavailable) qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
            "Host has no native IPX provider; select DOSBox or an explicit IPX UDP tunnel");
        else qa_error_set(error, QA_ERROR_IO, 0, "Native IPX socket creation failed");
        return false;
    }
    int enabled = 1, type = packet_type;
    bool configured = qa_socket_nonblocking(fd) &&
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&enabled, (qa_socklen)sizeof(enabled)) == 0;
#if defined(_WIN32)
    configured = configured && setsockopt(fd, NSPROTO_IPX, IPX_PTYPE,
                   (const char *)&type, (int)sizeof(type)) == 0;
#else
    configured = configured && setsockopt(fd, SOL_IPX, IPX_TYPE,
                   (const char *)&type, (socklen_t)sizeof(type)) == 0;
#endif
    struct sockaddr_ipx address = ipx_native_address(local, packet_type);
    qa_socklen address_size = (qa_socklen)sizeof(address);
    if (!configured || bind(fd, (const struct sockaddr *)&address, address_size) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &address_size) != 0 ||
        address_size != (qa_socklen)sizeof(address)) {
        qa_socket_close(fd);
        qa_socket_end();
        qa_error_set(error, QA_ERROR_IO, 0, "Native IPX socket configuration or bind failed");
        return false;
    }
    ipx_native *ipx = calloc(1, sizeof(*ipx));
    if (!ipx) {
        qa_socket_close(fd);
        qa_socket_end();
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Native IPX allocation failed");
        return false;
    }
    ipx->fd = fd;
    ipx->limit = limits.datagram_bytes;
    ipx->packet_type = packet_type;
    ipx->receive_buffer = malloc(ipx->limit + 1);
    if (!ipx->receive_buffer) {
        ipx_native_close(ipx);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Native IPX receive allocation failed");
        return false;
    }
    qa_net_address bound = ipx_native_from(&address);
    const qa_net_transport_ops ops = {
        .send = ipx_native_send, .collect = ipx_native_collect, .close = ipx_native_close
    };
    if (!qa_net_transport_create(&bound, limits, &ops, ipx, out, error)) {
        ipx_native_close(ipx);
        return false;
    }
    return true;
#else
    (void)packet_type;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "Host has no native IPX API; select DOSBox or an explicit IPX UDP tunnel");
    return false;
#endif
}
