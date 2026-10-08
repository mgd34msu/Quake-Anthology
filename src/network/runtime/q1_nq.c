#include "internal.h"
#include "qa/network_q1_runtime.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q1_peer_save.h"
#include "qa/network_save.h"
#include "q1_retirement.h"
#include <stdlib.h>
#include <string.h>

typedef struct nq_pending {
    struct nq_pending *next;
    qa_buffer bytes;
    uint64_t serial;
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
    uint64_t reliable_queued, reliable_inflight, reliable_acknowledged;
    uint8_t stage;
    bool started, retiring, signon_active;
    q1_retirement retirement;
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
    pending->serial = ++peer->reliable_queued;
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
    q1_retirement *r = &peer->retirement;
    if (r->busy) return qa_network_fail(error, "Recursive NetQuake source retirement");
    if (!peer->retiring) {
        if (!q1_retirement_start(r, reason, notify, 9, error)) return false;
        peer->retiring = true;
    }
    if (r->marked) return true;
    r->busy = true;
    bool previous = peer->runtime->callback; peer->runtime->callback = true;
    bool ok = true;
    if (!r->sent) {
        if (!r->packet.size) {
            uint8_t disconnect = QA_NQ_DISCONNECT; qa_bytes packet;
            ok = qa_nq_channel_unreliable(peer->native.channel.nq, (qa_bytes){&disconnect, 1}, &packet, error);
            if (ok) {
                memcpy(r->packet.data, packet.data, packet.size); r->packet.size = packet.size;
                r->packet_disconnect = true;
            }
        }
        if (ok) ok = qa_q1_peer_send(&peer->native, (qa_bytes){r->packet.data, r->packet.size}, error);
        if (ok) r->sent = true;
    }
    if (ok) {
        ok = peer->hooks.drop(peer->hooks.context, peer->id, r->reason, error);
        if (ok) r->marked = true;
    }
    peer->runtime->callback = previous;
    r->busy = false; return ok;
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
    bool pending = !qa_nq_channel_ready(peer->native.channel.nq);
    bool received = qa_q1_peer_receive(&peer->native, &packet->from, packet->payload, packet->received_ns, &delivery, error);
    if (received && pending && qa_nq_channel_ready(peer->native.channel.nq)) {
        peer->reliable_acknowledged = peer->reliable_inflight;
        peer->reliable_inflight = 0;
    }
    if (!received || !qa_network_received(runtime, id, packet->received_ns, error)) return false;
    if (!delivery.present || peer->retiring) return true;
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    qa_net_reader reader; qa_net_reader_init(&reader, delivery.payload, error); bool moved = false;
    while (qa_net_reader_remaining(&reader)) {
        qa_q1_client_message message;
        if (!qa_q1_client_read(&reader, client->protocol, delivery.sequence, &moved, &message)) return false;
        switch (message.op) {
        case QA_Q1_CLC_NOP: break;
        case QA_Q1_CLC_DISCONNECT: return retire(peer, "client disconnected", false, error);
        case QA_Q1_CLC_STRING:
            if (!peer->started) return qa_network_fail(error, "NetQuake command precedes original server signon");
            if (!string_command(peer, message.data.text, error)) return false;
            if (peer->stage == 2) {
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
    if (peer->retiring) return retire(peer, peer->retirement.reason, peer->retirement.notify, error);
    if (peer->first && qa_nq_channel_ready(peer->native.channel.nq)) {
        nq_pending *pending = peer->first;
        if (!qa_nq_channel_queue(peer->native.channel.nq, (qa_bytes){pending->bytes.data, pending->bytes.size}, error)) return false;
        peer->reliable_inflight = pending->serial;
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
static bool restart(void *context, uint64_t epoch, const uint64_t *composition, qa_error *error)
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
    queue_clear(peer); q1_retirement_clear(&peer->retirement);
    qa_nq_channel_destroy(peer->native.channel.nq); free(peer);
}
static const qa_network_peer_ops ops = {.receive=receive,.flush=flush,.command=command,
    .restart=restart,.rebind=rebind,.close=close_peer};
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
    if (!runtime || !request || !policy || !hooks || !out || request->protocol.kind > QA_NET_RMQ999 ||
        !qa_q1_profile_valid(request->protocol, error) || request->seat_count != 1 ||
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
    *out = (qa_network_nq_server_state){.input_sequence=peer->input_sequence,
        .queued_bytes=peer->queued_bytes, .queued_messages=peer->queued_messages,
        .stage=peer->stage, .started=peer->started, .retiring=peer->retiring,
        .reliable_queued=peer->reliable_queued, .reliable_inflight=peer->reliable_inflight,
        .reliable_acknowledged=peer->reliable_acknowledged}; return true;
}

bool qa_network_nq_peer(const qa_network_peer *peer)
{ return peer && peer->ops.receive == receive; }
bool qa_network_nq_retirement_pending(const qa_network_peer *peer)
{
    if (!qa_network_nq_peer(peer)) return false;
    const nq_server *source = peer->state;
    return source->retiring && !source->retirement.marked;
}
bool qa_network_nq_timeout(qa_network_peer *owner,const char *reason,qa_error *error)
{
    if (!qa_network_nq_peer(owner) || !reason)
        return qa_network_fail(error,"NetQuake timeout requires its actual source peer and reason");
    nq_server *peer=owner->state;
    if (!peer->runtime->pumping || !peer->runtime->callback || peer->signon_active)
        return qa_network_fail(error,"NetQuake timeout requires its genuine returned pump callback");
    return retire(peer,reason,true,error);
}
const qa_q1_peer *qa_network_nq_server_view(qa_network_runtime *runtime, qa_net_client_id id)
{ nq_server *peer = get(runtime, id, NULL); return peer ? &peer->native : NULL; }
bool qa_network_nq_server_policy_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_nq_server_policy *out, qa_error *error)
{
    nq_server *peer = get(runtime, id, error);
    if (!peer || !out) return qa_network_fail(error, "Missing original NetQuake policy output");
    *out = peer->policy; return true;
}
void qa_network_nq_transport_rebind(qa_network_peer *peer, qa_net_transport *transport)
{
    if (qa_network_nq_peer(peer)) ((nq_server *)peer->state)->native.transport = transport;
}
static bool continuation_valid(const nq_server *peer, const qa_net_client *client, qa_error *error)
{
    if (!q1_retirement_valid(&peer->retirement, peer->retiring, 9, error)) return false;
    if (peer->retirement.packet.size && (peer->retirement.packet.size != 9 ||
        !peer->retirement.packet_disconnect || peer->retirement.packet.data[8] != QA_NQ_DISCONNECT ||
        peer->retirement.packet.data[0] != 0 || peer->retirement.packet.data[1] != 16 ||
        peer->retirement.packet.data[2] != 0 || peer->retirement.packet.data[3] != 9))
        return qa_network_fail(error, "Invalid retained NetQuake disconnect packet");
    if (!client || client->protocol.kind > QA_NET_RMQ999 || !qa_q1_profile_valid(client->protocol, error) ||
        client->seat_count != 1 || peer->signon_active || peer->stage > 4 ||
        peer->started != (peer->stage != 0) ||
        (!peer->retiring && client->phase != (peer->stage < 2 ? QA_NET_CONNECTED :
            peer->stage < 4 ? QA_NET_PRIMED : QA_NET_ACTIVE)) ||
        !peer->policy.message_bytes || peer->policy.message_bytes > 65527 ||
        !peer->policy.fragment_bytes || peer->policy.fragment_bytes > peer->policy.message_bytes ||
        peer->policy.queued_bytes < peer->policy.message_bytes || peer->queued_bytes > peer->policy.queued_bytes)
        return qa_network_fail(error, "NetQuake continuation differs from its original connection phase or source policy");
    return true;
}
static qa_q1_peer_save_admission saved_admission(const nq_server *peer, const qa_net_client *client)
{
    return (qa_q1_peer_save_admission){.protocol = client->protocol, .transport = peer->runtime->transport,
        .remote = client->endpoint, .message_bytes = peer->policy.message_bytes,
        .channel.nq_fragment_bytes = peer->policy.fragment_bytes};
}
bool qa_network_nq_checkpoint_peer(const qa_network_peer *owner, qa_buffer *out, qa_error *error)
{
    if (!qa_network_nq_peer(owner) || !out)
        return qa_network_fail(error, "Missing original NetQuake continuation owner");
    const nq_server *peer = owner->state;
    const qa_net_client *client = qa_net_connections_get(peer->runtime->connections, peer->id);
    if (!continuation_valid(peer, client, error)) return false;
    size_t count = 0, queued = 0, extent = 80; const nq_pending *last = NULL;
    if (!q1_retirement_extent(&peer->retirement, &extent, error)) return false;
    for (const nq_pending *pending = peer->first; pending; pending = pending->next) {
        if (!pending->bytes.size || !pending->bytes.data || pending->bytes.size > peer->policy.message_bytes ||
            pending->bytes.size > peer->policy.queued_bytes - queued || count >= peer->queued_messages ||
            pending->bytes.size > SIZE_MAX - 8 || extent > SIZE_MAX - 8 - pending->bytes.size)
            return qa_network_fail(error, "NetQuake reliable FIFO differs from its retained ownership and bounds");
        queued += pending->bytes.size; extent += 8 + pending->bytes.size; ++count; last = pending;
    }
    if (count != peer->queued_messages || queued != peer->queued_bytes || last != peer->last)
        return qa_network_fail(error, "NetQuake reliable FIFO allocator inventory differs");
    qa_buffer native = {0}; qa_q1_peer_save_admission admission = saved_admission(peer, client);
    if (!qa_q1_peer_checkpoint(&peer->native, &admission, &native, error)) return false;
    if (native.size > SIZE_MAX - extent) {
        qa_buffer_free(&native); return qa_network_fail(error, "NetQuake continuation extent overflow");
    }
    qa_buffer bytes = {malloc(extent + native.size), 0};
    if (!bytes.data) {
        qa_buffer_free(&native); qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining NetQuake runtime continuation"); return false;
    }
    qa_net_writer writer; qa_net_writer_init(&writer, bytes.data, extent + native.size, error);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x534e4151)) &&
        qa_net_write_u64(&writer, peer->policy.message_bytes) && qa_net_write_u64(&writer, peer->policy.fragment_bytes) &&
        qa_net_write_u64(&writer, peer->policy.queued_bytes) && qa_net_write_u64(&writer, peer->input_sequence) &&
        qa_net_write_u8(&writer, peer->stage) && qa_net_write_u8(&writer, peer->started) && qa_net_write_u8(&writer, peer->retiring) &&
        qa_net_write_u64(&writer, peer->queued_bytes) && qa_net_write_u64(&writer, count) &&
        q1_retirement_write(&peer->retirement, &writer);
    for (const nq_pending *pending = peer->first; ok && pending; pending = pending->next)
        ok = qa_net_write_u64(&writer, pending->bytes.size) &&
            qa_net_write_data(&writer, pending->bytes.data, pending->bytes.size);
    if (ok) ok = qa_net_write_u64(&writer, native.size) && qa_net_write_data(&writer, native.data, native.size);
    qa_buffer_free(&native);
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&writer); *out = bytes; return true;
}
bool qa_network_nq_restore_peer(qa_network_runtime *runtime, const qa_net_client *client, qa_bytes bytes,
    const qa_network_checkpoint_refs *refs, qa_network_peer *owner, qa_error *error)
{
    if (!runtime || !client || !refs || !refs->source_nq || !owner || !bytes.data)
        return qa_network_fail(error, "NetQuake restore requires its actual candidate source bindings");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x534e4151))
        return qa_net_reader_fail(&reader, "Invalid NetQuake runtime continuation schema");
    uint64_t message = qa_net_read_u64(&reader), fragment = qa_net_read_u64(&reader), maximum = qa_net_read_u64(&reader);
    nq_server *peer = calloc(1, sizeof(*peer));
    if (!peer) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring NetQuake runtime owner"); return false; }
    peer->runtime = runtime; peer->id = client->id;
    peer->input_sequence = qa_net_read_u64(&reader); peer->stage = qa_net_read_u8(&reader);
    uint8_t started = qa_net_read_u8(&reader), retiring = qa_net_read_u8(&reader);
    uint64_t queued = qa_net_read_u64(&reader), count = qa_net_read_u64(&reader);
    peer->started = started != 0; peer->retiring = retiring != 0;
    bool ok = !reader.failed && started <= 1 && retiring <= 1 && message <= SIZE_MAX && fragment <= SIZE_MAX &&
        maximum <= SIZE_MAX && queued <= maximum && count <= qa_net_reader_remaining(&reader) / 9;
    if (ok) {
        peer->policy = (qa_network_nq_server_policy){(size_t)message, (size_t)fragment, (size_t)maximum};
        ok = q1_retirement_read(&peer->retirement, &reader, peer->retiring, 9, error) &&
            continuation_valid(peer, client, error);
    }
    for (uint64_t i = 0; ok && i < count; ++i) {
        uint64_t length = qa_net_read_u64(&reader); qa_bytes payload;
        ok = !reader.failed && length && length <= SIZE_MAX &&
            qa_net_read_bytes(&reader, (size_t)length, &payload) && queue(peer, payload, error);
    }
    qa_bytes native = {0}; uint64_t length = ok ? qa_net_read_u64(&reader) : 0;
    if (ok) ok = peer->queued_bytes == queued && !reader.failed && length <= SIZE_MAX &&
        qa_net_read_bytes(&reader, (size_t)length, &native) && qa_net_reader_finish(&reader);
    qa_network_nq_server_policy policy = {0};
    if (ok) ok = refs->source_nq(refs->context, client, &policy, &peer->hooks, error) &&
        policy.message_bytes == peer->policy.message_bytes && policy.fragment_bytes == peer->policy.fragment_bytes &&
        policy.queued_bytes == peer->policy.queued_bytes && peer->hooks.signon && peer->hooks.begin &&
        peer->hooks.command && peer->hooks.input && peer->hooks.drop;
    if (ok) {
        qa_q1_peer_save_admission admission = saved_admission(peer, client);
        ok = qa_q1_peer_restore_checkpoint(native, &admission, &peer->native, error);
    }
    if (!ok) {
        close_peer(peer);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Unqualified NetQuake runtime continuation");
        return false;
    }
    peer->reliable_queued = 0;
    if (!qa_nq_channel_ready(peer->native.channel.nq))
        peer->reliable_inflight = ++peer->reliable_queued;
    for (nq_pending *pending = peer->first; pending; pending = pending->next)
        pending->serial = ++peer->reliable_queued;
    owner->ops = ops; owner->state = peer; return true;
}
