#include "qa/network.h"

#include <stdlib.h>
#include <string.h>

enum { DOSBOX_WIRE_BYTES = 1424, DOSBOX_PAYLOAD_BYTES = 1394,
       DOSBOX_SOCKETS = 150, DOSBOX_QUEUE = 256, DOSBOX_PUMP_BUDGET = 256 };

typedef enum dosbox_phase { DOSBOX_REGISTERING, DOSBOX_READY, DOSBOX_FAILED, DOSBOX_CLOSED } dosbox_phase;
typedef struct dosbox_packet {
    qa_net_address from;
    uint64_t received_ns;
    size_t size;
    uint8_t data[];
} dosbox_packet;
typedef struct dosbox_socket dosbox_socket;

struct qa_net_dosbox {
    qa_net_transport *udp;
    qa_net_address server, address;
    dosbox_socket *sockets[DOSBOX_SOCKETS];
    size_t references, socket_count;
    uint64_t started_ns, timeout_ns, now_ns;
    dosbox_phase phase;
    qa_error failure;
};

struct dosbox_socket {
    qa_net_dosbox *network;
    qa_net_address address, dropped_from;
    uint8_t packet_type;
    size_t slot, head, count;
    bool dropped;
    dosbox_packet *packets[DOSBOX_QUEUE], *borrowed;
};

static bool dosbox_node_is(const qa_net_address *address, uint8_t byte) {
    for (size_t i = 0; i < 6; ++i) if (address->host.ipx.node[i] != byte) return false;
    return true;
}

static void dosbox_release(qa_net_dosbox *network) {
    if (--network->references == 0) free(network);
}

static bool dosbox_fail(qa_net_dosbox *network, qa_status code, const char *message, qa_error *error) {
    network->phase = DOSBOX_FAILED;
    qa_error_set(&network->failure, code, 0, "%s", message);
    qa_net_transport_close(network->udp);
    network->udp = NULL;
    if (error) *error = network->failure;
    return false;
}

static bool dosbox_ready(qa_net_dosbox *network, qa_error *error) {
    if (network->phase == DOSBOX_READY) return true;
    if (network->phase == DOSBOX_FAILED) {
        if (error) *error = network->failure;
    } else qa_error_set(error, QA_ERROR_IO, 0, network->phase == DOSBOX_REGISTERING ?
                        "DOSBox IPX registration is pending" : "DOSBox IPX network is closed");
    return false;
}

static dosbox_socket *dosbox_find(qa_net_dosbox *network, uint16_t port) {
    for (size_t i = 0; i < DOSBOX_SOCKETS; ++i)
        if (network->sockets[i] && network->sockets[i]->address.port == port) return network->sockets[i];
    return NULL;
}

static void dosbox_accept(dosbox_socket *socket, const qa_net_ipx_packet *packet, uint64_t now_ns) {
    if (packet->payload.size > DOSBOX_PAYLOAD_BYTES) {
        socket->dropped = true;
        socket->dropped_from = packet->from;
        return;
    }
    dosbox_packet *queued = malloc(sizeof(*queued) + packet->payload.size);
    if (!queued) {
        socket->dropped = true;
        socket->dropped_from = packet->from;
        return;
    }
    queued->from = packet->from;
    queued->received_ns = now_ns;
    queued->size = packet->payload.size;
    if (queued->size) memcpy(queued->data, packet->payload.data, queued->size);
    if (socket->count == DOSBOX_QUEUE) {
        socket->dropped_from = socket->packets[socket->head]->from;
        free(socket->packets[socket->head]);
        socket->head = (socket->head + 1) % DOSBOX_QUEUE;
        --socket->count;
        socket->dropped = true;
    }
    socket->packets[(socket->head + socket->count) % DOSBOX_QUEUE] = queued;
    ++socket->count;
}

static bool dosbox_udp_packet(qa_net_dosbox *network, const qa_net_ipx_packet *packet,
                              qa_error *error) {
    uint8_t buffer[DOSBOX_WIRE_BYTES];
    qa_net_writer writer;
    qa_net_writer_init(&writer, buffer, sizeof(buffer), error);
    if (!qa_net_ipx_encode(packet, &writer)) return false;
    return qa_net_transport_send(network->udp, &network->server,
                                 (qa_bytes){buffer, qa_net_writer_size(&writer)}, error);
}

static void dosbox_deliver(qa_net_dosbox *network, const qa_net_ipx_packet *packet,
                            uint64_t received_ns) {
    bool local = qa_net_address_equal(&packet->to, &network->address, false);
    bool broadcast = dosbox_node_is(&packet->to, 255) &&
        (!packet->to.host.ipx.network || packet->to.host.ipx.network == network->address.host.ipx.network);
    if (!local && !broadcast) return;
    if (packet->to.port == 2) {
        if (broadcast && packet->from.port == 2 && !packet->payload.size) {
            qa_net_ipx_packet response = {network->address, packet->from, 0, 0, {NULL, 0}};
            /* DOSBox sends periodic broadcast probes on its reserved socket. */
            (void)dosbox_udp_packet(network, &response, NULL);
        }
        return;
    }
    dosbox_socket *socket = dosbox_find(network, packet->to.port);
    if (socket) dosbox_accept(socket, packet, received_ns);
}

bool qa_net_dosbox_create(qa_net_transport *udp, const qa_net_address *server,
                          uint64_t now_ns, uint64_t timeout_ns,
                          qa_net_dosbox **out, qa_error *error) {
    const qa_net_address *bound = qa_net_transport_address(udp);
    if (!out || !server || server->kind != QA_NET_IPV4 || !server->port || !timeout_ns ||
        !bound || bound->kind != QA_NET_IPV4 || qa_net_transport_limit(udp) < DOSBOX_WIRE_BYTES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid DOSBox IPX registration options");
        return false;
    }
    qa_net_dosbox *network = calloc(1, sizeof(*network));
    if (!network) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "DOSBox IPX network allocation failed");
        return false;
    }
    network->udp = udp;
    network->server = *server;
    network->address.kind = QA_NET_IPX;
    network->address.port = 2;
    network->references = 1;
    network->started_ns = network->now_ns = now_ns;
    network->timeout_ns = timeout_ns;
    network->phase = DOSBOX_REGISTERING;
    qa_net_ipx_packet request = {network->address, network->address, 0, 0, {NULL, 0}};
    if (!dosbox_udp_packet(network, &request, error)) {
        free(network);
        return false;
    }
    *out = network;
    return true;
}

bool qa_net_dosbox_collect(qa_net_dosbox *network, uint64_t now_ns,
    qa_net_collect_policy policy, qa_net_transport_event *out, qa_error *error) {
    if (network->phase == DOSBOX_FAILED || network->phase == DOSBOX_CLOSED)
        return dosbox_ready(network, error);
    if (!qa_net_transport_collect(network->udp, now_ns, policy, out, error))
        return dosbox_fail(network, QA_ERROR_IO, "DOSBox IPX UDP transport failed", error);
    return true;
}

static bool dosbox_dispatch_body(qa_net_dosbox *network, const qa_net_transport_event *event,
    bool *processed, qa_error *error) {
    qa_net_datagram input;
    if (!qa_net_transport_dispatch(network->udp, event, &input, processed, error))
        return dosbox_fail(network, QA_ERROR_IO, "DOSBox IPX UDP transport failed", error);
    if (*processed && input.received_ns > network->now_ns) network->now_ns = input.received_ns;
    if (!*processed || input.kind != QA_NET_POLL_PACKET || input.payload.size > DOSBOX_WIRE_BYTES ||
        !qa_net_address_equal(&input.from, &network->server, true)) return true;
    qa_net_ipx_packet packet;
    if (!qa_net_ipx_decode(input.payload, &packet, NULL)) return true;
    if (network->phase == DOSBOX_REGISTERING) {
        if (packet.payload.size || packet.from.port != 2 || packet.to.port != 2 ||
            packet.from.host.ipx.network != 1 || dosbox_node_is(&packet.to, 0) ||
            dosbox_node_is(&packet.to, 255)) return true;
        network->address = packet.to;
        network->phase = DOSBOX_READY;
    } else dosbox_deliver(network, &packet, input.received_ns);
    return true;
}

bool qa_net_dosbox_dispatch(qa_net_dosbox *network, const qa_net_transport_event *event,
    bool *registered, qa_error *error) {
    bool processed;
    bool ok = dosbox_dispatch_body(network, event, &processed, error);
    *registered = network->phase == DOSBOX_READY;
    return ok;
}

bool qa_net_dosbox_maintenance(qa_net_dosbox *network, uint64_t now_ns, qa_error *error) {
    if (network->phase == DOSBOX_FAILED || network->phase == DOSBOX_CLOSED)
        return dosbox_ready(network, error);
    if (now_ns < network->now_ns)
        return dosbox_fail(network, QA_ERROR_ARGUMENT, "DOSBox IPX clock moved backwards", error);
    network->now_ns = now_ns;
    if (network->phase == DOSBOX_REGISTERING && now_ns - network->started_ns >= network->timeout_ns)
        return dosbox_fail(network, QA_ERROR_IO, "DOSBox IPX registration timed out", error);
    if (!qa_net_transport_maintenance(network->udp, now_ns, error))
        return dosbox_fail(network, QA_ERROR_IO, "DOSBox IPX UDP transport failed", error);
    return true;
}

static void dosbox_queue_local(qa_net_dosbox *network, const qa_net_ipx_packet *packet) {
    bool local = qa_net_address_equal(&packet->to, &network->address, false);
    bool broadcast = dosbox_node_is(&packet->to, 255) &&
        (!packet->to.host.ipx.network || packet->to.host.ipx.network == network->address.host.ipx.network);
    if (!local && !broadcast) return;
    dosbox_socket *socket = dosbox_find(network, packet->to.port);
    if (socket) dosbox_accept(socket, packet, network->now_ns);
}

static bool dosbox_send(void *opaque, const qa_net_address *to, qa_bytes bytes, qa_error *error) {
    dosbox_socket *socket = opaque;
    qa_net_dosbox *network = socket->network;
    if (!dosbox_ready(network, error)) return false;
    if (!to || to->kind != QA_NET_IPX || !to->port || bytes.size > DOSBOX_PAYLOAD_BYTES ||
        (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid DOSBox IPX datagram");
        return false;
    }
    qa_net_ipx_packet packet = {socket->address, *to, socket->packet_type, 0, bytes};
    bool local = qa_net_address_equal(to, &network->address, false);
    bool sent = local || dosbox_udp_packet(network, &packet, error);
    if (local || dosbox_node_is(to, 255)) dosbox_queue_local(network, &packet);
    return sent;
}

static bool dosbox_take(dosbox_socket *socket, uint64_t now_ns, qa_net_datagram *out) {
    free(socket->borrowed);
    socket->borrowed = NULL;
    *out = (qa_net_datagram){.kind = QA_NET_POLL_EMPTY};
    if (socket->dropped) {
        socket->dropped = false;
        out->kind = QA_NET_POLL_DROPPED;
        out->from = socket->dropped_from;
        out->received_ns = now_ns;
        return true;
    }
    if (!socket->count) return false;
    dosbox_packet *packet = socket->packets[socket->head];
    socket->head = (socket->head + 1) % DOSBOX_QUEUE;
    --socket->count;
    socket->borrowed = packet;
    *out = (qa_net_datagram){QA_NET_POLL_PACKET, packet->from,
                           {packet->data, packet->size}, packet->received_ns};
    return true;
}

static bool dosbox_collect(void *opaque, uint64_t now_ns, qa_net_collect_policy policy,
    qa_net_transport_event *out, qa_error *error) {
    dosbox_socket *socket = opaque;
    *out = (qa_net_transport_event){0};
    if (socket->dropped || socket->count) {
        (void)dosbox_take(socket, now_ns, &out->packet);
        out->source = 1;
        return true;
    }
    if (!qa_net_dosbox_collect(socket->network, now_ns, policy, out, error)) return false;
    out->source <<= 2;
    return true;
}

static bool dosbox_dispatch(void *opaque, const qa_net_transport_event *event,
    qa_net_datagram *out, bool *present, qa_error *error) {
    dosbox_socket *socket = opaque;
    if (event && (event->source & 3u) == 1) { *out = event->packet; *present = true; return true; }
    qa_net_transport_event child;
    if (event) { child = *event; child.source >>= 2; }
    for (size_t i = 0; i < (event ? 1U : DOSBOX_PUMP_BUDGET); ++i) {
        bool processed;
        if (!dosbox_dispatch_body(socket->network, event ? &child : NULL, &processed, error)) return false;
        *present = dosbox_take(socket, socket->network->now_ns, out);
        if (*present || event || !processed) return true;
    }
    *present = false;
    return true;
}

static bool dosbox_maintenance(void *opaque, uint64_t now_ns, qa_error *error) {
    dosbox_socket *socket = opaque;
    return qa_net_dosbox_maintenance(socket->network, now_ns, error);
}

static void dosbox_close_socket(void *opaque) {
    dosbox_socket *socket = opaque;
    qa_net_dosbox *network = socket->network;
    network->sockets[socket->slot] = NULL;
    --network->socket_count;
    for (size_t i = 0; i < socket->count; ++i)
        free(socket->packets[(socket->head + i) % DOSBOX_QUEUE]);
    free(socket->borrowed);
    free(socket);
    dosbox_release(network);
}

static bool dosbox_socket_ready(const void *opaque) {
    const dosbox_socket *socket = opaque;
    return socket->network->phase == DOSBOX_READY;
}

bool qa_net_dosbox_bind(qa_net_dosbox *network, uint16_t port, uint8_t packet_type,
                        qa_net_transport **out, qa_error *error) {
    if (!network || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid DOSBox socket arguments");
        return false;
    }
    if (!dosbox_ready(network, error)) return false;
    if (network->socket_count == DOSBOX_SOCKETS) {
        qa_error_set(error, QA_ERROR_IO, 0, "DOSBox IPX socket table is full");
        return false;
    }
    if (!port) {
        port = 0x4002;
        while (dosbox_find(network, port) && port < 0x7fff) ++port;
    }
    if (port == 2 || dosbox_find(network, port)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "DOSBox IPX socket is reserved or already bound");
        return false;
    }
    dosbox_socket *socket = calloc(1, sizeof(*socket));
    if (!socket) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "DOSBox IPX socket allocation failed");
        return false;
    }
    socket->network = network;
    socket->address = network->address;
    socket->address.port = port;
    socket->packet_type = packet_type;
    while (network->sockets[socket->slot]) ++socket->slot;
    const qa_net_transport_ops ops = {
        .send = dosbox_send, .collect = dosbox_collect, .dispatch = dosbox_dispatch,
        .maintenance = dosbox_maintenance, .close = dosbox_close_socket,
        .ready = dosbox_socket_ready
    };
    if (!qa_net_transport_create(&socket->address, (qa_net_limits){DOSBOX_PAYLOAD_BYTES, DOSBOX_QUEUE},
                                 &ops, socket, out, error)) {
        free(socket);
        return false;
    }
    network->sockets[socket->slot] = socket;
    ++network->socket_count;
    ++network->references;
    return true;
}

void qa_net_dosbox_destroy(qa_net_dosbox *network) {
    if (!network) return;
    network->phase = DOSBOX_CLOSED;
    qa_net_transport_close(network->udp);
    network->udp = NULL;
    /* Open transport handles keep the shared node alive until their close. */
    dosbox_release(network);
}
