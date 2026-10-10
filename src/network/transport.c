#define _POSIX_C_SOURCE 200809L
#include "qa/network.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

struct qa_net_transport {
    qa_net_address address;
    qa_net_limits limits;
    qa_net_transport_ops ops;
    void *state;
};

static bool fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}

static bool valid_address(const qa_net_address *address)
{
    if (address == NULL) return false;
    switch (address->kind) {
        case QA_NET_IPV4: case QA_NET_IPV6: case QA_NET_IPX: return true;
        case QA_NET_LOOPBACK:
            return address->host.loopback[0] != '\0' &&
                memchr(address->host.loopback, '\0', sizeof(address->host.loopback)) != NULL;
    }
    return false;
}

bool qa_net_address_equal(const qa_net_address *a, const qa_net_address *b, bool port)
{
    if (!valid_address(a) || !valid_address(b) || a->kind != b->kind) return false;
    if (port && a->kind != QA_NET_LOOPBACK && a->port != b->port) return false;
    switch (a->kind) {
        case QA_NET_IPV4: return memcmp(a->host.ipv4, b->host.ipv4, 4) == 0;
        case QA_NET_IPV6:
            return a->host.ipv6.scope == b->host.ipv6.scope &&
                memcmp(a->host.ipv6.bytes, b->host.ipv6.bytes, 16) == 0;
        case QA_NET_LOOPBACK: return strcmp(a->host.loopback, b->host.loopback) == 0;
        case QA_NET_IPX:
            return a->host.ipx.network == b->host.ipx.network && memcmp(a->host.ipx.node, b->host.ipx.node, 6) == 0;
    }
    return false;
}

static bool decimal(const char *text, uint32_t maximum, uint32_t *out)
{
    if (text == NULL || *text == '\0') return false;
    uint32_t value = 0;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') return false;
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool split_ip(const char *text, uint16_t default_port, bool allow_zero,
                     char host[256], uint16_t *port, qa_error *error)
{
    if (text == NULL || *text == '\0') return fail(error, QA_ERROR_ARGUMENT, "Empty network address");
    const char *begin = text, *end = text + strlen(text), *port_text = NULL;
    if (*text == '[') {
        begin = text + 1;
        end = strchr(begin, ']');
        if (end == NULL) return fail(error, QA_ERROR_FORMAT, "Unclosed IPv6 address");
        if (end[1] != '\0') {
            if (end[1] != ':') return fail(error, QA_ERROR_FORMAT, "Invalid address suffix");
            port_text = end + 2;
        }
    } else {
        const char *colon = strchr(text, ':');
        if (colon != NULL && strchr(colon + 1, ':') == NULL) { end = colon; port_text = colon + 1; }
    }
    size_t length = (size_t)(end - begin);
    if (length == 0 || length >= 256) return fail(error, QA_ERROR_FORMAT, "Network host length is invalid");
    memcpy(host, begin, length);
    host[length] = '\0';
    uint32_t number = default_port;
    if (port_text != NULL && !decimal(port_text, UINT16_MAX, &number))
        return fail(error, QA_ERROR_FORMAT, "Invalid network port");
    if (!allow_zero && number == 0) return fail(error, QA_ERROR_ARGUMENT, "Network port is zero");
    *port = (uint16_t)number;
    return true;
}

bool qa_net_address_parse(const char *text, uint16_t default_port, bool allow_zero,
                           qa_net_address *out, qa_error *error)
{
    if (text == NULL || out == NULL) return fail(error, QA_ERROR_ARGUMENT, "Missing network address");
    qa_net_address address = {0};
    if (strncmp(text, "loopback:", 9) == 0) {
        size_t size = strlen(text + 9);
        if (size == 0 || size >= sizeof(address.host.loopback))
            return fail(error, QA_ERROR_FORMAT, "Invalid loopback name");
        address.kind = QA_NET_LOOPBACK;
        memcpy(address.host.loopback, text + 9, size + 1);
        *out = address;
        return true;
    }
    if (strncmp(text, "ipx:", 4) == 0) {
        size_t size = strlen(text);
        if (size < 25 || text[12] != ':') return fail(error, QA_ERROR_FORMAT, "Invalid IPX address");
        address.kind = QA_NET_IPX;
        for (size_t index = 4; index < 12; ++index) {
            int digit = hex_digit(text[index]);
            if (digit < 0) return fail(error, QA_ERROR_FORMAT, "Invalid IPX network");
            address.host.ipx.network = address.host.ipx.network * 16 + (uint32_t)digit;
        }
        for (size_t index = 0; index < 6; ++index) {
            int a = hex_digit(text[13 + index * 2]), b = hex_digit(text[14 + index * 2]);
            if (a < 0 || b < 0) return fail(error, QA_ERROR_FORMAT, "Invalid IPX node");
            address.host.ipx.node[index] = (uint8_t)(a * 16 + b);
        }
        uint32_t port = default_port;
        if (size > 25 && (text[25] != ':' || !decimal(text + 26, UINT16_MAX, &port)))
            return fail(error, QA_ERROR_FORMAT, "Invalid IPX socket");
        if (!allow_zero && port == 0) return fail(error, QA_ERROR_FORMAT, "IPX socket is zero");
        address.port = (uint16_t)port;
        *out = address;
        return true;
    }
    char host[256];
    if (!split_ip(text, default_port, allow_zero, host, &address.port, error)) return false;
    if (inet_pton(AF_INET, host, address.host.ipv4) == 1) {
        address.kind = QA_NET_IPV4;
        *out = address;
        return true;
    }
    char *scope = strchr(host, '%');
    if (scope != NULL) {
        *scope++ = '\0';
        if (!decimal(scope, UINT32_MAX, &address.host.ipv6.scope)) {
            address.host.ipv6.scope = if_nametoindex(scope);
            if (address.host.ipv6.scope == 0) return fail(error, QA_ERROR_FORMAT, "Unknown IPv6 interface");
        }
    }
    if (inet_pton(AF_INET6, host, address.host.ipv6.bytes) != 1)
        return fail(error, QA_ERROR_FORMAT, "Expected an IP literal");
    address.kind = QA_NET_IPV6;
    *out = address;
    return true;
}

static bool from_sockaddr(const struct sockaddr *source, socklen_t size, qa_net_address *out)
{
    qa_net_address address = {0};
    if (source->sa_family == AF_INET && size >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *ip = (const struct sockaddr_in *)source;
        address.kind = QA_NET_IPV4;
        memcpy(address.host.ipv4, &ip->sin_addr, 4);
        address.port = ntohs(ip->sin_port);
    } else if (source->sa_family == AF_INET6 && size >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *ip = (const struct sockaddr_in6 *)source;
        if (IN6_IS_ADDR_V4MAPPED(&ip->sin6_addr)) {
            address.kind = QA_NET_IPV4;
            memcpy(address.host.ipv4, ip->sin6_addr.s6_addr + 12, 4);
        } else {
            address.kind = QA_NET_IPV6;
            memcpy(address.host.ipv6.bytes, &ip->sin6_addr, 16);
            address.host.ipv6.scope = ip->sin6_scope_id;
        }
        address.port = ntohs(ip->sin6_port);
    } else return false;
    *out = address;
    return true;
}

static bool to_sockaddr(const qa_net_address *address, bool map_ipv4,
                        struct sockaddr_storage *storage, socklen_t *size)
{
    memset(storage, 0, sizeof(*storage));
    if (address->kind == QA_NET_IPV4 && !map_ipv4) {
        struct sockaddr_in *ip = (struct sockaddr_in *)storage;
        ip->sin_family = AF_INET;
        ip->sin_port = htons(address->port);
        memcpy(&ip->sin_addr, address->host.ipv4, 4);
        *size = sizeof(*ip);
        return true;
    }
    if (address->kind == QA_NET_IPV6 || (address->kind == QA_NET_IPV4 && map_ipv4)) {
        struct sockaddr_in6 *ip = (struct sockaddr_in6 *)storage;
        ip->sin6_family = AF_INET6;
        ip->sin6_port = htons(address->port);
        if (address->kind == QA_NET_IPV4) {
            ip->sin6_addr.s6_addr[10] = 0xff;
            ip->sin6_addr.s6_addr[11] = 0xff;
            memcpy(ip->sin6_addr.s6_addr + 12, address->host.ipv4, 4);
        } else {
            memcpy(&ip->sin6_addr, address->host.ipv6.bytes, 16);
            ip->sin6_scope_id = address->host.ipv6.scope;
        }
        *size = sizeof(*ip);
        return true;
    }
    return false;
}

bool qa_net_address_resolve(const char *text, uint16_t default_port, unsigned family,
                             qa_net_address *out, qa_error *error)
{
    if (out == NULL || (family != 0 && family != 4 && family != 6))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid resolver family");
    qa_net_address literal;
    if (qa_net_address_parse(text, default_port, false, &literal, NULL)) {
        if ((literal.kind == QA_NET_IPV4 && family != 6) || (literal.kind == QA_NET_IPV6 && family != 4)) {
            *out = literal;
            return true;
        }
        return fail(error, QA_ERROR_ARGUMENT, "Address family differs from selected IP family");
    }
    char host[256], service[6];
    uint16_t port;
    if (!split_ip(text, default_port, false, host, &port, error)) return false;
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    struct addrinfo hints = {0}, *list = NULL;
    hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    int code = getaddrinfo(host, service, &hints, &list);
    if (code != 0) return fail(error, QA_ERROR_IO, "Network hostname resolution failed");
    bool found = false;
    for (const struct addrinfo *entry = list; entry != NULL; entry = entry->ai_next) {
        if (from_sockaddr(entry->ai_addr, (socklen_t)entry->ai_addrlen, &literal)) { found = true; break; }
    }
    freeaddrinfo(list);
    if (!found) return fail(error, QA_ERROR_NOT_FOUND, "No matching IP address");
    *out = literal;
    return true;
}

bool qa_net_address_format(const qa_net_address *address, char *out, size_t capacity, qa_error *error)
{
    if (!valid_address(address) || out == NULL || capacity == 0)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid address output");
    char host[INET6_ADDRSTRLEN], rendered[256];
    int length = -1;
    switch (address->kind) {
        case QA_NET_IPV4:
            if (inet_ntop(AF_INET, address->host.ipv4, host, sizeof(host)) == NULL) break;
            length = snprintf(rendered, sizeof(rendered), "%s:%u", host, (unsigned)address->port);
            break;
        case QA_NET_IPV6:
            if (inet_ntop(AF_INET6, address->host.ipv6.bytes, host, sizeof(host)) == NULL) break;
            if (address->host.ipv6.scope == 0)
                length = snprintf(rendered, sizeof(rendered), "[%s]:%u", host, (unsigned)address->port);
            else length = snprintf(rendered, sizeof(rendered), "[%s%%%u]:%u", host,
                                    (unsigned)address->host.ipv6.scope, (unsigned)address->port);
            break;
        case QA_NET_LOOPBACK:
            length = snprintf(rendered, sizeof(rendered), "loopback:%s", address->host.loopback);
            break;
        case QA_NET_IPX:
            length = snprintf(rendered, sizeof(rendered), "ipx:%08x:%02x%02x%02x%02x%02x%02x:%u",
                (unsigned)address->host.ipx.network, (unsigned)address->host.ipx.node[0],
                (unsigned)address->host.ipx.node[1], (unsigned)address->host.ipx.node[2],
                (unsigned)address->host.ipx.node[3], (unsigned)address->host.ipx.node[4],
                (unsigned)address->host.ipx.node[5], (unsigned)address->port);
            break;
    }
    if (length < 0 || (size_t)length >= sizeof(rendered) || (size_t)length >= capacity)
        return fail(error, QA_ERROR_ARGUMENT, "Address output is too small");
    memcpy(out, rendered, (size_t)length + 1);
    return true;
}

bool qa_net_transport_create(const qa_net_address *address, qa_net_limits limits,
                              const qa_net_transport_ops *ops, void *state,
                              qa_net_transport **out, qa_error *error)
{
    if (!valid_address(address) || limits.datagram_bytes == 0 || limits.datagram_bytes > 65535 ||
        limits.queue_packets == 0 || ops == NULL || ops->send == NULL || ops->collect == NULL ||
        ops->close == NULL || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid datagram transport configuration");
    qa_net_transport *transport = malloc(sizeof(*transport));
    if (transport == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate transport");
    *transport = (qa_net_transport){ .address = *address, .limits = limits, .ops = *ops, .state = state };
    *out = transport;
    return true;
}

const qa_net_address *qa_net_transport_address(const qa_net_transport *transport)
{
    return transport == NULL ? NULL : &transport->address;
}
size_t qa_net_transport_limit(const qa_net_transport *transport)
{
    return transport == NULL ? 0 : transport->limits.datagram_bytes;
}
bool qa_net_transport_ready(const qa_net_transport *transport)
{
    return transport != NULL && (transport->ops.ready == NULL || transport->ops.ready(transport->state));
}
qa_net_send_result qa_net_transport_send(qa_net_transport *transport, const qa_net_address *to, qa_bytes payload, qa_error *error)
{
    if (transport == NULL || !valid_address(to) || (payload.size != 0 && payload.data == NULL) ||
        payload.size > transport->limits.datagram_bytes || (to->kind != QA_NET_LOOPBACK && to->port == 0))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid datagram destination or payload");
    return transport->ops.send(transport->state, to, payload, error);
}
bool qa_net_transport_reliable_receipt(const qa_net_transport *transport,
    const qa_net_address *to, qa_network_reliable_receipt *out)
{
    *out = (qa_network_reliable_receipt){0};
    return transport && transport->ops.reliable_receipt &&
        transport->ops.reliable_receipt(transport->state, to, out);
}
bool qa_net_transport_collect(qa_net_transport *transport, uint64_t now_ns,
    qa_net_transport_event *out, qa_error *error)
{
    if (transport == NULL || out == NULL) return fail(error, QA_ERROR_ARGUMENT, "Invalid datagram receive request");
    *out=(qa_net_transport_event){0};
    return transport->ops.collect(transport->state,now_ns,out,error);
}
bool qa_net_transport_dispatch(qa_net_transport *transport, const qa_net_transport_event *event,
    qa_net_datagram *out, bool *present, qa_error *error)
{
    qa_net_datagram result={0};
    *present=false;
    if (transport->ops.dispatch) {
        if (!transport->ops.dispatch(transport->state,event,&result,present,error)) return false;
    } else if (event) {
        result=event->packet;
        *present=result.kind!=QA_NET_POLL_EMPTY;
    }
    if (!*present) return true;
    if (result.kind < QA_NET_POLL_EMPTY || result.kind > QA_NET_POLL_DROPPED ||
        (result.kind != QA_NET_POLL_EMPTY && !valid_address(&result.from)) ||
        (result.kind == QA_NET_POLL_PACKET && (result.payload.size > transport->limits.datagram_bytes ||
            (result.payload.size != 0 && result.payload.data == NULL))))
        return fail(error, QA_ERROR_FORMAT, "Transport returned an invalid datagram");
    *out=result;
    return true;
}
bool qa_net_transport_maintenance(qa_net_transport *transport,uint64_t now_ns,qa_error *error)
{
    return !transport->ops.maintenance || transport->ops.maintenance(transport->state,now_ns,error);
}
void qa_net_transport_close(qa_net_transport *transport)
{
    if (transport == NULL) return;
    transport->ops.close(transport->state);
    free(transport);
}

typedef struct host_state {
    qa_net_transport *children[2];
    unsigned collect_next, dispatch_next;
} host_state;

static qa_net_send_result host_send(void *context, const qa_net_address *to, qa_bytes payload, qa_error *error)
{
    host_state *state = context;
    return qa_net_transport_send(state->children[to->kind == QA_NET_LOOPBACK ? 1 : 0],
        to, payload, error);
}
static bool host_reliable_receipt(const void *context, const qa_net_address *to,
    qa_network_reliable_receipt *out)
{
    const host_state *state = context;
    return qa_net_transport_reliable_receipt(state->children[to->kind == QA_NET_LOOPBACK ? 1 : 0], to, out);
}

static bool host_collect(void *context, uint64_t now_ns, qa_net_transport_event *out, qa_error *error)
{
    host_state *state = context;
    unsigned first = state->collect_next;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned branch = (first + i) & 1u;
        if (!state->children[branch]) continue;
        if (!qa_net_transport_collect(state->children[branch], now_ns, out, error)) return false;
        out->route = (out->route << 1) | branch;
        if (out->packet.kind != QA_NET_POLL_EMPTY) {
            state->collect_next = branch ^ 1u;
            return true;
        }
    }
    state->collect_next = first ^ 1u;
    return true;
}

static bool host_dispatch(void *context, const qa_net_transport_event *event,
    qa_net_datagram *out, bool *present, qa_error *error)
{
    host_state *state = context;
    if (event) {
        qa_net_transport_event child = *event;
        unsigned branch = child.route & 1u;
        child.route >>= 1;
        return qa_net_transport_dispatch(state->children[branch], &child, out, present, error);
    }
    unsigned first = state->dispatch_next;
    for (unsigned i = 0; i < 2; ++i) {
        unsigned branch = (first + i) & 1u;
        if (!state->children[branch]) continue;
        if (!qa_net_transport_dispatch(state->children[branch], NULL, out, present, error)) return false;
        if (*present) {
            state->dispatch_next = branch ^ 1u;
            return true;
        }
    }
    state->dispatch_next = first ^ 1u;
    return true;
}

static bool host_maintenance(void *context, uint64_t now_ns, qa_error *error)
{
    host_state *state = context;
    if (state->children[0] && !qa_net_transport_maintenance(state->children[0], now_ns, error)) return false;
    return qa_net_transport_maintenance(state->children[1], now_ns, error);
}

static bool host_ready(const void *context)
{
    const host_state *state = context;
    return (!state->children[0] || qa_net_transport_ready(state->children[0])) &&
        qa_net_transport_ready(state->children[1]);
}

static void host_close(void *context)
{
    host_state *state = context;
    qa_net_transport_close(state->children[0]);
    qa_net_transport_close(state->children[1]);
    free(state);
}

bool qa_net_host_transport_create(qa_net_transport *external, qa_net_transport *local,
    qa_net_transport **out, qa_error *error)
{
    if (!local || !out || external == local)
        return fail(error, QA_ERROR_ARGUMENT, "Host transport requires distinct owned endpoints and output");
    host_state *state = calloc(1, sizeof(*state));
    if (!state) return fail(error, QA_ERROR_MEMORY, "Allocating host transport");
    state->children[0] = external;
    state->children[1] = local;
    qa_net_limits limits = local->limits;
    if (external) {
        if (external->limits.datagram_bytes > limits.datagram_bytes)
            limits.datagram_bytes = external->limits.datagram_bytes;
        if (external->limits.queue_packets > limits.queue_packets)
            limits.queue_packets = external->limits.queue_packets;
    }
    const qa_net_transport_ops ops = {.send = host_send, .collect = host_collect,
        .dispatch = host_dispatch, .maintenance = host_maintenance,
        .ready = host_ready, .close = host_close, .reliable_receipt = host_reliable_receipt};
    if (!qa_net_transport_create(external ? &external->address : &local->address,
        limits, &ops, state, out, error)) {
        free(state);
        return false;
    }
    return true;
}

typedef struct udp_state { int fd; uint8_t *bytes; size_t capacity; bool ipv6, ipv6_only; } udp_state;

static qa_net_send_result udp_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    udp_state *state = context;
    struct sockaddr_storage destination;
    socklen_t size;
    if ((state->ipv6_only && to->kind != QA_NET_IPV6) || (!state->ipv6 && to->kind != QA_NET_IPV4) ||
        !to_sockaddr(to, state->ipv6, &destination, &size))
        return fail(error, QA_ERROR_ARGUMENT, "UDP destination has the wrong address family");
    ssize_t sent;
    do { sent = sendto(state->fd, bytes.data, bytes.size, 0, (struct sockaddr *)&destination, size); }
    while (sent < 0 && errno == EINTR);
    if (sent < 0) { qa_error_set(error, QA_ERROR_IO, 0, "UDP send failed: %s", strerror(errno)); return false; }
    if ((size_t)sent != bytes.size) return fail(error, QA_ERROR_IO, "UDP send was incomplete");
    return true;
}

static bool udp_receive(void *context, uint64_t now_ns, qa_net_transport_event *event, qa_error *error)
{
    qa_net_datagram *out=&event->packet;
    udp_state *state = context;
    struct sockaddr_storage source;
    struct iovec vector = { .iov_base = state->bytes, .iov_len = state->capacity };
    struct msghdr message = { .msg_name = &source, .msg_namelen = sizeof(source), .msg_iov = &vector, .msg_iovlen = 1 };
    ssize_t received;
    do { received = recvmsg(state->fd, &message, 0); } while (received < 0 && errno == EINTR);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) { *out = (qa_net_datagram){0}; return true; }
        qa_error_set(error, QA_ERROR_IO, 0, "UDP receive failed: %s", strerror(errno));
        return false;
    }
    qa_net_address address;
    if (!from_sockaddr((const struct sockaddr *)&source, message.msg_namelen, &address))
        return fail(error, QA_ERROR_FORMAT, "UDP source has an unsupported address family");
    bool truncated = (message.msg_flags & MSG_TRUNC) != 0 || (size_t)received > state->capacity;
    *out = (qa_net_datagram){ .kind = truncated ? QA_NET_POLL_OVERSIZE : QA_NET_POLL_PACKET,
        .from = address, .received_ns = now_ns,
        .payload = { truncated ? NULL : state->bytes, truncated ? 0 : (size_t)received } };
    return true;
}

static void udp_close(void *context)
{
    udp_state *state = context;
    close(state->fd);
    free(state->bytes);
    free(state);
}

bool qa_net_udp_policy_read(const qa_net_transport *transport, qa_net_udp_policy *out,
                            bool *present, qa_error *error)
{
    if (!transport || !out || !present)
        return fail(error, QA_ERROR_ARGUMENT, "Missing native UDP policy observation");
    *out = (qa_net_udp_policy){0};
    *present = false;
    if (transport->ops.close == host_close) {
        const host_state *state = transport->state;
        return !state->children[0] || qa_net_udp_policy_read(state->children[0], out, present, error);
    }
    if (transport->ops.send != udp_send || transport->ops.collect != udp_receive ||
        transport->ops.close != udp_close) return true;
    const udp_state *state = transport->state;
    if (!state || state->fd < 0)
        return fail(error, QA_ERROR_ARGUMENT, "Native UDP socket is not retained");
    struct sockaddr_storage address;
    socklen_t size = sizeof(address);
    if (getsockname(state->fd, (struct sockaddr *)&address, &size) < 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Reading UDP bound address: %s", strerror(errno));
        return false;
    }
    qa_net_udp_policy actual = {0};
    if (!from_sockaddr((const struct sockaddr *)&address, size, &actual.bound) ||
        !qa_net_address_equal(&actual.bound, &transport->address, true) ||
        address.ss_family != (state->ipv6 ? AF_INET6 : AF_INET))
        return fail(error, QA_ERROR_ARGUMENT, "Native UDP bound address changed");
    if (state->ipv6) {
        int enabled = 0;
        size = sizeof(enabled);
        if (getsockopt(state->fd, IPPROTO_IPV6, IPV6_V6ONLY, &enabled, &size) < 0) {
            qa_error_set(error, QA_ERROR_IO, 0, "Reading UDP IPv6 policy: %s", strerror(errno));
            return false;
        }
        if (size != sizeof(enabled) || (enabled != 0 && enabled != 1))
            return fail(error, QA_ERROR_FORMAT, "Native UDP IPv6 policy is invalid");
        actual.ipv6_only = enabled != 0;
    }
    *out = actual;
    *present = true;
    return true;
}

bool qa_net_udp_open(const qa_net_udp_options *options, qa_net_transport **out, qa_error *error)
{
    if (options == NULL || out == NULL || !valid_address(&options->bind) ||
        (options->bind.kind != QA_NET_IPV4 && options->bind.kind != QA_NET_IPV6) ||
        options->limits.datagram_bytes == 0 || options->limits.datagram_bytes > 65507 || options->limits.queue_packets == 0)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid UDP configuration");
    udp_state *state = calloc(1, sizeof(*state));
    if (state == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate UDP transport");
    state->fd = -1;
    state->capacity = options->limits.datagram_bytes;
    state->bytes = malloc(state->capacity);
    state->ipv6 = options->bind.kind == QA_NET_IPV6;
    state->ipv6_only = state->ipv6 && options->ipv6_only;
    if (state->bytes == NULL) { free(state); return fail(error, QA_ERROR_MEMORY, "Cannot allocate UDP receive storage"); }
    state->fd = socket(state->ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (state->fd < 0) goto io_error;
    int flags = fcntl(state->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(state->fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(state->fd, F_SETFD, FD_CLOEXEC) < 0)
        goto io_error;
    size_t receive_bytes = options->limits.queue_packets > (size_t)INT_MAX / state->capacity
        ? (size_t)INT_MAX : options->limits.queue_packets * state->capacity;
    int receive_hint = (int)receive_bytes;
    if (setsockopt(state->fd, SOL_SOCKET, SO_RCVBUF, &receive_hint, sizeof(receive_hint)) < 0) goto io_error;
    if (options->broadcast) {
        int enabled = 1;
        if (setsockopt(state->fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0) goto io_error;
    }
    if (state->ipv6) {
        int enabled = options->ipv6_only ? 1 : 0;
        if (setsockopt(state->fd, IPPROTO_IPV6, IPV6_V6ONLY, &enabled, sizeof(enabled)) < 0) goto io_error;
    }
    struct sockaddr_storage address;
    socklen_t size;
    if (!to_sockaddr(&options->bind, false, &address, &size)) goto io_error;
    if (bind(state->fd, (struct sockaddr *)&address, size) < 0) goto io_error;
    size = sizeof(address);
    if (getsockname(state->fd, (struct sockaddr *)&address, &size) < 0) goto io_error;
    qa_net_address actual;
    if (!from_sockaddr((const struct sockaddr *)&address, size, &actual)) goto io_error;
    const qa_net_transport_ops ops = { .send = udp_send, .collect = udp_receive, .close = udp_close };
    if (!qa_net_transport_create(&actual, options->limits, &ops, state, out, error)) { udp_close(state); return false; }
    return true;
io_error:
    qa_error_set(error, QA_ERROR_IO, 0, "UDP socket setup failed: %s", strerror(errno));
    if (state->fd >= 0) close(state->fd);
    free(state->bytes);
    free(state);
    return false;
}

typedef struct loop_packet { qa_net_address from; uint8_t *bytes; size_t size; } loop_packet;
typedef struct loop_endpoint loop_endpoint;
struct qa_net_loopback { qa_net_limits limits; loop_endpoint *head; size_t references; bool closed; };
struct loop_endpoint {
    qa_net_loopback *hub;
    loop_endpoint *next;
    qa_net_address address;
    loop_packet *packets;
    size_t head, count;
    uint8_t *storage, *borrowed;
    bool closed, dropped;
    qa_net_address dropped_from;
};

static void loop_clear(loop_endpoint *endpoint)
{
    for (size_t index = 0; index < endpoint->hub->limits.queue_packets; ++index) {
        endpoint->packets[index].size = 0;
    }
    endpoint->head = 0;
    endpoint->count = 0;
    endpoint->dropped = false;
}

static qa_net_send_result loop_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *error)
{
    loop_endpoint *endpoint = context;
    if (endpoint->closed || endpoint->hub->closed) return fail(error, QA_ERROR_IO, "Loopback endpoint is closed");
    if (to->kind != QA_NET_LOOPBACK) return fail(error, QA_ERROR_ARGUMENT, "Loopback destination has the wrong family");
    loop_endpoint *peer = endpoint->hub->head;
    while (peer != NULL && !qa_net_address_equal(&peer->address, to, true)) peer = peer->next;
    if (peer == NULL || peer->closed) return fail(error, QA_ERROR_NOT_FOUND, "Loopback peer is not bound");
    size_t capacity = endpoint->hub->limits.queue_packets;
    if (peer->count == capacity) {
        loop_packet *oldest = &peer->packets[peer->head];
        peer->dropped_from = oldest->from;
        peer->dropped = true;
        peer->head = (peer->head + 1) % capacity;
        --peer->count;
    }
    size_t tail = (peer->head + peer->count) % capacity;
    loop_packet *packet = &peer->packets[tail];
    if (bytes.size != 0) memmove(packet->bytes, bytes.data, bytes.size);
    packet->from = endpoint->address;
    packet->size = bytes.size;
    ++peer->count;
    return true;
}

static bool loop_receive(void *context, uint64_t now_ns, qa_net_transport_event *event, qa_error *error)
{
    qa_net_datagram *out=&event->packet;
    loop_endpoint *endpoint = context;
    if (endpoint->closed || endpoint->hub->closed) return fail(error, QA_ERROR_IO, "Loopback endpoint is closed");
    *out = (qa_net_datagram){0};
    if (endpoint->dropped) {
        endpoint->dropped = false;
        out->kind = QA_NET_POLL_DROPPED;
        out->from = endpoint->dropped_from;
        out->received_ns = now_ns;
        return true;
    }
    if (endpoint->count == 0) return true;
    loop_packet packet = endpoint->packets[endpoint->head];
    endpoint->packets[endpoint->head].size = 0;
    endpoint->head = (endpoint->head + 1) % endpoint->hub->limits.queue_packets;
    --endpoint->count;
    if (packet.size != 0) memcpy(endpoint->borrowed, packet.bytes, packet.size);
    *out = (qa_net_datagram){ .kind = QA_NET_POLL_PACKET, .from = packet.from,
        .payload = { endpoint->borrowed, packet.size }, .received_ns = now_ns };
    return true;
}

static void loop_close(void *context)
{
    loop_endpoint *endpoint = context;
    qa_net_loopback *hub = endpoint->hub;
    loop_endpoint **link = &hub->head;
    while (*link != NULL && *link != endpoint) link = &(*link)->next;
    if (*link == endpoint) *link = endpoint->next;
    loop_clear(endpoint);
    free(endpoint->storage);
    free(endpoint->packets);
    free(endpoint);
    if (--hub->references == 0) free(hub);
}

static bool loop_ready(const void *context)
{
    const loop_endpoint *endpoint = context;
    return !endpoint->closed && !endpoint->hub->closed;
}

bool qa_net_loopback_create(qa_net_limits limits, qa_net_loopback **out, qa_error *error)
{
    if (out == NULL || limits.datagram_bytes == 0 || limits.datagram_bytes > 65535 ||
        limits.queue_packets == 0 || limits.queue_packets > SIZE_MAX / sizeof(loop_packet) ||
        limits.queue_packets >= SIZE_MAX / limits.datagram_bytes)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid loopback capacity");
    qa_net_loopback *hub = calloc(1, sizeof(*hub));
    if (hub == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate loopback hub");
    hub->limits = limits;
    hub->references = 1;
    *out = hub;
    return true;
}

bool qa_net_loopback_bind(qa_net_loopback *hub, const char *name, qa_net_transport **out, qa_error *error)
{
    if (hub == NULL || hub->closed || name == NULL || *name == '\0' || out == NULL)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid loopback binding");
    qa_net_address address = { .kind = QA_NET_LOOPBACK };
    size_t size = strlen(name);
    if (size >= sizeof(address.host.loopback)) return fail(error, QA_ERROR_ARGUMENT, "Loopback name is too long");
    memcpy(address.host.loopback, name, size + 1);
    for (loop_endpoint *entry = hub->head; entry != NULL; entry = entry->next)
        if (qa_net_address_equal(&entry->address, &address, true)) return fail(error, QA_ERROR_ARGUMENT, "Loopback name is already bound");
    if (hub->references == SIZE_MAX) return fail(error, QA_ERROR_MEMORY, "Loopback endpoint count overflow");
    loop_endpoint *endpoint = calloc(1, sizeof(*endpoint));
    if (endpoint == NULL) return fail(error, QA_ERROR_MEMORY, "Cannot allocate loopback endpoint");
    endpoint->packets = calloc(hub->limits.queue_packets, sizeof(*endpoint->packets));
    endpoint->storage = malloc((hub->limits.queue_packets + 1) * hub->limits.datagram_bytes);
    if (endpoint->packets == NULL || endpoint->storage == NULL) {
        free(endpoint->packets); free(endpoint->storage); free(endpoint);
        return fail(error, QA_ERROR_MEMORY, "Cannot allocate loopback queue");
    }
    for (size_t index = 0; index < hub->limits.queue_packets; ++index)
        endpoint->packets[index].bytes = endpoint->storage + index * hub->limits.datagram_bytes;
    endpoint->borrowed = endpoint->storage + hub->limits.queue_packets * hub->limits.datagram_bytes;
    endpoint->hub = hub;
    endpoint->address = address;
    const qa_net_transport_ops ops = { .send = loop_send, .collect = loop_receive,
        .close = loop_close, .ready = loop_ready };
    if (!qa_net_transport_create(&address, hub->limits, &ops, endpoint, out, error)) {
        free(endpoint->packets);
        free(endpoint->storage);
        free(endpoint);
        return false;
    }
    endpoint->next = hub->head;
    hub->head = endpoint;
    ++hub->references;
    return true;
}

void qa_net_loopback_close(qa_net_loopback *hub)
{
    if (hub == NULL || hub->closed) return;
    hub->closed = true;
    for (loop_endpoint *endpoint = hub->head; endpoint != NULL; endpoint = endpoint->next) {
        endpoint->closed = true;
        loop_clear(endpoint);
    }
    if (--hub->references == 0) free(hub);
}
