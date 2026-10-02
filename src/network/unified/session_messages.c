#include "session_internal.h"
#include "channel_internal.h"

#include <stdlib.h>

qa_json_id qa_unified_session_value(const qa_unified_document *d)
{
    const qa_json_document *json = qa_unified_document_json(d);
    return qa_unified_document_type(d) == QA_UNIFIED_CONTROL_DOCUMENT ?
        qa_json_get(json, qa_unified_document_root(d), "value") : qa_unified_document_root(d);
}

bool qa_unified_session_kind(const qa_unified_document *d, const char *kind)
{
    return d && qa_unified_document_type(d) == QA_UNIFIED_CONTROL_DOCUMENT &&
        qa_json_string_equal(qa_unified_document_json(d),
            qa_json_get(qa_unified_document_json(d), qa_unified_session_value(d), "kind"), kind);
}

bool qa_unified_session_document_epoch(const qa_unified_document *d, uint32_t *out, qa_error *e)
{
    double epoch;
    if (!d || !out || !qa_unified_document_number(d,
        qa_json_get(qa_unified_document_json(d), qa_unified_session_value(d), "epoch"), &epoch, e)) return false;
    if (epoch < 1 || epoch > UINT32_MAX)
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production epoch exceeds the control namespace");
    *out = (uint32_t)epoch; return true;
}

static bool server_control(const qa_unified_document *d)
{
    return qa_unified_session_kind(d, "offer") || qa_unified_session_kind(d, "admitted") ||
        qa_unified_session_kind(d, "resources") || qa_unified_session_kind(d, "components") ||
        qa_unified_session_kind(d, "events");
}

static bool client_control(const qa_unified_document *d)
{
    return qa_unified_session_kind(d, "ready") || qa_unified_session_kind(d, "userinfo") ||
        qa_unified_session_kind(d, "command") || qa_unified_session_kind(d, "component-command");
}

bool qa_unified_session_reply_valid(const qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!d || qa_unified_document_type(d) != QA_UNIFIED_CONTROL_DOCUMENT || qa_unified_session_kind(d, "offer") ||
        (!qa_unified_session_kind(d, "disconnect") && !(s->server ? server_control(d) : client_control(d))))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its authenticated control direction");
    if (qa_unified_session_kind(d, "disconnect")) return true;
    uint32_t epoch;
    if (!qa_unified_session_document_epoch(d, &epoch, e)) return false;
    return epoch == s->epoch || qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its retained control epoch");
}

bool qa_unified_session_queue_control(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!s || !d || qa_unified_document_type(d) != QA_UNIFIED_CONTROL_DOCUMENT || s->disconnected || s->closing ||
        (!qa_unified_session_kind(d, "disconnect") && !(s->server ? server_control(d) : client_control(d))))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control changes its authenticated direction");
    bool offer = qa_unified_session_kind(d, "offer"), disconnect = qa_unified_session_kind(d, "disconnect");
    uint32_t epoch = s->epoch;
    if (!disconnect && !qa_unified_session_document_epoch(d, &epoch, e)) return false;
    if (!disconnect && (offer ? epoch <= s->epoch : epoch != s->epoch))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production control changes its retained world epoch");
    qa_buffer encoded = {0};
    if (!qa_unified_document_encode(d, &encoded, e)) return false;
    uint32_t sequence;
    bool ok = qa_unified_channel_reliable(s->channel, (qa_bytes){encoded.data, encoded.size}, &sequence, e);
    qa_buffer_free(&encoded);
    if (!ok) return false;
    s->required = sequence;
    if (offer) {
        s->epoch = epoch; s->admitted = false; s->acknowledged = -1;
        qa_unified_inputs_free(&s->inputs);
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

bool qa_unified_session_frame(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source || !s->server || !s->admitted || s->disconnected || s->closing ||
        !d || qa_unified_document_type(d) != QA_UNIFIED_FRAME_DOCUMENT)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame lacks an admitted server producer");
    uint32_t epoch;
    if (!qa_unified_session_document_epoch(d, &epoch, e)) return false;
    if (epoch != s->epoch) return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame belongs to another epoch");
    double acknowledged;
    if (!qa_unified_document_number(d, qa_json_get(qa_unified_document_json(d),
        qa_unified_document_root(d), "acknowledgedInput"), &acknowledged, e)) return false;
    if (acknowledged < s->acknowledged)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production frame acknowledgement regressed");
    qa_buffer encoded = {0};
    if (!qa_unified_document_encode(d, &encoded, e)) return false;
    bool ok = qa_unified_channel_frame(s->channel, (qa_bytes){encoded.data, encoded.size}, s->required, e);
    qa_buffer_free(&encoded);
    if (ok) s->acknowledged = (int64_t)acknowledged;
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

static bool commit_queue(qa_unified_session *s, qa_unified_held *held, qa_error *e)
{
    const qa_unified_session_commit *commit = &held->commit;
    if (held->responses_queued) return true;
    if (commit->followup_count > 8)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source control followups exceed their actual return extent");
    qa_buffer encoded[9] = {0}; qa_bytes payloads[9];
    size_t count = commit->followup_count + (commit->reply != NULL);
    bool disconnect = false, ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_unified_document *d = commit->reply && !i ? commit->reply :
            commit->followups[i - (commit->reply != NULL)];
        bool next_disconnect = qa_unified_session_kind(d, "disconnect");
        if (disconnect || s->closing || s->disconnected) {
            ok = qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source reply changes its authenticated control direction"); break;
        }
        ok = qa_unified_session_reply_valid(s, d, e);
        if (ok) ok = qa_unified_document_encode(d, encoded + i, e);
        payloads[i] = (qa_bytes){encoded[i].data, encoded[i].size};
        disconnect = next_disconnect;
    }
    if (ok) ok = qa_unified_channel_reliable_batch(s->channel, payloads, count,
        &held->response_first, &held->response_last, e);
    for (size_t i = 0; i < 9; ++i) qa_buffer_free(encoded + i);
    if (!ok) return false;
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

void qa_unified_session_delivery_free(qa_unified_held *held)
{
    if (!held) return;
    commit_free(&held->commit);
    qa_unified_document_destroy(held->document); qa_buffer_free(&held->wire); free(held);
}

static bool process_control(qa_unified_session *s, qa_unified_held *held, qa_error *e)
{
    const qa_unified_document *d = held->document;
    bool disconnect = qa_unified_session_kind(d, "disconnect"), offer = qa_unified_session_kind(d, "offer");
    if (!disconnect && !(s->server ? client_control(d) : server_control(d)))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Received production control changes its authenticated direction");
    uint32_t epoch = s->epoch;
    if (!disconnect && !qa_unified_session_document_epoch(d, &epoch, e)) return false;
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
            qa_unified_inputs_free(&s->inputs);
        }
        if (commit.applied && disconnect) s->admitted = false;
    }
    ok = commit_queue(s, held, e);
    if (ok && !s->closing && held->commit.applied && qa_unified_session_kind(d, "ready")) {
        ok = phase(s, QA_NET_ACTIVE, e);
        if (ok) s->admitted = true;
    }
    if (ok && held->commit.applied && qa_unified_session_kind(d, "admitted")) {
        ok = phase(s, QA_NET_PRIMED, e);
        if (ok) s->admitted = true;
    }
    if (ok && disconnect) {
        if (s->server && !s->closing) ok = qa_unified_session_queue_control(s, d, e);
        else if (!s->server) s->disconnected = true;
    }
    return ok;
}

static bool process_frame(qa_unified_session *s, qa_unified_held *held, qa_error *e)
{
    const qa_unified_document *d = held->document;
    uint32_t epoch;
    if (!qa_unified_session_document_epoch(d, &epoch, e)) return false;
    if (!held->source_finished && (epoch != s->epoch || !s->admitted)) return true;
    double wire_ack;
    if (!qa_unified_document_number(d, qa_json_get(qa_unified_document_json(d),
        qa_unified_document_root(d), "acknowledgedInput"), &wire_ack, e)) return false;
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
    bool ok = commit_queue(s, held, e);
    if (ok && !s->closing && held->commit.applied) ok = phase(s, QA_NET_ACTIVE, e);
    if (ok && held->commit.applied) qa_unified_session_ack(s, held->commit.acknowledged_input);
    return ok;
}

static bool process_input(qa_unified_session *s, const qa_unified_document *d, qa_error *e)
{
    qa_unified_input_batch batch = {0};
    if (!qa_unified_inputs_read(d, &batch, e)) return false;
    bool ok = batch.epoch != s->epoch || !s->admitted ||
        s->hooks.input(s->hooks.context, s->runtime, s->id, &batch, e);
    qa_unified_inputs_free(&batch);
    return ok;
}

bool qa_unified_session_process(qa_unified_session *s, bool *waiting, qa_error *e)
{
    if (!waiting || !qa_unified_session_idle(s) || !s->bound_source)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production preparation requires its idle runtime owner");
    *waiting = false; s->processing = true;
    bool ok = true;
    if (s->timeout_pending && !s->closing && !s->disconnected) {
        static const char json[] = "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"disconnect\",\"reason\":\"Connection timed out\"}}";
        if (!s->timeout_delivery) {
            s->timeout_delivery = calloc(1, sizeof(*s->timeout_delivery));
            if (!s->timeout_delivery) ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining actual timeout control continuation");
            if (ok) ok = qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
                (qa_bytes){(const uint8_t *)json, sizeof(json) - 1}, &s->timeout_delivery->document, e);
            if (ok) ok = qa_unified_document_encode(s->timeout_delivery->document, &s->timeout_delivery->wire, e);
            if (ok) s->timeout_delivery->bytes = s->timeout_delivery->wire.size;
            else { qa_unified_session_delivery_free(s->timeout_delivery); s->timeout_delivery = NULL; }
        }
        if (ok) ok = process_control(s, s->timeout_delivery, e);
        if (ok) {
            s->timeout_pending = false;
            qa_unified_session_delivery_free(s->timeout_delivery); s->timeout_delivery = NULL;
        }
    }
    while (ok && s->held && !s->disconnected) {
        qa_unified_held *held = s->held;
        qa_unified_document_kind kind = qa_unified_document_type(held->document);
        bool obsolete = false;
        if (!held->source_finished && !s->server && !qa_unified_session_kind(held->document, "disconnect")) {
            uint32_t epoch;
            ok = qa_unified_session_document_epoch(held->document, &epoch, e);
            if (!ok) break;
            obsolete = qa_unified_session_kind(held->document, "offer") ? epoch <= s->epoch : epoch != s->epoch;
            if (kind == QA_UNIFIED_FRAME_DOCUMENT && !s->admitted) obsolete = true;
        }
        if (!held->source_finished && !s->server && !s->closing && !obsolete && !qa_unified_session_kind(held->document, "disconnect")) {
            bool ready = false;
            ok = s->hooks.prepare(s->hooks.context, s->id, held->document, &ready, e);
            if (!ok || !ready) { *waiting = ok; break; }
        }
        ok = s->closing || obsolete || (kind == QA_UNIFIED_CONTROL_DOCUMENT ? process_control(s, held, e) :
            s->server ? process_input(s, held->document, e) : process_frame(s, held, e));
        if (!ok) break;
        if (kind == QA_UNIFIED_CONTROL_DOCUMENT) s->reliable_applied = held->sequence;
        else s->frame_applied = held->sequence;
        s->held = held->next; if (!s->held) s->tail = NULL;
        --s->held_count; s->held_bytes -= held->bytes;
        qa_unified_session_delivery_free(held);
    }
    s->processing = false;
    return ok;
}
