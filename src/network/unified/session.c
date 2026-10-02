#include "session_internal.h"

#include <stdlib.h>
#include <string.h>

bool qa_unified_session_fail(qa_error *e, qa_status status, const char *message)
{
    qa_error_set(e, status, 0, "%s", message); return false;
}

bool qa_unified_session_idle(const qa_unified_session *s)
{
    return s && !s->entered && !s->processing && qa_network_callbacks_idle(s->runtime);
}
bool qa_unified_session_disconnected(const qa_unified_session *s)
{
    return !s || s->disconnected || (s->closing &&
        (qa_unified_channel_acknowledged(s->channel) >= s->required ||
         (s->now_ns >= s->closing_ns && s->now_ns - s->closing_ns > UINT64_C(1000000000))));
}
uint32_t qa_unified_session_epoch(const qa_unified_session *s) { return s ? s->epoch : 0; }
int64_t qa_unified_session_acknowledged(const qa_unified_session *s) { return s ? s->acknowledged : -1; }

void qa_unified_session_release(qa_unified_session *s)
{
    if (!s) return;
    while (s->held) {
        qa_unified_held *next = s->held->next;
        qa_unified_session_delivery_free(s->held); s->held = next;
    }
    qa_unified_session_delivery_free(s->timeout_delivery);
    qa_unified_inputs_free(&s->inputs);
    qa_unified_channel_destroy(s->channel);
    free(s);
}

static void close_peer(void *state)
{
    qa_unified_session *s = state;
    if (!s || s->entered || s->processing) return;
    if (s->bound_source && s->hooks.closed) s->hooks.closed(s->hooks.context, s->id);
    qa_unified_session_release(s);
}

static bool hold_delivery(void *context, const qa_unified_delivery *delivery, qa_error *e)
{
    qa_unified_session *s = context;
    if (s->held_count >= (size_t)s->limits.reliable_window_messages + 1 ||
        delivery->payload.size > s->limits.queued_reliable_bytes + s->limits.message_bytes - s->held_bytes)
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production document holding capacity exceeded");
    qa_unified_held *held = calloc(1, sizeof(*held));
    if (!held) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining production document delivery");
    qa_unified_document_kind kind = delivery->kind == QA_UNIFIED_RELIABLE ? QA_UNIFIED_CONTROL_DOCUMENT :
        s->server ? QA_UNIFIED_INPUT_DOCUMENT : QA_UNIFIED_FRAME_DOCUMENT;
    held->kind = kind;
    held->wire.data = malloc(delivery->payload.size ? delivery->payload.size : 1);
    if (!held->wire.data) { free(held); return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining complete production delivery bytes"); }
    held->wire.size = delivery->payload.size;
    if (held->wire.size) memcpy(held->wire.data, delivery->payload.data, held->wire.size);
    if (!s->server && !qa_unified_document_decode(kind,
        (qa_bytes){held->wire.data, held->wire.size}, &held->document, e)) {
        qa_buffer_free(&held->wire); free(held); return false;
    }
    held->bytes = delivery->payload.size; held->sequence = delivery->sequence; held->required = delivery->required_reliable;
    if (s->tail) s->tail->next = held; else s->held = held;
    s->tail = held; s->held_bytes += held->bytes; ++s->held_count;
    return true;
}

static bool receive_peer(void *state, qa_network_runtime *runtime, qa_net_client_id id,
    const qa_net_datagram *packet, qa_error *e)
{
    qa_unified_session *s = state;
    if (!s->bound_source || s->entered || s->processing || runtime != s->runtime || !qa_net_client_id_equal(id, s->id))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Recursive or mismatched production session receive");
    qa_unified_packet decoded;
    if (!qa_unified_packet_decode(packet->payload, &decoded, NULL)) return true;
    unsigned mismatch = 0;
    for (size_t i = 0; i < 16; ++i) mismatch |= (unsigned)(decoded.token.bytes[i] ^ s->token.bytes[i]);
    if (mismatch || packet->payload.size > s->limits.datagram_bytes ||
        (decoded.kind != QA_UNIFIED_ACK && (decoded.total_bytes > s->limits.message_bytes || decoded.fragments > s->limits.fragments))) return true;
    s->entered = true; s->now_ns = packet->received_ns;
    bool ok = qa_unified_channel_receive(s->channel, packet->payload, packet->received_ns, hold_delivery, s, e);
    if (ok) ok = qa_network_received(runtime, id, packet->received_ns, e);
    s->entered = false;
    return ok;
}

static bool send_peer(void *state, qa_bytes bytes, qa_error *e)
{
    qa_unified_session *s = state;
    return qa_network_send(s->runtime, s->id, bytes, e);
}

static bool flush_peer(void *state, qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *e)
{
    qa_unified_session *s = state;
    if (!s->bound_source || s->entered || s->processing || runtime != s->runtime || !qa_net_client_id_equal(id, s->id))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Recursive or mismatched production session flush");
    s->entered = true; s->now_ns = now;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(runtime), id);
    uint64_t timeout = s->server ? UINT64_C(30000000000) : UINT64_C(120000000000);
    if (!s->closing && !s->disconnected && !s->timeout_pending && client && now >= client->received_ns && now - client->received_ns > timeout) {
        s->timeout_pending = true; s->close_cause = 1; s->entered = false; return true;
    }
    bool ok = s->server || !s->admitted || !client || client->phase != QA_NET_ACTIVE ||
        s->disconnected || s->closing || qa_unified_session_queue_inputs(s, e);
    if (ok) ok = qa_unified_channel_flush(s->channel, now, send_peer, s, NULL, e);
    s->entered = false;
    return ok;
}

bool qa_unified_session_flush(qa_unified_session *s, uint64_t now, qa_error *e)
{
    if (!qa_unified_session_idle(s) || now < s->now_ns)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production flush requires its idle monotonic owner");
    if (s->held || s->timeout_pending) return true;
    return flush_peer(s, s->runtime, s->id, now, e);
}

static bool restart_peer(void *state, uint64_t epoch, const qa_sha256_digest *composition, qa_error *e)
{
    qa_unified_session *s = state;
    if (!s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart requires actual published Source callback ownership");
    if (!s->server) {
        if (s->entered || !epoch || qa_unified_channel_closed(s->channel))
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production client restart lost its retained channel");
        s->admitted = false; s->acknowledged = -1;
        qa_unified_inputs_free(&s->inputs);
        return true;
    }
    if (s->entered || s->processing || !s->hooks.restart || !epoch || s->epoch == UINT32_MAX)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart lacks its real server offer producer");
    s->entered = true;
    qa_unified_document *offer = NULL;
    uint32_t next_wire_epoch = s->epoch + 1;
    bool ok = s->hooks.restart(s->hooks.context, s->runtime, s->id, next_wire_epoch, composition, &offer, e);
    if (ok && (!offer || !qa_unified_session_kind(offer, "offer")))
        ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart did not produce an actual offer");
    uint32_t offered = 0;
    if (ok) ok = qa_unified_session_document_epoch(offer, &offered, e);
    if (ok && offered != next_wire_epoch)
        ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart offer changed its requested epoch");
    if (ok) ok = qa_unified_session_queue_control(s, offer, e);
    qa_unified_document_destroy(offer);
    s->entered = false;
    return ok;
}

static bool rebind_peer(void *state, const qa_net_address *endpoint, qa_error *e)
{
    qa_unified_session *s = state;
    if (s->entered || s->processing || !endpoint)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production endpoint rebind is not idle");
    /* The sole generic connection table authenticates and owns this endpoint. */
    return true;
}
static bool receive_pending(const void *state)
{
    const qa_unified_session *s = state;
    return s->held != NULL || s->timeout_pending;
}

qa_network_peer_ops qa_unified_session_operations(void)
{
    return (qa_network_peer_ops){receive_peer, flush_peer, qa_unified_session_command,
        restart_peer, rebind_peer, close_peer, receive_pending};
}

bool qa_unified_session_attach(qa_network_runtime *runtime, const qa_net_connect *request, bool server,
    qa_unified_token token, const qa_unified_limits *limits, const qa_unified_session_hooks *hooks,
    uint64_t now, qa_net_client_id *id, qa_unified_session **out, qa_error *e)
{
    if (!runtime || !request || !hooks || !id || !out || request->protocol.kind != QA_NET_UNIFIED_1 ||
        request->protocol.flags || request->protocol.revision || request->seat_count != 1 || !request->seats ||
        !hooks->player || !hooks->control || (server ? (!hooks->input || !hooks->restart) : (!hooks->prepare || !hooks->frame)))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production session lacks its actual one-seat source contracts");
    qa_unified_session *s = calloc(1, sizeof(*s));
    if (!s) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Allocating production session");
    s->runtime = runtime; s->server = server; s->hooks = *hooks; s->token = token;
    s->seat = request->seats[0].seat; s->acknowledged = -1; s->now_ns = now;
    s->limits = limits ? *limits : qa_unified_limits_default();
    if (s->limits.queued_reliable_bytes > SIZE_MAX - s->limits.message_bytes) {
        free(s); return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production holding capacity exceeds storage");
    }
    if (!qa_unified_channel_create(token, &s->limits, &s->channel, e)) { free(s); return false; }
    const qa_network_peer_ops ops = qa_unified_session_operations();
    if (!qa_network_attach(runtime, request, &ops, s, now, &s->id, e)) { qa_unified_session_release(s); return false; }
    s->bound_source = true;
    *id = s->id; *out = s;
    return true;
}
