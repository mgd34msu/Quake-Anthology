#include "internal.h"
#include "qa/network_qw_runtime.h"
#include "qa/network_q1_peer_save.h"
#include "qa/network_q1_session_save.h"
#include "qa/network_save.h"
#include "q1_retirement.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct qw_pending {
    struct qw_pending *next;
    qa_buffer bytes;
} qw_pending;
typedef struct qw_server {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_q1_peer native;
    qa_network_qw_server_policy policy;
    qa_network_qw_server_hooks hooks;
    qa_qw_signon *signon;
    qa_qw_source_history *frames;
    qa_qw_command last_command;
    qw_pending *first, *last;
    size_t queued_bytes, queued_messages, reliable_bytes;
    uint32_t input_sequence, choked;
    uint8_t delta, loss;
    bool has_delta, reply, retiring, signon_active, primed;
    q1_retirement retirement;
} qw_server;

static const qa_net_protocol_id protocol = {.kind = QA_NET_QW28};
static bool queue(void *context, qa_bytes bytes, qa_error *error)
{
    qw_server *peer = context;
    if ((bytes.size && !bytes.data) || bytes.size > peer->policy.message_bytes)
        return qa_network_fail(error, "QuakeWorld reliable block exceeds its source message extent");
    if (!bytes.size) return true;
    if (peer->last && bytes.size <= peer->policy.message_bytes - peer->last->bytes.size) {
        size_t size = peer->last->bytes.size + bytes.size;
        void *next = realloc(peer->last->bytes.data, size);
        if (!next) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining QuakeWorld reliable block"); return false;
        }
        peer->last->bytes.data = next;
        memcpy(peer->last->bytes.data + peer->last->bytes.size, bytes.data, bytes.size);
        peer->last->bytes.size = size; peer->queued_bytes += bytes.size; return true;
    }
    if (peer->queued_messages >= peer->policy.queued_messages)
        return qa_network_fail(error, "QuakeWorld reliable back buffers overflow");
    qw_pending *pending = calloc(1, sizeof(*pending));
    if (pending) pending->bytes.data = malloc(bytes.size);
    if (!pending || !pending->bytes.data) {
        free(pending); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining QuakeWorld reliable FIFO"); return false;
    }
    memcpy(pending->bytes.data, bytes.data, bytes.size); pending->bytes.size = bytes.size;
    if (peer->last) peer->last->next = pending; else peer->first = pending;
    peer->last = pending; ++peer->queued_messages; peer->queued_bytes += bytes.size; return true;
}
static void queue_clear(qw_server *peer)
{
    while (peer->first) {
        qw_pending *next = peer->first->next;
        qa_buffer_free(&peer->first->bytes); free(peer->first); peer->first = next;
    }
    peer->last = NULL; peer->queued_messages = peer->queued_bytes = 0;
}
static bool paused(qw_server *peer, bool *out, qa_error *error)
{
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    bool ok = peer->hooks.paused(peer->hooks.context, peer->id, out, error);
    peer->runtime->callback = previous; return ok;
}
static bool native_send(qw_server *peer, qa_bytes packet, qa_error *error)
{
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    bool ok = qa_q1_peer_send(&peer->native, packet, error);
    peer->runtime->callback = previous; return ok;
}
static bool retire(qw_server *peer, const char *reason, bool notify, qa_error *error)
{
    q1_retirement *r = &peer->retirement;
    if (r->busy) return qa_network_fail(error, "Recursive QuakeWorld source retirement");
    if (!peer->retiring) {
        if (!q1_retirement_start(r, reason, notify, peer->policy.message_bytes + 8, error)) return false;
        peer->retiring = true;
    }
    if (r->marked) return true;
    r->busy = true;
    bool ok = true;
    while (ok && !r->sent) {
        if (!r->packet.size) {
            uint8_t disconnect = 2; qa_bytes packet;
            ok = qa_qw_channel_transmit(peer->native.channel.qw, (qa_bytes){&disconnect, 1},
                peer->runtime->now_ns, true, &packet, error);
            if (ok) {
                size_t reliable = (qa_load_u32le(packet.data) & UINT32_C(0x80000000)) ? peer->reliable_bytes : 0;
                memcpy(r->packet.data, packet.data, packet.size); r->packet.size = packet.size;
                r->packet_disconnect = packet.size == 8 + reliable + 1;
            }
        }
        if (ok) ok = native_send(peer, (qa_bytes){r->packet.data, r->packet.size}, error);
        if (ok) {
            if (r->packet_disconnect) r->sent = true;
            else r->packet.size = 0;
        }
    }
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    if (ok) {
        ok = peer->hooks.drop(peer->hooks.context, peer->id, r->reason, error);
        if (ok) r->marked = true;
    }
    peer->runtime->callback = previous;
    r->busy = false; return ok;
}
static bool source_command(qw_server *peer, const char *text, qa_error *error)
{
    const char *cursor = text; char name[32]; bool present;
    if (!qa_q1_token(&cursor, true, name, sizeof(name), &present, error)) return false;
    if (!present) return true;
    if (!strcmp(name, "drop") || !strcmp(name, "disconnect"))
        return retire(peer, "client disconnected", true, error);
    if (peer->signon_active) return qa_network_fail(error, "Recursive QuakeWorld signon command");
    peer->signon_active = true;
    bool handled = false;
    bool ok = qa_qw_signon_command(peer->signon, text, queue, peer, &handled, error);
    peer->signon_active = false;
    if (!ok || peer->retiring) return ok;
    const qa_net_client *client = qa_net_connections_get(peer->runtime->connections, peer->id);
    if (!client) return qa_network_fail(error, "QuakeWorld signon lost its source connection");
    if (handled && client->phase == QA_NET_CONNECTED &&
        !qa_network_phase(peer->runtime, peer->id, QA_NET_PRIMED, error)) return false;
    if (handled) peer->primed = true;
    if (qa_qw_signon_spawned(peer->signon) && client->phase != QA_NET_ACTIVE &&
        !qa_network_phase(peer->runtime, peer->id, QA_NET_ACTIVE, error)) return false;
    return handled || peer->hooks.command(peer->hooks.context, peer->id, text, error);
}
typedef struct qw_group {
    qa_qw_command commands[20];
    size_t count;
} qw_group;
static bool retain_command(void *context, const qa_qw_command *command, qa_error *error)
{
    qw_group *group = context;
    if (group->count == 20) return qa_network_fail(error, "QuakeWorld loss recovery exceeded its source cutoff");
    group->commands[group->count++] = *command; return true;
}
static bool receive(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *error)
{
    qw_server *peer = context; qa_q1_delivery delivery;
    if (peer->hooks.blocked(peer->hooks.context, &packet->from)) return true;
    qa_qw_channel_stats before = qa_qw_channel_get_stats(peer->native.channel.qw);
    if (!qa_q1_peer_receive(&peer->native, &packet->from, packet->payload,
        packet->received_ns, &delivery, error)) return false;
    if (!delivery.present || peer->retiring) return true;
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    if (!client || (!qa_net_address_equal(&client->endpoint, &packet->from, true) &&
        !qa_net_connections_rebind(runtime->connections, id, &packet->from, error))) return false;
    if (!qa_network_received(runtime, id, packet->received_ns, error)) return false;
    if (!peer->hooks.receipt(peer->hooks.context, id, delivery.acknowledged,
            before.outgoing_sequence, packet->received_ns, error)) return false;
    peer->reply = delivery.sequence >= before.outgoing_sequence; peer->has_delta = false;
    qa_net_reader reader; qa_net_reader_init(&reader, delivery.payload, error); bool moved = false;
    while (qa_net_reader_remaining(&reader)) {
        qa_q1_client_message message;
        if (!qa_q1_client_read(&reader, protocol, delivery.sequence, &moved, &message)) return false;
        switch (message.op) {
        case QA_Q1_CLC_NOP: break;
        case QA_Q1_CLC_DELTA: peer->delta = message.data.delta; peer->has_delta = true; break;
        case QA_Q1_CLC_STRING:
            if (!peer->hooks.defer_command(peer->hooks.context, id, message.data.text, error)) return false;
            if (peer->retiring) return true;
            break;
        case QA_Q1_CLC_MOVE: {
            if (!qa_qw_signon_spawned(peer->signon)) break;
            bool source_paused; qw_group group = {0}; qa_qw_command recovered = peer->last_command;
            if (!paused(peer, &source_paused, error) ||
                !qa_qw_replay_commands(&recovered, &message.data.qw_move, delivery.dropped,
                    source_paused, retain_command, &group, error)) return false;
            if (group.count && !peer->hooks.input(peer->hooks.context, id, group.commands,
                group.count, delivery.sequence, error)) return false;
            peer->last_command = recovered; peer->loss = message.data.qw_move.loss;
            if (group.count) peer->input_sequence = delivery.sequence;
            break;
        }
        /* These source opcodes have no application action in the behavioral
         * reference's native QW packet consumer. Their payload is consumed. */
        case QA_Q1_CLC_TELEPORT: case QA_Q1_CLC_UPLOAD: break;
        default: return qa_network_fail(error, "Unsupported QuakeWorld source client service");
        }
    }
    return !reader.failed;
}
static bool send(qw_server *peer, qa_bytes services, const qa_qw_source_frame *frame, qa_error *error)
{
    if (!peer->reply || peer->retiring) return true;
    bool source_paused;
    if (!paused(peer, &source_paused, error)) return false;
    peer->reply = false;
    if (!source_paused && !qa_qw_channel_can_send(peer->native.channel.qw, peer->runtime->now_ns)) {
        if (peer->choked == UINT32_MAX) return qa_network_fail(error, "QuakeWorld choke counter exhausted");
        ++peer->choked; return true;
    }
    if (peer->first && !qa_qw_channel_pending(peer->native.channel.qw)) {
        qw_pending *pending = peer->first;
        if (!qa_qw_channel_queue(peer->native.channel.qw,
            (qa_bytes){pending->bytes.data, pending->bytes.size}, error)) return false;
        peer->reliable_bytes = pending->bytes.size;
        peer->first = pending->next; if (!peer->first) peer->last = NULL;
        peer->queued_bytes -= pending->bytes.size; --peer->queued_messages;
        qa_buffer_free(&pending->bytes); free(pending);
    }
    uint8_t *storage = malloc(peer->policy.message_bytes);
    if (!storage) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding QuakeWorld source frame"); return false; }
    qa_error encoding_error = {0};
    qa_net_writer writer; qa_net_writer_init(&writer, storage, peer->policy.message_bytes, &encoding_error);
    qa_qw_source_frame sent = {0}; bool encoded = false;
    uint32_t sequence = qa_qw_channel_get_stats(peer->native.channel.qw).outgoing_sequence;
    if (frame) {
        sent = *frame; sent.sequence = sequence;
        if (peer->choked) {
            qa_qw_service choke = {.kind = QA_QW_CHOKE_COUNT, .data.byte = (uint8_t)peer->choked};
            if (!qa_qw_service_write(&writer, protocol, &choke, NULL)) goto datagram_failed;
            peer->choked = 0;
        }
        if (!qa_net_write_data(&writer, services.data, services.size)) goto datagram_failed;
        const qa_qw_source_frame *previous = NULL;
        if (peer->has_delta) for (uint32_t age = 1; age < QA_QW_UPDATE_BACKUP && age <= sequence; ++age) {
            uint32_t candidate = sequence - age;
            if ((uint8_t)candidate != peer->delta) continue;
            previous = qa_qw_source_history_frame(peer->frames, candidate); break;
        }
        if (!qa_qw_source_write_entities(&writer, peer->frames, &sent, previous)) goto datagram_failed;
        encoded = true;
    }
    goto transmit;
datagram_failed: {
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    peer->hooks.print(peer->hooks.context, encoding_error.message);
    peer->runtime->callback = previous;
    qa_net_writer_init(&writer, storage, peer->policy.message_bytes, &encoding_error);
}
transmit:;
    qa_bytes packet;
    if (!qa_qw_channel_transmit(peer->native.channel.qw,
        (qa_bytes){storage, qa_net_writer_size(&writer)}, peer->runtime->now_ns, source_paused, &packet, error) ||
        !native_send(peer, packet, error)) goto failure;
    size_t reliable = (qa_load_u32le(packet.data) & UINT32_C(0x80000000)) ? peer->reliable_bytes : 0;
    if (encoded && packet.size == 8 + reliable + qa_net_writer_size(&writer) &&
        !qa_qw_source_history_store(peer->frames, &sent, error)) goto failure;
    free(storage); return true;
failure:
    free(storage); return false;
}
static bool flush(void *context, qa_network_runtime *runtime, qa_net_client_id id,
    uint64_t now_ns, qa_error *error)
{
    qw_server *peer = context; (void)runtime; (void)id; (void)now_ns;
    if (peer->retiring) return retire(peer, peer->retirement.reason, peer->retirement.notify, error);
    return qa_qw_signon_spawned(peer->signon) || send(peer, (qa_bytes){0}, NULL, error);
}
static bool command(void *context, const qa_network_command *value, qa_error *error)
{ (void)context; (void)value; return qa_network_fail(error, "QuakeWorld server cannot submit local client movement"); }
static bool restart(void *context, uint64_t epoch, const qa_sha256_digest *composition, qa_error *error)
{
    qw_server *peer = context; (void)epoch; (void)composition;
    if (peer->retiring || peer->signon_active) return qa_network_fail(error, "QuakeWorld source peer is retiring or executing signon");
    qa_qw_signon *signon = NULL;
    if (!qa_qw_signon_create(&peer->hooks.signon, false, &signon, error)) return false;
    qa_qw_source_history_reset(peer->frames);
    qa_qw_signon_destroy(peer->signon); peer->signon = signon;
    queue_clear(peer); peer->last_command = (qa_qw_command){0};
    peer->input_sequence = peer->choked = 0; peer->has_delta = false;
    peer->primed = false;
    uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_qw_service changing = {.kind = QA_QW_STUFFTEXT, .data.text.value = "changing\nreconnect\n"};
    return qa_qw_service_write(&writer, protocol, &changing, NULL) &&
        queue(peer, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
static bool rebind(void *context, const qa_net_address *address, qa_error *error)
{
    qw_server *peer = context;
    if (!qa_net_address_equal(&peer->native.remote, address, false))
        return qa_network_fail(error, "QuakeWorld endpoint rebinding changes its admitted host address");
    peer->native.remote = *address; return true;
}
static void close_peer(void *context)
{
    qw_server *peer = context; if (!peer) return;
    queue_clear(peer); q1_retirement_clear(&peer->retirement); qa_qw_signon_destroy(peer->signon);
    qa_qw_source_history_destroy(peer->frames); qa_qw_channel_destroy(peer->native.channel.qw); free(peer);
}
static const qa_network_peer_ops ops = {.receive=receive,.flush=flush,.command=command,
    .restart=restart,.rebind=rebind,.close=close_peer};
static qw_server *get(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (peer->ops.receive != receive) { qa_network_fail(error, "Connection is not an original QuakeWorld server peer"); return NULL; }
    return peer->state;
}
static bool hooks_valid(const qa_network_qw_server_hooks *hooks)
{
    return hooks && hooks->print && hooks->paused && hooks->input && hooks->command &&
        hooks->defer_command && hooks->receipt && hooks->blocked && hooks->drop;
}
bool qa_network_attach_qw_server(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_qw_server_policy *policy, const qa_network_qw_server_hooks *hooks,
    uint64_t now_ns, qa_net_client_id *out, qa_error *error)
{
    if (!runtime || !request || !policy || !out || request->protocol.kind != QA_NET_QW28 ||
        request->protocol.flags || request->protocol.revision || request->seat_count != 1 ||
        !hooks_valid(hooks) || !runtime->options.hooks.commands || policy->message_bytes != 1450 ||
        policy->queued_messages != 5 || policy->bytes_per_second < 500 || policy->bytes_per_second > 10000 ||
        policy->message_bytes + 10 > qa_net_transport_limit(runtime->transport))
        return qa_network_fail(error, "QuakeWorld server requires its actual source hooks and native single-seat policy");
    qw_server *peer = calloc(1, sizeof(*peer));
    if (!peer) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating QuakeWorld source peer"); return false; }
    peer->runtime = runtime; peer->policy = *policy; peer->hooks = *hooks;
    peer->native = (qa_q1_peer){.transport = runtime->transport, .remote = request->endpoint, .kind = QA_Q1_PEER_QUAKEWORLD};
    if (!qa_qw_channel_create(QA_Q1_CHANNEL_SERVER, policy->qport, policy->message_bytes,
        policy->bytes_per_second, &peer->native.channel.qw, error) ||
        !qa_qw_signon_create(&hooks->signon, false, &peer->signon, error) ||
        !(peer->frames = qa_qw_source_history_create(error)) ||
        !qa_network_attach(runtime, request, &ops, peer, now_ns, &peer->id, error)) {
        close_peer(peer); return false;
    }
    *out = peer->id; return true;
}
bool qa_network_qw_server_reliable(qa_network_runtime *runtime, qa_net_client_id id, qa_bytes bytes, qa_error *error)
{
    qw_server *peer = get(runtime, id, error); return peer && !peer->retiring && queue(peer, bytes, error);
}
bool qa_network_qw_server_command(qa_network_runtime *runtime, qa_net_client_id id,
    const char *text, qa_error *error)
{
    if (!runtime || !text || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "QuakeWorld source actions require returned receive callbacks");
    qw_server *peer = get(runtime, id, error);
    return peer && !peer->retiring && source_command(peer, text, error);
}
bool qa_network_qw_server_baselines(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_qw_source_entity *entities, size_t count, qa_error *error)
{
    qw_server *peer = get(runtime, id, error);
    if (!peer || (count && !entities) || peer->retiring) return qa_network_fail(error, "Missing QuakeWorld baseline owner");
    return qa_qw_source_history_baselines(peer->frames, entities, count, error);
}
bool qa_network_qw_server_frame(qa_network_runtime *runtime, qa_net_client_id id,
    qa_bytes services, const qa_qw_source_frame *frame, qa_error *error)
{
    qw_server *peer = get(runtime, id, error);
    if (!peer || !frame || (services.size && !services.data) || !qa_qw_signon_spawned(peer->signon))
        return qa_network_fail(error, "QuakeWorld frame requires its actual begun source client");
    return send(peer, services, frame, error);
}
bool qa_network_qw_server_drop(qa_network_runtime *runtime, qa_net_client_id id, const char *reason, qa_error *error)
{
    qw_server *peer = get(runtime, id, error); return peer && retire(peer, reason ? reason : "disconnected", true, error);
}
bool qa_network_qw_server_state_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_qw_server_state *out, qa_error *error)
{
    qw_server *peer = get(runtime, id, error);
    if (!peer || !out) return qa_network_fail(error, "Missing QuakeWorld source peer observation");
    *out = (qa_network_qw_server_state){.input_sequence = peer->input_sequence,
        .outgoing_sequence = qa_qw_channel_get_stats(peer->native.channel.qw).outgoing_sequence,
        .choked = peer->choked, .loss = peer->loss, .qport = peer->policy.qport,
        .queued_messages = peer->queued_messages, .queued_bytes = peer->queued_bytes,
        .active = qa_qw_signon_spawned(peer->signon), .retiring = peer->retiring, .reply = peer->reply};
    return true;
}
bool qa_network_qw_server_rate(qa_network_runtime *runtime, qa_net_client_id id, uint32_t rate, qa_error *error)
{
    qw_server *peer = get(runtime, id, error);
    if (!peer || rate < 500 || rate > 10000) return qa_network_fail(error, "QuakeWorld source rate exceeds original bounds");
    if (!qa_qw_channel_rate(peer->native.channel.qw, rate, error)) return false;
    peer->policy.bytes_per_second = rate; return true;
}
const qa_q1_peer *qa_network_qw_server_view(qa_network_runtime *runtime, qa_net_client_id id)
{
    qw_server *peer = get(runtime, id, NULL); return peer ? &peer->native : NULL;
}
bool qa_network_qw_peer(const qa_network_peer *peer)
{ return peer && peer->occupied && peer->ops.receive == receive; }
bool qa_network_qw_retirement_pending(const qa_network_peer *peer)
{
    if (!qa_network_qw_peer(peer)) return false;
    const qw_server *source = peer->state;
    return source->retiring && !source->retirement.marked;
}
bool qa_network_qw_peer_matches(const qa_network_peer *owner, const qa_net_datagram *packet)
{
    if (!qa_network_qw_peer(owner) || !packet || !packet->payload.data || packet->payload.size < 10) return false;
    const qw_server *peer = owner->state;
    return qa_net_address_equal(&peer->native.remote, &packet->from, false) &&
        qa_load_u16le(packet->payload.data + 8) == peer->policy.qport;
}
void qa_network_qw_transport_rebind(qa_network_peer *owner, qa_net_transport *transport)
{ if (qa_network_qw_peer(owner)) ((qw_server *)owner->state)->native.transport = transport; }
static bool continuation_valid(const qw_server *peer, const qa_net_client *client, qa_error *error)
{
    if (!q1_retirement_valid(&peer->retirement, peer->retiring, peer->policy.message_bytes + 8, error)) return false;
    qa_qw_channel_stats channel = qa_qw_channel_get_stats(peer->native.channel.qw);
    if (peer->retirement.packet.size) {
        const q1_retirement *r = &peer->retirement;
        if (r->packet.size < 8) return qa_network_fail(error, "Truncated retained QuakeWorld disconnect packet");
        uint32_t sequence = qa_load_u32le(r->packet.data);
        size_t reliable = (sequence & UINT32_C(0x80000000)) ? peer->reliable_bytes : 0;
        if ((sequence & INT32_MAX) >= channel.outgoing_sequence ||
            (r->packet_disconnect ? (r->packet.size != 8 + reliable + 1 || r->packet.data[r->packet.size - 1] != 2) :
                (reliable != peer->policy.message_bytes || r->packet.size != 8 + reliable)))
            return qa_network_fail(error, "Invalid retained QuakeWorld disconnect packet");
    }
    if (!client || client->protocol.kind != QA_NET_QW28 || client->protocol.flags || client->protocol.revision ||
        client->seat_count != 1 || peer->signon_active || peer->policy.message_bytes != 1450 ||
        peer->policy.queued_messages != 5 || peer->policy.bytes_per_second < 500 || peer->policy.bytes_per_second > 10000 ||
        channel.bytes_per_second != peer->policy.bytes_per_second ||
        peer->queued_messages > peer->policy.queued_messages || peer->input_sequence > INT32_MAX ||
        peer->reliable_bytes > peer->policy.message_bytes ||
        !isfinite(peer->last_command.angles[0]) || !isfinite(peer->last_command.angles[1]) ||
        !isfinite(peer->last_command.angles[2]) || peer->last_command.buttons ||
        (qa_qw_signon_spawned(peer->signon) && !peer->primed) ||
        peer->input_sequence > channel.incoming_sequence ||
        (!peer->retiring && client->phase != (qa_qw_signon_spawned(peer->signon) ? QA_NET_ACTIVE :
            peer->primed ? QA_NET_PRIMED : QA_NET_CONNECTED)))
        return qa_network_fail(error, "QuakeWorld continuation differs from its source phase or owned policy");
    return qa_qw_source_history_cut(peer->frames, channel.outgoing_sequence, error);
}
static qa_q1_peer_save_admission saved_admission(const qw_server *peer, const qa_net_client *client)
{
    return (qa_q1_peer_save_admission){.protocol = protocol, .transport = peer->runtime->transport,
        .remote = client->endpoint, .message_bytes = peer->policy.message_bytes,
        .channel.qw = {QA_Q1_CHANNEL_SERVER, peer->policy.qport}};
}
static bool write_blob(qa_net_writer *writer, const qa_buffer *bytes)
{ return qa_net_write_u64(writer, bytes->size) && qa_net_write_data(writer, bytes->data, bytes->size); }
static bool read_blob(qa_net_reader *reader, qa_bytes *bytes)
{
    uint64_t size = qa_net_read_u64(reader);
    return !reader->failed && size <= SIZE_MAX && qa_net_read_bytes(reader, (size_t)size, bytes);
}
bool qa_network_qw_checkpoint_peer(const qa_network_peer *owner, qa_buffer *out, qa_error *error)
{
    if (!qa_network_qw_peer(owner) || !out) return qa_network_fail(error, "Missing QuakeWorld continuation owner");
    const qw_server *peer = owner->state;
    const qa_net_client *client = qa_net_connections_get(peer->runtime->connections, peer->id);
    if (!continuation_valid(peer, client, error)) return false;
    size_t count = 0, queued = 0, capacity = 128; const qw_pending *last = NULL;
    if (!q1_retirement_extent(&peer->retirement, &capacity, error)) return false;
    for (const qw_pending *pending = peer->first; pending; pending = pending->next) {
        if (!pending->bytes.data || !pending->bytes.size || pending->bytes.size > peer->policy.message_bytes ||
            count >= peer->policy.queued_messages || capacity > SIZE_MAX - 8 - pending->bytes.size)
            return qa_network_fail(error, "QuakeWorld retained reliable FIFO inventory differs");
        ++count; queued += pending->bytes.size; capacity += 8 + pending->bytes.size; last = pending;
    }
    if (count != peer->queued_messages || queued != peer->queued_bytes || last != peer->last)
        return qa_network_fail(error, "QuakeWorld reliable FIFO allocator state differs");
    qa_buffer native = {0}, signon = {0}, frames = {0};
    qa_q1_peer_save_admission admission = saved_admission(peer, client);
    bool ok = qa_q1_peer_checkpoint(&peer->native, &admission, &native, error) &&
        qa_qw_signon_checkpoint(peer->signon, &signon, error) &&
        qa_qw_source_history_checkpoint(peer->frames,
            qa_qw_channel_get_stats(peer->native.channel.qw).outgoing_sequence, &frames, error);
    const qa_buffer *blobs[] = {&native, &signon, &frames};
    for (size_t i = 0; ok && i < 3; ++i) {
        if (blobs[i]->size > SIZE_MAX - capacity) ok = qa_network_fail(error, "QuakeWorld continuation extent overflows");
        else capacity += blobs[i]->size;
    }
    qa_buffer bytes = {0}; qa_net_writer writer;
    if (ok) {
        bytes.data = malloc(capacity);
        if (!bytes.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding QuakeWorld source continuation"); ok = false; }
        else qa_net_writer_init(&writer, bytes.data, capacity, error);
    }
    if (ok) ok = qa_net_write_u32(&writer, UINT32_C(0x53574151)) && qa_net_write_u32(&writer, 3) &&
        qa_net_write_u16(&writer, peer->policy.qport) && qa_net_write_u32(&writer, peer->policy.bytes_per_second) &&
        qa_net_write_u64(&writer, peer->policy.message_bytes) && qa_net_write_u64(&writer, peer->policy.queued_messages) &&
        qa_net_write_u32(&writer, peer->input_sequence) && qa_net_write_u32(&writer, peer->choked) &&
        qa_net_write_u8(&writer, peer->delta) && qa_net_write_u8(&writer, peer->loss) &&
        qa_net_write_u8(&writer, peer->has_delta) && qa_net_write_u8(&writer, peer->reply) &&
        qa_net_write_u8(&writer, peer->retiring) && qa_net_write_u8(&writer, peer->primed) &&
        qa_net_write_u64(&writer, peer->reliable_bytes);
    for (size_t i = 0; ok && i < 3; ++i) ok = qa_net_write_f32(&writer, peer->last_command.angles[i]);
    if (ok) ok = qa_net_write_i16(&writer, peer->last_command.forward) &&
        qa_net_write_i16(&writer, peer->last_command.side) && qa_net_write_i16(&writer, peer->last_command.up) &&
        qa_net_write_u8(&writer, peer->last_command.msec) && qa_net_write_u8(&writer, peer->last_command.impulse) &&
        qa_net_write_u64(&writer, peer->queued_bytes) && qa_net_write_u64(&writer, peer->queued_messages) &&
        q1_retirement_write(&peer->retirement, &writer);
    for (const qw_pending *pending = peer->first; ok && pending; pending = pending->next) ok = write_blob(&writer, &pending->bytes);
    for (size_t i = 0; ok && i < 3; ++i) ok = write_blob(&writer, blobs[i]);
    if (ok) { bytes.size = qa_net_writer_size(&writer); *out = bytes; }
    else qa_buffer_free(&bytes);
    qa_buffer_free(&native); qa_buffer_free(&signon); qa_buffer_free(&frames); return ok;
}
bool qa_network_qw_restore_peer(qa_network_runtime *runtime, const qa_net_client *client, qa_bytes bytes,
    const qa_network_checkpoint_refs *refs, qa_network_peer *owner, qa_error *error)
{
    if (!runtime || !client || !refs || !refs->source_qw || !owner || !bytes.data || !runtime->options.hooks.commands)
        return qa_network_fail(error, "QuakeWorld restore lacks its actual candidate source consumers");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x53574151) || qa_net_read_u32(&reader) != 3)
        return qa_net_reader_fail(&reader, "Invalid QuakeWorld source continuation schema");
    qw_server *peer = calloc(1, sizeof(*peer));
    if (!peer) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring QuakeWorld source peer"); return false; }
    peer->runtime = runtime; peer->id = client->id;
    peer->policy.qport = qa_net_read_u16(&reader); peer->policy.bytes_per_second = qa_net_read_u32(&reader);
    uint64_t message = qa_net_read_u64(&reader), maximum = qa_net_read_u64(&reader);
    peer->input_sequence = qa_net_read_u32(&reader); peer->choked = qa_net_read_u32(&reader);
    peer->delta = qa_net_read_u8(&reader); peer->loss = qa_net_read_u8(&reader);
    uint8_t has_delta = qa_net_read_u8(&reader), reply = qa_net_read_u8(&reader),
        retiring = qa_net_read_u8(&reader), primed = qa_net_read_u8(&reader);
    uint64_t reliable = qa_net_read_u64(&reader);
    for (size_t i = 0; i < 3; ++i) peer->last_command.angles[i] = qa_net_read_f32(&reader);
    peer->last_command.forward = qa_net_read_i16(&reader); peer->last_command.side = qa_net_read_i16(&reader);
    peer->last_command.up = qa_net_read_i16(&reader); peer->last_command.msec = qa_net_read_u8(&reader);
    peer->last_command.impulse = qa_net_read_u8(&reader);
    uint64_t queued = qa_net_read_u64(&reader), count = qa_net_read_u64(&reader);
    bool ok = !reader.failed && message == 1450 && maximum == 5 && reliable <= message &&
        count <= maximum && queued <= count * message && has_delta <= 1 && reply <= 1 && retiring <= 1 && primed <= 1;
    peer->policy.message_bytes = 1450; peer->policy.queued_messages = 5;
    peer->has_delta = has_delta != 0; peer->reply = reply != 0; peer->retiring = retiring != 0;
    peer->primed = primed != 0; peer->reliable_bytes = (size_t)reliable;
    if (ok) ok = q1_retirement_read(&peer->retirement, &reader, peer->retiring, 1458, error);
    for (uint64_t i = 0; ok && i < count; ++i) {
        qa_bytes payload = {0}; ok = read_blob(&reader, &payload) && payload.size && payload.size <= 1450;
        if (ok) {
            qw_pending *pending = calloc(1, sizeof(*pending));
            if (pending) pending->bytes.data = malloc(payload.size);
            if (!pending || !pending->bytes.data) {
                free(pending); qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring QuakeWorld reliable FIFO"); ok = false;
            } else {
                memcpy(pending->bytes.data, payload.data, payload.size); pending->bytes.size = payload.size;
                if (peer->last) peer->last->next = pending; else peer->first = pending;
                peer->last = pending; ++peer->queued_messages; peer->queued_bytes += payload.size;
            }
        }
    }
    qa_bytes native = {0}, signon = {0}, frames = {0};
    if (ok) ok = peer->queued_bytes == queued && read_blob(&reader, &native) &&
        read_blob(&reader, &signon) && read_blob(&reader, &frames) && qa_net_reader_finish(&reader);
    qa_network_qw_server_policy policy = {0}; qa_qw_download_admission downloads = {0};
    if (ok) ok = refs->source_qw(refs->context, client, &policy, &peer->hooks, &downloads, error) &&
        policy.qport == peer->policy.qport && policy.message_bytes == peer->policy.message_bytes &&
        policy.queued_messages == peer->policy.queued_messages &&
        policy.bytes_per_second == peer->policy.bytes_per_second && hooks_valid(&peer->hooks);
    if (ok) {
        qa_q1_peer_save_admission admission = saved_admission(peer, client);
        ok = qa_q1_peer_restore_checkpoint(native, &admission, &peer->native, error) &&
            qa_qw_signon_restore_checkpoint(signon, &peer->hooks.signon, false, &downloads, &peer->signon, error) &&
            qa_qw_source_history_restore(frames,
                qa_qw_channel_get_stats(peer->native.channel.qw).outgoing_sequence,
                &peer->frames, error) && continuation_valid(peer, client, error);
    }
    if (!ok) {
        close_peer(peer);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Unqualified QuakeWorld source continuation");
        return false;
    }
    owner->ops = ops; owner->state = peer; return true;
}
