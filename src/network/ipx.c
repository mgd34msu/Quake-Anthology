#include "qa/network.h"
#include "socket_private.h"

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

enum { IPX_HEADER = 30, IPX_MAX_PAYLOAD = 65505, IPX_POLL_BUDGET = 256 };

static uint16_t ipx_u16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static void ipx_put16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void ipx_write_address(uint8_t *data, const qa_net_address *address) {
    uint32_t network = address->host.ipx.network;
    data[0] = (uint8_t)(network >> 24);
    data[1] = (uint8_t)(network >> 16);
    data[2] = (uint8_t)(network >> 8);
    data[3] = (uint8_t)network;
    memcpy(data + 4, address->host.ipx.node, 6);
    ipx_put16(data + 10, address->port);
}

static qa_net_address ipx_read_address(const uint8_t *data) {
    qa_net_address address = {0};
    address.kind = QA_NET_IPX;
    address.host.ipx.network = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
                               (uint32_t)data[2] << 8 | data[3];
    memcpy(address.host.ipx.node, data + 4, 6);
    address.port = ipx_u16(data + 10);
    return address;
}

static bool ipx_broadcast(const qa_net_address *address) {
    for (size_t i = 0; i < 6; ++i) if (address->host.ipx.node[i] != 255) return false;
    return true;
}

static void ipx_sequence_bytes(uint8_t *data, uint32_t sequence) {
    data[0] = (uint8_t)sequence;
    data[1] = (uint8_t)(sequence >> 8);
    data[2] = (uint8_t)(sequence >> 16);
    data[3] = (uint8_t)(sequence >> 24);
}

bool qa_net_ipx_encode(const qa_net_ipx_packet *packet, qa_net_writer *writer) {
    if (!writer) return false;
    if (!packet || packet->from.kind != QA_NET_IPX || packet->to.kind != QA_NET_IPX ||
        !packet->from.port || !packet->to.port || packet->payload.size > IPX_MAX_PAYLOAD ||
        (packet->payload.size && !packet->payload.data))
        return qa_net_writer_fail(writer, "Invalid IPX packet");
    uint8_t header[IPX_HEADER] = {255, 255};
    ipx_put16(header + 2, (uint16_t)(packet->payload.size + IPX_HEADER));
    header[4] = packet->hops;
    header[5] = packet->packet_type;
    ipx_write_address(header + 6, &packet->to);
    ipx_write_address(header + 18, &packet->from);
    return qa_net_write_data(writer, header, sizeof(header)) &&
           qa_net_write_data(writer, packet->payload.data, packet->payload.size);
}

bool qa_net_ipx_decode(qa_bytes bytes, qa_net_ipx_packet *out, qa_error *error) {
    if (!out || !bytes.data || bytes.size < IPX_HEADER || bytes.size > UINT16_MAX ||
        ipx_u16(bytes.data) != UINT16_MAX || ipx_u16(bytes.data + 2) != bytes.size ||
        !ipx_u16(bytes.data + 16) || !ipx_u16(bytes.data + 28)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid IPX packet header or length");
        return false;
    }
    *out = (qa_net_ipx_packet){
        .from = ipx_read_address(bytes.data + 18),
        .to = ipx_read_address(bytes.data + 6),
        .packet_type = bytes.data[5], .hops = bytes.data[4],
        .payload = {bytes.data + IPX_HEADER, bytes.size - IPX_HEADER}
    };
    return true;
}

typedef struct ipx_peer {
    qa_net_address ipx, udp;
} ipx_peer;

struct qa_net_ipx_tunnel {
    qa_net_transport *udp, *transport;
    qa_net_address local;
    ipx_peer *peers;
    size_t peer_count, peer_capacity, payload_limit;
    uint8_t *send_buffer;
    uint32_t sequence;
    uint8_t packet_type;
    bool quake_sequence;
};

static ipx_peer *ipx_find_peer(qa_net_ipx_tunnel *tunnel, const qa_net_address *ipx) {
    for (size_t i = 0; i < tunnel->peer_count; ++i)
        if (qa_net_address_equal(&tunnel->peers[i].ipx, ipx, false)) return &tunnel->peers[i];
    return NULL;
}

static bool ipx_tunnel_send(void *opaque, const qa_net_address *to, qa_bytes payload,
                            qa_error *error) {
    qa_net_ipx_tunnel *tunnel = opaque;
    if (!to || to->kind != QA_NET_IPX || !to->port || payload.size > tunnel->payload_limit ||
        (payload.size && !payload.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid IPX tunnel destination or payload");
        return false;
    }
    size_t prefix = tunnel->quake_sequence ? 4U : 0U;
    if (prefix) ipx_sequence_bytes(tunnel->send_buffer + IPX_HEADER, tunnel->sequence++);
    if (payload.size) memcpy(tunnel->send_buffer + IPX_HEADER + prefix, payload.data, payload.size);
    qa_net_ipx_packet packet = {tunnel->local, *to, tunnel->packet_type, 0,
                               {tunnel->send_buffer + IPX_HEADER, payload.size + prefix}};
    /* The payload is already in place; encode only the fixed header. */
    uint8_t header[IPX_HEADER];
    memset(header, 0, sizeof(header));
    header[0] = header[1] = 255;
    ipx_put16(header + 2, (uint16_t)(IPX_HEADER + packet.payload.size));
    header[5] = packet.packet_type;
    ipx_write_address(header + 6, &packet.to);
    ipx_write_address(header + 18, &packet.from);
    memcpy(tunnel->send_buffer, header, IPX_HEADER);
    qa_bytes wire = {tunnel->send_buffer, IPX_HEADER + packet.payload.size};
    if (!ipx_broadcast(to)) {
        ipx_peer *peer = ipx_find_peer(tunnel, to);
        if (!peer) {
            qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "No UDP peer for IPX destination");
            return false;
        }
        return qa_net_transport_send(tunnel->udp, &peer->udp, wire, error);
    }
    bool sent = false, attempted = false;
    qa_error first_error = {0};
    for (size_t i = 0; i < tunnel->peer_count; ++i) {
        ipx_peer *peer = &tunnel->peers[i];
        if (to->host.ipx.network && to->host.ipx.network != peer->ipx.host.ipx.network) continue;
        qa_error send_error = {0};
        if (qa_net_transport_send(tunnel->udp, &peer->udp, wire, &send_error)) sent = true;
        else if (!attempted) first_error = send_error;
        attempted = true;
    }
    if (!sent) {
        if (attempted && error) *error = first_error;
        if (!attempted) qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "No IPX broadcast peers");
    }
    return sent;
}

static bool ipx_tunnel_receive(void *opaque, uint64_t now_ns, qa_net_datagram *out,
                               qa_error *error) {
    qa_net_ipx_tunnel *tunnel = opaque;
    for (size_t i = 0; i < IPX_POLL_BUDGET; ++i) {
        qa_net_datagram event;
        if (!qa_net_transport_receive(tunnel->udp, now_ns, &event, error)) return false;
        if (event.kind == QA_NET_POLL_EMPTY) { *out = event; return true; }
        if (event.kind != QA_NET_POLL_PACKET) continue;
        qa_net_ipx_packet packet;
        if (!qa_net_ipx_decode(event.payload, &packet, NULL)) continue;
        ipx_peer *peer = ipx_find_peer(tunnel, &packet.from);
        if (!peer || !qa_net_address_equal(&peer->udp, &event.from, true)) continue;
        bool broadcast = ipx_broadcast(&packet.to) &&
            (!packet.to.host.ipx.network || packet.to.host.ipx.network == tunnel->local.host.ipx.network);
        if (packet.to.port != tunnel->local.port ||
            (!broadcast && !qa_net_address_equal(&packet.to, &tunnel->local, true)) ||
            packet.packet_type != tunnel->packet_type) continue;
        size_t prefix = tunnel->quake_sequence ? 4U : 0U;
        if (packet.payload.size < prefix) continue;
        *out = (qa_net_datagram){QA_NET_POLL_PACKET, packet.from,
            {packet.payload.data + prefix, packet.payload.size - prefix}, event.received_ns};
        return true;
    }
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    return true;
}

static void ipx_tunnel_close(void *opaque) {
    qa_net_ipx_tunnel *tunnel = opaque;
    qa_net_transport_close(tunnel->udp);
    free(tunnel->send_buffer);
    free(tunnel->peers);
    free(tunnel);
}

static bool ipx_tunnel_ready(const void *opaque) {
    const qa_net_ipx_tunnel *tunnel = opaque;
    return qa_net_transport_ready(tunnel->udp);
}

bool qa_net_ipx_tunnel_create(qa_net_transport *udp, const qa_net_address *local,
                             bool quake_sequence, uint8_t packet_type,
                             qa_net_ipx_tunnel **out, qa_error *error) {
    const qa_net_address *udp_address = qa_net_transport_address(udp);
    size_t overhead = IPX_HEADER + (quake_sequence ? 4U : 0U);
    size_t wire_limit = qa_net_transport_limit(udp);
    if (!out || !local || local->kind != QA_NET_IPX || !local->port || !udp_address ||
        (udp_address->kind != QA_NET_IPV4 && udp_address->kind != QA_NET_IPV6) ||
        wire_limit <= overhead) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid IPX tunnel options");
        return false;
    }
    if (wire_limit > UINT16_MAX) wire_limit = UINT16_MAX;
    qa_net_ipx_tunnel *tunnel = calloc(1, sizeof(*tunnel));
    if (!tunnel) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX tunnel allocation failed");
        return false;
    }
    tunnel->send_buffer = malloc(wire_limit);
    if (!tunnel->send_buffer) {
        free(tunnel);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX packet allocation failed");
        return false;
    }
    tunnel->udp = udp;
    tunnel->local = *local;
    tunnel->payload_limit = wire_limit - overhead;
    tunnel->quake_sequence = quake_sequence;
    tunnel->packet_type = quake_sequence ? 4 : packet_type;
    const qa_net_transport_ops ops = {
        .send = ipx_tunnel_send, .receive = ipx_tunnel_receive, .close = ipx_tunnel_close,
        .ready = ipx_tunnel_ready
    };
    if (!qa_net_transport_create(local, (qa_net_limits){tunnel->payload_limit, 256},
                                 &ops, tunnel, &tunnel->transport, error)) {
        free(tunnel->send_buffer);
        free(tunnel);
        return false;
    }
    *out = tunnel;
    return true;
}

qa_net_transport *qa_net_ipx_tunnel_transport(qa_net_ipx_tunnel *tunnel) {
    return tunnel ? tunnel->transport : NULL;
}

bool qa_net_ipx_tunnel_peer(qa_net_ipx_tunnel *tunnel, const qa_net_address *ipx,
                           const qa_net_address *udp, qa_error *error) {
    if (!tunnel || !ipx || ipx->kind != QA_NET_IPX || !udp || !udp->port ||
        (udp->kind != QA_NET_IPV4 && udp->kind != QA_NET_IPV6) || ipx_broadcast(ipx)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid IPX tunnel peer");
        return false;
    }
    ipx_peer *peer = ipx_find_peer(tunnel, ipx);
    if (!peer) {
        if (tunnel->peer_count == tunnel->peer_capacity) {
            size_t capacity = tunnel->peer_capacity ? tunnel->peer_capacity * 2 : 8;
            if (capacity < tunnel->peer_capacity || capacity > SIZE_MAX / sizeof(*peer)) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX peer table exceeds capacity");
                return false;
            }
            ipx_peer *peers = realloc(tunnel->peers, capacity * sizeof(*peer));
            if (!peers) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX peer allocation failed");
                return false;
            }
            tunnel->peers = peers;
            tunnel->peer_capacity = capacity;
        }
        peer = &tunnel->peers[tunnel->peer_count++];
    }
    *peer = (ipx_peer){*ipx, *udp};
    return true;
}

bool qa_net_ipx_tunnel_remove(qa_net_ipx_tunnel *tunnel, const qa_net_address *ipx) {
    if (!tunnel || !ipx || ipx->kind != QA_NET_IPX) return false;
    ipx_peer *peer = ipx_find_peer(tunnel, ipx);
    if (!peer) return false;
    size_t index = (size_t)(peer - tunnel->peers);
    memmove(peer, peer + 1, (tunnel->peer_count - index - 1) * sizeof(*peer));
    --tunnel->peer_count;
    return true;
}

void qa_net_ipx_tunnel_destroy(qa_net_ipx_tunnel *tunnel) {
    if (tunnel) qa_net_transport_close(tunnel->transport);
}

typedef struct ipx_game {
    qa_net_transport *raw;
    uint8_t *send_buffer;
    size_t payload_limit;
    uint32_t sequence;
} ipx_game;

static bool ipx_game_send(void *opaque, const qa_net_address *to, qa_bytes payload, qa_error *error) {
    ipx_game *game = opaque;
    if (!to || to->kind != QA_NET_IPX || !to->port || payload.size > game->payload_limit ||
        (payload.size && !payload.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid game IPX datagram");
        return false;
    }
    ipx_sequence_bytes(game->send_buffer, game->sequence++);
    if (payload.size) memcpy(game->send_buffer + 4, payload.data, payload.size);
    return qa_net_transport_send(game->raw, to,
                                 (qa_bytes){game->send_buffer, payload.size + 4}, error);
}

static bool ipx_game_receive(void *opaque, uint64_t now_ns, qa_net_datagram *out, qa_error *error) {
    ipx_game *game = opaque;
    for (size_t i = 0; i < IPX_POLL_BUDGET; ++i) {
        if (!qa_net_transport_receive(game->raw, now_ns, out, error)) return false;
        if (out->kind != QA_NET_POLL_PACKET) return true;
        if (out->payload.size < 4) continue;
        out->payload.data += 4;
        out->payload.size -= 4;
        return true;
    }
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    return true;
}

static void ipx_game_close(void *opaque) {
    ipx_game *game = opaque;
    qa_net_transport_close(game->raw);
    free(game->send_buffer);
    free(game);
}

static bool ipx_game_ready(const void *opaque) {
    const ipx_game *game = opaque;
    return qa_net_transport_ready(game->raw);
}

bool qa_net_ipx_game_wrap(qa_net_transport *raw, bool quake_sequence,
                         qa_net_transport **out, qa_error *error) {
    const qa_net_address *address = qa_net_transport_address(raw);
    size_t limit = qa_net_transport_limit(raw);
    if (!out || !address || address->kind != QA_NET_IPX ||
        (quake_sequence && limit <= 4)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid IPX game transport");
        return false;
    }
    if (!quake_sequence) { *out = raw; return true; }
    ipx_game *game = calloc(1, sizeof(*game));
    if (!game) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX game transport allocation failed");
        return false;
    }
    game->send_buffer = malloc(limit);
    if (!game->send_buffer) {
        free(game);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "IPX game packet allocation failed");
        return false;
    }
    game->raw = raw;
    game->payload_limit = limit - 4;
    const qa_net_transport_ops ops = {
        .send = ipx_game_send, .receive = ipx_game_receive, .close = ipx_game_close,
        .ready = ipx_game_ready
    };
    if (!qa_net_transport_create(address, (qa_net_limits){limit - 4, 256}, &ops, game, out, error)) {
        free(game->send_buffer);
        free(game);
        return false;
    }
    return true;
}

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
    uint8_t wire[12];
    ipx_write_address(wire, address);
    native.sa_family = AF_IPX;
    memcpy(native.sa_netnum, wire, 4);
    memcpy(native.sa_nodenum, wire + 4, 6);
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

static bool ipx_native_send(void *opaque, const qa_net_address *to, qa_bytes bytes, qa_error *error) {
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

static bool ipx_native_receive(void *opaque, uint64_t now_ns, qa_net_datagram *out, qa_error *error) {
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

bool qa_net_ipx_native_open(const qa_net_address *local, qa_net_limits limits,
                           uint8_t packet_type, qa_net_transport **out, qa_error *error) {
    if (!local || local->kind != QA_NET_IPX || !out || !limits.datagram_bytes ||
        limits.datagram_bytes > IPX_MAX_PAYLOAD || !limits.queue_packets) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid native IPX options");
        return false;
    }
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
        .send = ipx_native_send, .receive = ipx_native_receive, .close = ipx_native_close
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
