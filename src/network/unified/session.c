#include "session_internal.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_events.h"
#include "channel_internal.h"
#include "value_internal.h"

#include <stdlib.h>
#include <string.h>

bool qa_unified_session_fail(qa_error *e, qa_status status, const char *message)
{
    qa_error_set(e, status, 0, "%s", message); return false;
}

const qa_unified_limits *qa_unified_session_limits(const qa_unified_session *s)
{ return s?&s->limits:NULL; }

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
bool qa_unified_session_retiring(const qa_unified_session *s)
{
    return qa_unified_session_idle(s) && (s->timeout_pending || s->closing || s->disconnected);
}
bool qa_unified_session_active(const qa_unified_session *s)
{
    if (!qa_unified_session_idle(s) || !s->bound_source || !s->admitted ||
        s->timeout_pending || s->closing || s->disconnected) return false;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(s->runtime), s->id);
    return client && client->phase == QA_NET_ACTIVE;
}
uint32_t qa_unified_session_epoch(const qa_unified_session *s) { return s ? s->epoch : 0; }
int64_t qa_unified_session_acknowledged(const qa_unified_session *s) { return s ? s->acknowledged : -1; }

static void frame_release(qa_unified_session *s, qa_unified_frame_receipt *frame)
{
    s->frame_bytes -= frame->bytes;
    qa_unified_document_destroy(frame->document);
    *frame = (qa_unified_frame_receipt){0};
}

void qa_unified_session_frames_clear(qa_unified_session *s)
{
    for (size_t i = 0; i < QA_UNIFIED_FRAME_BACKUP; ++i) frame_release(s, s->frames + i);
    s->frame_bytes = 0;
}

void qa_unified_session_frame_forget(qa_unified_session *s, uint32_t sequence)
{
    qa_unified_frame_receipt *frame = s->frames + sequence % QA_UNIFIED_FRAME_BACKUP;
    if (sequence && frame->sequence == sequence) frame_release(s, frame);
}

const qa_unified_document *qa_unified_session_frame_find(const qa_unified_session *s, uint32_t sequence)
{
    const qa_unified_frame_receipt *frame = s->frames + sequence % QA_UNIFIED_FRAME_BACKUP;
    return sequence && frame->sequence == sequence ? frame->document : NULL;
}

bool qa_unified_session_frame_retain(qa_unified_session *s, uint32_t sequence,
    const qa_unified_document *document, qa_error *e)
{
    qa_unified_document *retained = NULL;
    if (!sequence || !document || qa_unified_document_type(document) != QA_UNIFIED_FRAME_DOCUMENT)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Retaining a Unified baseline requires its actual frame receipt");
    size_t bytes = qa_unified_document_memory(document);
    if (!qa_unified_document_retain(document, &retained, e)) return false;
    qa_unified_frame_receipt *slot = s->frames + sequence % QA_UNIFIED_FRAME_BACKUP;
    frame_release(s, slot);
    while (bytes > QA_UNIFIED_FRAME_HISTORY_BYTES - s->frame_bytes) {
        qa_unified_frame_receipt *oldest = NULL;
        for (size_t i = 0; i < QA_UNIFIED_FRAME_BACKUP; ++i)
            if (s->frames[i].document && (!oldest || s->frames[i].sequence < oldest->sequence)) oldest = s->frames + i;
        if (!oldest) { qa_unified_document_destroy(retained); return true; }
        frame_release(s, oldest);
    }
    *slot = (qa_unified_frame_receipt){retained, bytes, sequence}; s->frame_bytes += bytes;
    return true;
}

bool qa_unified_session_frame_decode(const qa_unified_session *s, qa_bytes bytes,
    qa_unified_document **out, bool *missing, qa_error *e)
{
    uint32_t sequence;
    *missing = false;
    if (!qa_unified_frame_baseline(bytes, &sequence, e)) return false;
    const qa_unified_document *baseline = qa_unified_session_frame_find(s, sequence);
    if (sequence && !baseline) { *missing = true; return true; }
    return qa_unified_frame_decode(bytes, baseline, sequence, s->frame_pool, s->strings, out, e);
}

void qa_unified_session_release(qa_unified_session *s)
{
    if (!s) return;
    while (s->held) {
        qa_unified_held *next = s->held->next;
        qa_unified_session_delivery_free(s->held); s->held = next;
    }
    qa_unified_session_delivery_free(s->timeout_delivery);
    qa_unified_session_frames_clear(s);
    qa_unified_inputs_free(&s->inputs);
    qa_unified_channel_destroy(s->channel);
    free(s->frame_wire.data);
    qa_unified_frame_pool_destroy(&s->frame_pool);
    qa_strings_destroy(s->strings); free(s);
}

static void close_peer(void *state)
{
    qa_unified_session *s = state;
    if (!s || s->entered || s->processing) return;
    if (s->bound_source && s->hooks.closed) s->hooks.closed(s->hooks.context, s->id);
    qa_unified_session_release(s);
}

static bool admit_delivery(void *context, const qa_unified_delivery *delivery)
{
    const qa_unified_session *s = context;
    if (s->held_count >= (size_t)s->limits.reliable_window_messages + 1 ||
        delivery->payload.size > s->limits.queued_reliable_bytes + s->limits.message_bytes - s->held_bytes)
        return false;
    if (delivery->kind == QA_UNIFIED_FRAME)
        for (const qa_unified_held *held = s->held; held; held = held->next)
            if (held->kind != QA_UNIFIED_CONTROL_DOCUMENT) return false;
    return true;
}

static bool hold_delivery(void *context, const qa_unified_delivery *delivery, bool *accepted, qa_error *e)
{
    qa_unified_session *s = context;
    *accepted=false;
    if (!admit_delivery(s, delivery))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production document holding capacity exceeded");
    qa_unified_document_kind kind = delivery->kind == QA_UNIFIED_RELIABLE ? QA_UNIFIED_CONTROL_DOCUMENT :
        s->server ? QA_UNIFIED_INPUT_DOCUMENT : QA_UNIFIED_FRAME_DOCUMENT;
    qa_unified_document *document=NULL;
    qa_unified_held *held=NULL;
    if (!s->server) {
        bool missing=false;
        bool ready=true,okay;
        if (kind==QA_UNIFIED_CONTROL_DOCUMENT && delivery->payload.size>=4 &&
            !memcmp(delivery->payload.data,"QUEV",4) && s->hooks.events_decode)
            okay=s->hooks.events_decode(s->hooks.context,delivery->payload,&held,&ready,e);
        else okay=kind==QA_UNIFIED_FRAME_DOCUMENT ? qa_unified_session_frame_decode(s,
            delivery->payload,&document,&missing,e) : qa_unified_document_decode(kind,delivery->payload, s->strings, NULL,&document,e);
        if (held) document=held->document;
        if (okay && !ready) return true;
        if (!okay || missing) {
            if (missing) {
                *accepted=true;
                s->frame_applied=delivery->sequence;
                s->channel->frame_ack_pending=s->channel->frame_admitted!=0;
            }
            qa_unified_document_destroy(document); return okay;
        }
    }
    const qa_unified_frame *frame=qa_unified_document_frame(document);
    qa_unified_frame_lease *lease=frame?frame->lease:NULL;
    if (lease && !qa_unified_frame_lease_retain(lease,e)) { qa_unified_document_destroy(document); return false; }
    if (!held) held=lease ? qa_unified_frame_lease_alloc(lease,1,sizeof(*held),_Alignof(qa_unified_held),e) : calloc(1,sizeof(*held));
    if (!held) { qa_unified_document_destroy(document); qa_unified_frame_lease_release(lease);
        return qa_unified_session_fail(e,QA_ERROR_MEMORY,"Retaining production document delivery"); }
    held->lease=lease; held->document=document;
    held->kind = kind;
    if (!held->event_lease) {
        held->wire.data=lease ? qa_unified_frame_lease_alloc(lease,delivery->payload.size?delivery->payload.size:1,1,1,e) :
            malloc(delivery->payload.size?delivery->payload.size:1);
        if (!held->wire.data) { qa_unified_session_delivery_free(held); return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining complete production delivery bytes"); }
        held->wire.size = delivery->payload.size;
        if (held->wire.size) memcpy(held->wire.data, delivery->payload.data, held->wire.size);
    }
    held->bytes = delivery->payload.size; held->sequence = delivery->sequence; held->required = delivery->required_reliable;
    if (s->tail) s->tail->next = held; else s->held = held;
    s->tail = held; s->held_bytes += held->bytes; ++s->held_count;
    *accepted=true;
    return true;
}

bool qa_unified_session_receive_resume(qa_unified_session *s, qa_error *e)
{
    return qa_unified_channel_resume(s->channel, admit_delivery, hold_delivery, s, e);
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
    bool ok = qa_unified_channel_receive_buffered(s->channel, packet->payload, packet->received_ns,
        admit_delivery, hold_delivery, s, e);
    if (ok) ok = qa_network_received(runtime, id, packet->received_ns, e);
    s->entered = false;
    return ok;
}

static qa_net_send_result send_peer(void *state, qa_bytes bytes, qa_error *e)
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
    bool ok = true;
    if (!s->server && s->admitted && client && client->phase == QA_NET_ACTIVE &&
        !s->disconnected && !s->closing && !s->timeout_pending) {
        qa_error deferred = {0};
        ok = qa_unified_session_queue_inputs(s, &deferred);
        if (!ok && deferred.code == QA_ERROR_MEMORY) ok = true;
        else if (!ok && e) *e = deferred;
    }
    if (ok) ok = qa_unified_channel_flush(s->channel, now, send_peer, s, NULL, e);
    s->entered = false;
    return ok;
}

bool qa_unified_session_flush(qa_unified_session *s, uint64_t now, qa_error *e)
{
    if (!qa_unified_session_idle(s) || now < s->now_ns)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production flush requires its idle monotonic owner");
    return flush_peer(s, s->runtime, s->id, now, e);
}

static bool restart_peer(void *state, uint64_t epoch, const uint64_t *composition, qa_error *e)
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
    if (s->entered || s->processing || s->closing || s->disconnected || s->timeout_pending ||
        !s->hooks.restart || !epoch || s->epoch == UINT32_MAX)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart lacks its real server offer producer");
    for (const qa_unified_held *held = s->held; held; held = held->next)
        if (held->source_finished)
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart retains an unfinished reply batch from its prior epoch");
    s->entered = true;
    qa_unified_document *offer = NULL;
    uint32_t next_wire_epoch = s->epoch + 1;
    bool ok = s->hooks.restart(s->hooks.context, s->runtime, s->id, next_wire_epoch, composition, &offer, e);
    if (ok && (!offer || !qa_unified_session_kind(offer, "offer")))
        ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart did not produce an actual offer");
    uint32_t offered = 0;
    if (ok) ok = qa_unified_document_epoch(offer, &offered, e);
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
    s->strings=hooks->strings; qa_strings_retain(s->strings);
    s->seat = request->seats[0].seat; s->acknowledged = -1; s->now_ns = now;
    s->limits = limits ? *limits : qa_unified_limits_default();
    if (s->limits.queued_reliable_bytes > SIZE_MAX - s->limits.message_bytes) {
        qa_strings_destroy(s->strings); free(s); return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production holding capacity exceeds storage");
    }
    s->frame_pool=qa_unified_frame_pool_create(0,0,e);
    if (!s->frame_pool || !qa_unified_channel_create(token, &s->limits, &s->channel, e)) {
        qa_unified_session_release(s); return false;
    }
    const qa_network_peer_ops ops = qa_unified_session_operations();
    if (!qa_network_attach(runtime, request, &ops, s, now, &s->id, e)) { qa_unified_session_release(s); return false; }
    s->bound_source = true;
    *id = s->id; *out = s;
    return true;
}
