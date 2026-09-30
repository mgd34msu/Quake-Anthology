#include "internal.h"
#include "qa/network_q1_runtime.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_nq.h"
#include <stdlib.h>
#include <string.h>

typedef struct nq_pending {
    struct nq_pending *next;
    qa_buffer bytes;
} nq_pending;
typedef struct nq_server {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_q1_peer native;
    qa_network_nq_server_policy policy;
    qa_network_nq_server_hooks hooks;
    nq_pending *first, *last;
    size_t queued_bytes, queued_messages;
    uint64_t input_sequence;
    uint8_t stage;
    bool started, retiring, signon_active;
} nq_server;
static bool queue(void *context, qa_bytes bytes, qa_error *error)
{
    nq_server *peer = context;
    if ((bytes.size && !bytes.data) || bytes.size > peer->policy.message_bytes ||
        bytes.size > peer->policy.queued_bytes - peer->queued_bytes || peer->queued_messages == SIZE_MAX)
        return qa_network_fail(error, "Original NetQuake reliable queue exceeds its admitted bounds");
    if (!bytes.size) return true;
    nq_pending *pending = calloc(1, sizeof(*pending));
    if (pending) pending->bytes.data = malloc(bytes.size);
    if (!pending || !pending->bytes.data) {
        free(pending); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original NetQuake reliable message"); return false;
    }
    memcpy(pending->bytes.data, bytes.data, bytes.size); pending->bytes.size = bytes.size;
    if (peer->last) peer->last->next = pending; else peer->first = pending;
    peer->last = pending; peer->queued_bytes += bytes.size; ++peer->queued_messages; return true;
}
static void queue_clear(nq_server *peer)
{
    while (peer->first) {
        nq_pending *next = peer->first->next;
        qa_buffer_free(&peer->first->bytes); free(peer->first); peer->first = next;
    }
    peer->last = NULL; peer->queued_bytes = peer->queued_messages = 0;
}
static bool signon(nq_server *peer, uint8_t stage, qa_error *error)
{
    if (peer->signon_active || stage < 1 || stage > 3)
        return qa_network_fail(error, "Recursive or invalid original NetQuake signon stage");
    peer->signon_active = true;
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    bool ok = peer->hooks.signon(peer->hooks.context, peer->id, stage, queue, peer, error);
    peer->runtime->callback = previous;
    uint8_t service[2] = {QA_NQ_SIGNON, stage};
    if (ok) ok = queue(peer, (qa_bytes){service, sizeof(service)}, error);
    peer->signon_active = false;
    if (ok) { peer->stage = stage; peer->started = true; }
    return ok;
}
static bool retire(nq_server *peer, const char *reason, bool notify, qa_error *error)
{
    if (peer->retiring) return true;
    peer->retiring = true;
    qa_error send_error = {0}; bool sent = true;
    if (notify) {
        uint8_t disconnect = QA_NQ_DISCONNECT; qa_bytes packet;
        sent = qa_nq_channel_unreliable(peer->native.channel.nq, (qa_bytes){&disconnect, 1}, &packet, &send_error) &&
            qa_q1_peer_send(&peer->native, packet, &send_error);
    }
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    bool dropped = peer->hooks.drop(peer->hooks.context, peer->id, reason, error);
    peer->runtime->callback = previous;
    if (dropped && !sent && error) *error = send_error;
    return dropped && sent;
}
static bool string_command(nq_server *peer, const char *text, qa_error *error)
{
    const char *cursor = text; char name[32]; bool present;
    if (!qa_q1_token(&cursor, false, name, sizeof(name), &present, error)) return false;
    if (!present) return true;
    if (!strcmp(name, "prespawn")) return peer->stage != 1 || signon(peer, 2, error);
    if (!strcmp(name, "spawn")) return peer->stage != 2 || signon(peer, 3, error);
    if (!strcmp(name, "begin")) {
        if (peer->stage != 3) return true;
        if (!peer->hooks.begin(peer->hooks.context, peer->id, error) ||
            !qa_network_phase(peer->runtime, peer->id, QA_NET_ACTIVE, error)) return false;
        peer->stage = 4; return true;
    }
    return peer->hooks.command(peer->hooks.context, peer->id, text, error);
}
static bool receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *error)
{
    nq_server *peer = context;
    if (packet->payload.size >= 4 && (packet->payload.data[0] & 128)) {
        return !runtime->options.hooks.connectionless ||
            runtime->options.hooks.connectionless(runtime->options.hooks.context, runtime, packet, error);
    }
    qa_q1_delivery delivery;
    if (!qa_q1_peer_receive(&peer->native, &packet->from, packet->payload, packet->received_ns, &delivery, error) ||
        !qa_network_received(runtime, id, packet->received_ns, error)) return false;
    if (!delivery.present || peer->retiring) return true;
    qa_net_reader reader; qa_net_reader_init(&reader, delivery.payload, error); bool moved = false;
    while (qa_net_reader_remaining(&reader)) {
        qa_q1_client_message message;
        if (!qa_q1_client_read(&reader, (qa_net_protocol_id){.kind = QA_NET_NQ15}, delivery.sequence, &moved, &message)) return false;
        switch (message.op) {
        case QA_Q1_CLC_NOP: break;
        case QA_Q1_CLC_DISCONNECT: return retire(peer, "client disconnected", false, error);
        case QA_Q1_CLC_STRING:
            if (!peer->started) return qa_network_fail(error, "NetQuake command precedes original server signon");
            if (!string_command(peer, message.data.text, error)) return false;
            if (peer->stage == 2) {
                const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
                if (client->phase == QA_NET_CONNECTED && !qa_network_phase(runtime, id, QA_NET_PRIMED, error)) return false;
            }
            break;
        case QA_Q1_CLC_MOVE:
            if (peer->stage != 4) break;
            if (peer->input_sequence == UINT64_MAX) return qa_network_fail(error, "NetQuake source input sequence exhausted");
            if (!peer->hooks.input(peer->hooks.context, id, &message.data.nq_move, peer->input_sequence + 1, error)) return false;
            ++peer->input_sequence; break;
        default: return qa_network_fail(error, "Unsupported original NetQuake client service");
        }
    }
    return !reader.failed;
}
static bool flush(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    uint64_t now, qa_error *error)
{
    nq_server *peer = context; (void)runtime; (void)id;
    if (peer->retiring) return true;
    if (peer->first && qa_nq_channel_ready(peer->native.channel.nq)) {
        nq_pending *pending = peer->first;
        if (!qa_nq_channel_queue(peer->native.channel.nq, (qa_bytes){pending->bytes.data, pending->bytes.size}, error)) return false;
        peer->first = pending->next; if (!peer->first) peer->last = NULL;
        peer->queued_bytes -= pending->bytes.size; --peer->queued_messages;
        qa_buffer_free(&pending->bytes); free(pending);
    }
    bool present; qa_bytes packet;
    return qa_nq_channel_next(peer->native.channel.nq, now, &present, &packet, error) &&
        (!present || qa_q1_peer_send(&peer->native, packet, error));
}
static bool command(void *context, const qa_network_command *value, qa_error *error)
{ (void)context; (void)value; return qa_network_fail(error, "NetQuake server cannot submit local client movement"); }
static bool restart(void *context, uint64_t epoch, const qa_sha256_digest *composition, qa_error *error)
{
    nq_server *peer = context; (void)epoch; (void)composition;
    if (peer->retiring) return qa_network_fail(error, "Cannot restart a retiring NetQuake source peer");
    queue_clear(peer); peer->started = false; peer->stage = 0;
    return signon(peer, 1, error);
}
static bool rebind(void *context, const qa_net_address *address, qa_error *error)
{ (void)context; (void)address; return qa_network_fail(error, "NetQuake endpoint changes require a new original connection admission"); }
static void close_peer(void *context)
{
    nq_server *peer = context; if (!peer) return;
    queue_clear(peer); qa_nq_channel_destroy(peer->native.channel.nq); free(peer);
}
static const qa_network_peer_ops ops = {receive, flush, command, restart, rebind, close_peer};
static nq_server *get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (peer->ops.receive != receive) { qa_network_fail(error, "Connection is not an original NetQuake server peer"); return NULL; }
    return peer->state;
}
bool qa_network_attach_nq_server(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_nq_server_policy *policy, const qa_network_nq_server_hooks *hooks,
    uint64_t now, qa_net_client_id *out, qa_error *error)
{
    if (!runtime || !request || !policy || !hooks || !out || request->protocol.kind != QA_NET_NQ15 ||
        request->protocol.revision || request->protocol.flags || request->seat_count != 1 ||
        !hooks->signon || !hooks->begin || !hooks->command || !hooks->input || !hooks->drop ||
        !policy->message_bytes || policy->message_bytes > 65527 || !policy->fragment_bytes ||
        policy->fragment_bytes > policy->message_bytes || policy->queued_bytes < policy->message_bytes ||
        policy->message_bytes + 8 > qa_net_transport_limit(runtime->transport))
        return qa_network_fail(error, "NetQuake server requires its complete original single-seat source and transport policy");
    nq_server *peer = calloc(1, sizeof(*peer));
    if (!peer) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating original NetQuake runtime peer"); return false; }
    peer->runtime = runtime; peer->policy = *policy; peer->hooks = *hooks;
    peer->native.transport = runtime->transport; peer->native.remote = request->endpoint; peer->native.kind = QA_Q1_PEER_NETQUAKE;
    if (!qa_nq_channel_create(policy->message_bytes, policy->fragment_bytes, &peer->native.channel.nq, error)) { free(peer); return false; }
    if (!qa_network_attach(runtime, request, &ops, peer, now, &peer->id, error)) { close_peer(peer); return false; }
    *out = peer->id; return true;
}
bool qa_network_nq_server_start(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    if (!peer || peer->retiring || peer->started) return qa_network_fail(error, "NetQuake source signon is already started or retiring");
    return signon(peer, 1, error);
}
bool qa_network_nq_server_reliable(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    return peer && !peer->retiring && queue(peer, bytes, error);
}
bool qa_network_nq_server_frame(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    if (!peer) return false;
    if (peer->retiring || peer->stage != 4) return true;
    qa_bytes packet;
    return qa_nq_channel_unreliable(peer->native.channel.nq, bytes, &packet, error) &&
        qa_q1_peer_send(&peer->native, packet, error);
}
bool qa_network_nq_server_drop(qa_network_runtime *runtime, qa_net_client_id id, const char *reason, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    return peer && retire(peer, reason ? reason : "server disconnected", true, error);
}
bool qa_network_nq_server_state_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_nq_server_state *out, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    if (!peer || !out) return qa_network_fail(error, "Missing original NetQuake peer state output");
    *out = (qa_network_nq_server_state){peer->input_sequence, peer->queued_bytes, peer->queued_messages,
        peer->stage, peer->started, peer->retiring}; return true;
}
