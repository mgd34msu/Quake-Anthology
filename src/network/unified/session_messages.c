#include "session_internal.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_metadata.h"
#include "qa/network_unified_control.h"
#include "channel_internal.h"
#include "value_internal.h"

#include <stdlib.h>
#include <string.h>

qa_json_id qa_unified_session_value(const qa_unified_document *d)
{
    const qa_json_document *json = qa_unified_document_json(d);
    return qa_unified_document_type(d) == QA_UNIFIED_CONTROL_DOCUMENT ?
        qa_json_get(json, qa_unified_document_root(d), "value") : qa_unified_document_root(d);
}

bool qa_unified_session_kind(const qa_unified_document *d, const char *kind)
{
    static const char *const names[] = {"ready", "admitted", "resources", "userinfo", "command",
        "source-command", "disconnect", "offer", "components", "component-command", "events", "metadata"};
    qa_unified_control_kind actual=qa_unified_document_control_type(d);
    return actual<QA_UNIFIED_CONTROL_INVALID && !strcmp(kind,names[actual]);
}


static bool server_control(const qa_unified_document *d)
{
    return qa_unified_session_kind(d, "offer") || qa_unified_session_kind(d, "admitted") ||
        qa_unified_session_kind(d, "resources") || qa_unified_session_kind(d, "components") ||
        qa_unified_session_kind(d, "events") || qa_unified_session_kind(d, "metadata");
}

static bool client_control(const qa_unified_document *d)
{
    return qa_unified_session_kind(d, "ready") || qa_unified_session_kind(d, "userinfo") ||
        qa_unified_session_kind(d, "command") || qa_unified_session_kind(d, "component-command") ||
        qa_unified_session_kind(d, "source-command");
}

bool qa_unified_session_reply_valid(const qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!d || qa_unified_document_type(d) != QA_UNIFIED_CONTROL_DOCUMENT || qa_unified_session_kind(d, "offer") ||
        (!qa_unified_session_kind(d, "disconnect") && !(s->server ? server_control(d) : client_control(d))))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its authenticated control direction");
    if (qa_unified_session_kind(d, "disconnect")) return true;
    uint32_t epoch;
    if (!qa_unified_document_epoch(d, &epoch, e)) return false;
    return epoch == s->epoch || qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its retained control epoch");
}

static bool outgoing_control(const qa_unified_session *s, const qa_unified_document *d,
    uint32_t *epoch, bool *offer, bool *disconnect, qa_error *e)
{
    if (!s || !d || qa_unified_document_type(d) != QA_UNIFIED_CONTROL_DOCUMENT || s->disconnected || s->closing ||
        (!qa_unified_session_kind(d, "disconnect") && !(s->server ? server_control(d) : client_control(d))))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control changes its authenticated direction");
    *offer = qa_unified_session_kind(d, "offer"); *disconnect = qa_unified_session_kind(d, "disconnect");
    *epoch = s->epoch;
    if (!*disconnect && !qa_unified_document_epoch(d, epoch, e)) return false;
    if (!*disconnect && (*offer ? *epoch <= s->epoch : *epoch != s->epoch))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control changes its retained world epoch");
    return true;
}

static bool control_admission(const qa_unified_session *s, const qa_unified_document *d,
    bool *ready, qa_error *e)
{
    bool offer, disconnect; uint32_t epoch;
    if (!ready) return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Missing production control admission output");
    if (!outgoing_control(s, d, &epoch, &offer, &disconnect, e)) return false;
    qa_error deferred = {0}; qa_unified_builder wire=s->frame_wire;
    bool okay = qa_unified_document_write(d,s->limits.message_bytes,&wire,&deferred);
    qa_bytes payload = {wire.data,wire.size};
    if (okay) okay = qa_unified_channel_reliable_ready(s->channel, &payload, 1, ready, &deferred);
    if (!okay && deferred.code == QA_ERROR_MEMORY) { *ready = false; return true; }
    if (!okay && e) *e = deferred;
    return okay;
}

bool qa_unified_session_control_ready(const qa_unified_session *s, const qa_unified_document *d,
    bool *ready, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control admission requires its idle Source owner");
    return control_admission(s, d, ready, e);
}

bool qa_unified_session_queue_control(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    bool offer, disconnect; uint32_t epoch;
    if (!outgoing_control(s, d, &epoch, &offer, &disconnect, e)) return false;
    if(!qa_unified_document_write(d,s->limits.message_bytes,&s->frame_wire,e)) return false;
    uint32_t sequence;
    bool ok=qa_unified_channel_reliable(s->channel,
        (qa_bytes){s->frame_wire.data,s->frame_wire.size},&sequence,e);
    if (!ok) return false;
    s->required = sequence;
    if (offer) {
        s->epoch = epoch; s->admitted = false; s->acknowledged = -1;
        qa_unified_session_frames_clear(s);
        qa_unified_inputs_reset(&s->inputs);
    }
    if (disconnect) { s->closing = true; s->admitted = false; s->closing_ns = s->now_ns; }
    return true;
}

bool qa_unified_session_control(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control producer is not idle");
    return qa_unified_session_queue_control(s, d, e);
}

bool qa_unified_session_control_value(qa_unified_session *s, const qa_unified_control *value, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control producer is not idle");
    qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(s->frame_pool,e);
    qa_unified_document *document=NULL;
    bool okay=lease && qa_unified_document_create_control(value,lease,&document,e) &&
        qa_unified_session_queue_control(s,document,e);
    qa_unified_document_destroy(document);
    qa_unified_frame_lease_release(lease);
    return okay;
}

bool qa_unified_session_frame(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source || !s->server || !s->admitted || s->disconnected || s->closing ||
        !d || qa_unified_document_type(d) != QA_UNIFIED_FRAME_DOCUMENT)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame lacks an admitted server producer");
    uint32_t epoch;
    if (!qa_unified_document_epoch(d, &epoch, e)) return false;
    if (epoch != s->epoch) return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame belongs to another epoch");
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame) return false;
    int64_t acknowledged=frame->acknowledged_input;
    if (acknowledged < s->acknowledged)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame acknowledgement regressed");
    uint32_t baseline_sequence = s->channel->frame_acknowledged;
    const qa_unified_document *baseline = qa_unified_session_frame_find(s, baseline_sequence);
    if (!baseline) baseline_sequence = 0;
    if (!qa_unified_frame_write(d,baseline,baseline_sequence,s->limits.message_bytes,&s->frame_wire,e)) return false;
    uint32_t sequence = (uint32_t)s->channel->next_frame;
    uint32_t discarded = s->channel->pending_frame ? s->channel->pending_frame->sequence : 0;
    bool ok = qa_unified_channel_frame(s->channel, (qa_bytes){s->frame_wire.data,s->frame_wire.size}, s->required, e);
    if (ok) {
        qa_unified_session_frame_forget(s, discarded);
        s->acknowledged = (int64_t)acknowledged;
        ok = qa_unified_session_frame_retain(s, sequence, d, e);
    }
    return ok;
}

static bool phase(qa_unified_session *s, qa_net_phase wanted, qa_error *e)
{
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(s->runtime), s->id);
    if (!client) return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production session lost its admitted connection");
    if (client->phase == wanted) return true;
    if (client->phase == QA_NET_CONNECTED && wanted == QA_NET_ACTIVE &&
        !qa_network_phase(s->runtime, s->id, QA_NET_PRIMED, e)) return false;
    return qa_network_phase(s->runtime, s->id, wanted, e);
}

static bool commit_queue(qa_unified_session *s, qa_unified_held *held, bool *waiting, qa_error *e)
{
    const qa_unified_session_commit *commit = &held->commit;
    if (held->responses_queued) return true;
    if (commit->followup_count > 8)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source control followups exceed their actual return extent");
    qa_bytes *payloads=held->response_payloads;
    size_t count = commit->followup_count + (commit->reply != NULL);
    bool disconnect = false, ok = true;
    qa_error deferred = {0};
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_unified_document *d = commit->reply && !i ? commit->reply :
            commit->followups[i - (commit->reply != NULL)];
        bool next_disconnect = qa_unified_session_kind(d, "disconnect");
        if (disconnect || s->closing || s->disconnected) {
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its authenticated control direction"); break;
        }
        ok = qa_unified_session_reply_valid(s, d, &deferred);
        if(ok && !payloads[i].data) {
            ok=qa_unified_document_write(d,s->limits.message_bytes,&s->frame_wire,&deferred);
            if(ok && !held->lease) held->lease=qa_unified_frame_lease_acquire(s->frame_pool,&deferred);
            if(ok) ok=held->lease!=NULL;
            uint8_t *data=ok?qa_unified_frame_lease_alloc(held->lease,s->frame_wire.size,1,1,&deferred):NULL;
            if(ok) ok=data!=NULL;
            if(ok) { memcpy(data,s->frame_wire.data,s->frame_wire.size); payloads[i]=(qa_bytes){data,s->frame_wire.size}; }
        }
        disconnect = next_disconnect;
    }
    bool ready = false;
    if (ok) ok = qa_unified_channel_reliable_ready(s->channel, payloads, count, &ready, &deferred);
    if (ok && ready) ok = qa_unified_channel_reliable_batch(s->channel, payloads, count,
        &held->response_first, &held->response_last, &deferred);
    if (!ok && deferred.code == QA_ERROR_MEMORY) { *waiting = true; return true; }
    if (!ok) { if (e && deferred.code != QA_OK) *e = deferred; return false; }
    if (!ready) { *waiting = true; return true; }
    if (count) s->required = held->response_last;
    if (disconnect) { s->closing = true; s->admitted = false; s->closing_ns = s->now_ns; }
    held->responses_queued = true;
    return true;
}

static void commit_free(qa_unified_session_commit *commit)
{
    qa_unified_document_destroy(commit->reply);
    for (size_t i = 0; i < commit->followup_count && i < 8; ++i) qa_unified_document_destroy(commit->followups[i]);
}

bool qa_unified_session_control_delivery(qa_unified_session *s,const qa_unified_control *value,
    qa_unified_held **out,qa_error *e)
{
    qa_unified_held *held=qa_unified_session_delivery_create(s,NULL,e);
    if (!held) return false;
    held->kind=QA_UNIFIED_CONTROL_DOCUMENT;
    bool okay=qa_unified_document_create_control(value,held->lease,&held->document,e) &&
        qa_unified_document_write(held->document,s->limits.message_bytes,&s->frame_wire,e);
    if (okay) {
        held->wire.data=qa_unified_frame_lease_alloc(held->lease,s->frame_wire.size,1,1,e);
        okay=held->wire.data!=NULL;
        if (okay) { memcpy(held->wire.data,s->frame_wire.data,s->frame_wire.size);held->wire.size=held->bytes=s->frame_wire.size; }
    }
    if (!okay) { qa_unified_session_delivery_free(held);return false; }
    *out=held;return true;
}
void qa_unified_session_delivery_free(qa_unified_held *held)
{
    if (!held) return;
    qa_unified_frame_lease *lease=held->lease;
    qa_event_lease *event_lease=held->event_lease;
    commit_free(&held->commit);
    qa_unified_document_destroy(held->document);
    if(!event_lease && !lease) { qa_buffer_free(&held->wire); free(held); }
    if(event_lease) qa_event_lease_release(event_lease);
    if(lease) qa_unified_frame_lease_release(lease);
}

static bool process_control(qa_unified_session *s, qa_unified_held *held, bool *waiting, qa_error *e)
{
    const qa_unified_document *d = held->document;
    bool disconnect = qa_unified_session_kind(d, "disconnect"), offer = qa_unified_session_kind(d, "offer");
    if (!disconnect && s->server && !client_control(d))
        return qa_unified_session_close(s, "Client sent a server-only control message", e);
    if (!disconnect && !s->server && !server_control(d))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Received production control changes its authenticated direction");
    uint32_t epoch = s->epoch;
    if (!disconnect && !qa_unified_document_epoch(d, &epoch, e)) return false;
    bool ok = true;
    if (!held->source_finished) {
        if (!disconnect && (offer ? epoch <= s->epoch : epoch != s->epoch)) return true;
        if (s->server && s->admitted && qa_unified_session_kind(d, "ready")) return true;
        qa_unified_session_commit commit = {.acknowledged_input = -1};
        ok = s->hooks.control(s->hooks.context, s->runtime, s->id, epoch, d, &commit, e);
        if (ok && offer && !commit.applied)
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Prepared production offer was not published by its Source owner");
        if (ok && commit.followup_count > 8)
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source control followups exceed their actual return extent");
        if (ok && commit.applied && qa_unified_session_kind(d, "ready") &&
            (!commit.reply || !qa_unified_session_kind(commit.reply, "admitted")))
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source admission did not produce its real admitted control");
        if (!ok) { commit_free(&commit); return false; }
        held->commit = commit; held->source_finished = true;
        if (commit.applied && offer) {
            s->epoch = epoch; s->admitted = false; s->acknowledged = -1;
            qa_unified_session_frames_clear(s);
            qa_unified_inputs_reset(&s->inputs);
        }
        if (commit.applied && disconnect) s->admitted = false;
    }
    ok = commit_queue(s, held, waiting, e);
    if (!ok || *waiting) return ok;
    if (ok && !s->closing && held->commit.applied && qa_unified_session_kind(d, "ready")) {
        ok = phase(s, QA_NET_ACTIVE, e);
        if (ok) s->admitted = true;
    }
    if (ok && held->commit.applied && qa_unified_session_kind(d, "admitted")) {
        ok = phase(s, QA_NET_PRIMED, e);
        if (ok) s->admitted = true;
    }
    if (ok && disconnect) {
        if (s->server && !s->closing) {
            bool ready = false;
            ok = control_admission(s, d, &ready, e);
            if (!ok) return false;
            if (!ready) { *waiting = true; return true; }
            qa_error deferred = {0};
            ok = qa_unified_session_queue_control(s, d, &deferred);
            if (!ok && deferred.code == QA_ERROR_MEMORY) { *waiting = true; return true; }
            if (!ok && e) *e = deferred;
        }
        else if (!s->server) s->disconnected = true;
    }
    return ok;
}

static bool process_frame(qa_unified_session *s, qa_unified_held *held, bool *waiting, qa_error *e)
{
    const qa_unified_document *d = held->document;
    uint32_t epoch;
    if (!qa_unified_document_epoch(d, &epoch, e)) return false;
    if (!held->source_finished && (epoch != s->epoch || !s->admitted)) return true;
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame) return false;
    int64_t wire_ack=frame->acknowledged_input;
    if (!held->source_finished && wire_ack < s->acknowledged) return true;
    if (!held->source_finished) {
        qa_unified_session_commit commit = {.acknowledged_input = -1};
        bool ok = s->hooks.frame(s->hooks.context, s->runtime, s->id, d, &commit, e);
        if (ok && commit.applied && commit.acknowledged_input != (int64_t)wire_ack)
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source frame publication changed its actual input acknowledgement");
        if (ok && commit.followup_count > 8)
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source frame followups exceed their actual return extent");
        if (!ok) { commit_free(&commit); return false; }
        held->commit = commit; held->source_finished = true;
    }
    bool ok = commit_queue(s, held, waiting, e);
    if (!ok || *waiting) return ok;
    if (ok && !s->closing && held->commit.applied) ok = phase(s, QA_NET_ACTIVE, e);
    if (ok && held->commit.applied) {
        ok = qa_unified_session_frame_retain(s, held->sequence, d, e) &&
            qa_unified_channel_frame_applied(s->channel, held->sequence, e);
        if (ok) qa_unified_session_ack(s, held->commit.acknowledged_input);
    }
    return ok;
}

static bool process_input(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    const qa_unified_input_batch *batch = qa_unified_document_inputs(d);
    qa_error rejected = {0};
    if (!batch) return qa_unified_session_close(s, "Invalid typed Unified input", e);
    bool ok = batch->epoch != s->epoch || !s->admitted ||
        s->hooks.input(s->hooks.context, s->runtime, s->id, batch, &rejected);
    if (!ok && rejected.code != QA_ERROR_MEMORY)
        ok = qa_unified_session_close(s, rejected.message[0] ? rejected.message : "Invalid Unified input", e);
    else if (!ok && e) *e = rejected;
    return ok;
}

static void delivery_applied(qa_unified_session *s)
{
    qa_unified_held *held = s->held;
    if (held->kind == QA_UNIFIED_CONTROL_DOCUMENT) s->reliable_applied = held->sequence;
    else s->frame_applied = held->sequence;
    s->held = held->next; if (!s->held) s->tail = NULL;
    --s->held_count; s->held_bytes -= held->bytes;
    qa_unified_session_delivery_free(held);
}

bool qa_unified_session_restart_prepare(qa_unified_session *s, bool *ready, bool *retiring, qa_error *e)
{
    if (!ready || !retiring || !qa_unified_session_idle(s) || !s->bound_source || !s->server)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production travel requires its returned actual server peer");
    *ready = true; *retiring = qa_unified_session_retiring(s);
    if (*retiring) return true;
    s->processing = true;
    bool okay = true, waiting = false;
    while (okay && s->held && s->held->source_finished && !s->closing && !s->disconnected && !s->timeout_pending) {
        if (!s->held->document || s->held->kind != QA_UNIFIED_CONTROL_DOCUMENT) {
            okay = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production travel retained a completed non-control server delivery");
            break;
        }
        okay = process_control(s, s->held, &waiting, e);
        if (!okay || waiting) break;
        delivery_applied(s);
    }
    s->processing = false;
    *ready = !waiting; *retiring = qa_unified_session_retiring(s);
    return okay;
}

bool qa_unified_session_offer_ready(const qa_unified_session *s, const qa_unified_document *offer,
    bool *ready, qa_error *e)
{
    if (!ready || !qa_unified_session_idle(s) || !s->bound_source || !s->server ||
        qa_unified_session_retiring(s) || !qa_unified_session_kind(offer, "offer") || s->epoch == UINT32_MAX)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production offer admission lacks its returned server epoch");
    uint32_t epoch;
    if (!qa_unified_document_epoch(offer, &epoch, e)) return false;
    if (epoch != s->epoch + 1)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production offer changes its actual next wire epoch");
    for (const qa_unified_held *held = s->held; held; held = held->next)
        if (held->source_finished)
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production offer admission retains a completed prior epoch reply");
    return control_admission(s, offer, ready, e);
}

bool qa_unified_session_process(qa_unified_session *s, bool *waiting, qa_error *e)
{
    if (!waiting || !qa_unified_session_idle(s) || !s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production preparation requires its idle runtime owner");
    *waiting = false; s->processing = true;
    bool ok = qa_unified_session_receive_resume(s, e);
    if (ok && s->timeout_pending && !s->closing && !s->disconnected) {
        const qa_unified_control timeout={.kind=QA_UNIFIED_CONTROL_DISCONNECT,
            .value.disconnect="Connection timed out"};
        if (!s->timeout_delivery) {
            ok=qa_unified_session_control_delivery(s,&timeout,&s->timeout_delivery,e);
        }
        if (ok) ok = process_control(s, s->timeout_delivery, waiting, e);
        if (ok && !*waiting) {
            s->timeout_pending = false; s->close_cause = 0;
            qa_unified_session_delivery_free(s->timeout_delivery); s->timeout_delivery = NULL;
        }
    }
    while (ok && s->held && !s->disconnected && !s->timeout_pending && !*waiting) {
        qa_unified_held *held = s->held;
        qa_unified_document_kind kind = held->kind;
        bool skipped = s->closing || (s->server && kind == QA_UNIFIED_INPUT_DOCUMENT && !s->admitted);
        if (!skipped && !held->document) {
            qa_error decode = {0};
            if (!qa_unified_document_decode(kind, (qa_bytes){held->wire.data, held->wire.size}, s->strings, held->lease, NULL, &held->document, &decode)) {
                if (!s->server || decode.code == QA_ERROR_MEMORY) {
                    if (e) *e = decode;
                    ok = false; break;
                }
                ok = qa_unified_session_close(s, decode.message[0] ? decode.message : "Invalid Unified message", e);
                if (!ok) break;
                skipped = true;
            }
        }
        bool obsolete = false;
        if (!skipped && !held->source_finished && !s->server && !qa_unified_session_kind(held->document, "disconnect")) {
            uint32_t epoch;
            ok = qa_unified_document_epoch(held->document, &epoch, e);
            if (!ok) break;
            obsolete = qa_unified_session_kind(held->document, "offer") ? epoch <= s->epoch : epoch != s->epoch;
            if (kind == QA_UNIFIED_FRAME_DOCUMENT && !s->admitted) obsolete = true;
        }
        if (!skipped && !held->source_finished && !s->server && !obsolete && !qa_unified_session_kind(held->document, "disconnect")) {
            bool ready = false;
            ok = s->hooks.prepare(s->hooks.context, s->id, held->document, &ready, e);
            if (!ok || !ready) { *waiting = ok; break; }
        }
        ok = skipped || obsolete || (kind == QA_UNIFIED_CONTROL_DOCUMENT ? process_control(s, held, waiting, e) :
            s->server ? process_input(s, held->document, e) : process_frame(s, held, waiting, e));
        if (!ok || *waiting) break;
        delivery_applied(s);
        ok = qa_unified_session_receive_resume(s, e);
    }
    s->processing = false;
    return ok;
}
