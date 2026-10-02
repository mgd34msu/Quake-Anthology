#include "session_internal.h"
#include "qa/network_unified_save.h"

#include <stdlib.h>
#include <string.h>

static bool token_equal(qa_unified_token a, qa_unified_token b) { return !memcmp(a.bytes, b.bytes, 16); }

bool qa_unified_session_qualified(const qa_unified_session *s, const qa_net_client *client, qa_error *e)
{
    qa_unified_token token; qa_unified_limits limits;
    qa_unified_progress progress;
    if (!s || !client || !qa_net_client_id_equal(s->id, client->id) || client->protocol.kind != QA_NET_UNIFIED_1 ||
        client->protocol.flags || client->protocol.revision || client->seat_count != 1 || !client->seats ||
        client->seats[0].seat.owner != s->seat.owner || client->seats[0].seat.index != s->seat.index ||
        !s->hooks.player || !s->hooks.control || (s->server ? (!s->hooks.input || !s->hooks.restart) : (!s->hooks.prepare || !s->hooks.frame)) ||
        s->acknowledged < -1 || (uint64_t)(s->acknowledged < 0 ? 0 : s->acknowledged) > QA_UNIFIED_SAFE_INTEGER ||
        (!s->epoch && (s->admitted || s->inputs.count || s->acknowledged != -1)) ||
        (s->admitted && (s->closing || s->disconnected || client->phase == QA_NET_CONNECTED || (s->server && client->phase != QA_NET_ACTIVE))) ||
        (s->closing && (s->closing_ns > s->now_ns || !s->required)) ||
        (s->timeout_pending && (s->closing || s->disconnected)) ||
        qa_unified_channel_closed(s->channel) || !qa_unified_channel_descriptor(s->channel, &token, &limits, e) ||
        !qa_unified_channel_progress_read(s->channel, &progress, e) || !token_equal(token, s->token) ||
        (uint64_t)s->required + 1 != progress.next_reliable || progress.time_ceiling > s->now_ns ||
        client->received_ns > s->now_ns || s->reliable_applied > progress.reliable_received || s->frame_applied > progress.frame_received ||
        limits.datagram_bytes != s->limits.datagram_bytes || limits.message_bytes != s->limits.message_bytes ||
        limits.queued_reliable_bytes != s->limits.queued_reliable_bytes || limits.queued_reliable_messages != s->limits.queued_reliable_messages ||
        limits.reliable_window_messages != s->limits.reliable_window_messages || limits.fragments != s->limits.fragments ||
        limits.packets_per_flush != s->limits.packets_per_flush || limits.maximum_transmissions != s->limits.maximum_transmissions ||
        limits.retry_ns != s->limits.retry_ns || limits.assembly_ns != s->limits.assembly_ns ||
        limits.queued_reliable_bytes > SIZE_MAX - limits.message_bytes || s->inputs.count > 64 ||
        (s->server && s->inputs.count) || (s->inputs.count && s->inputs.epoch != s->epoch))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production continuation lacks its actual channel, epoch or admitted seat");
    size_t count = 0, bytes = 0;
    const qa_unified_held *tail = NULL;
    uint64_t reliable = s->reliable_applied;
    size_t frames = 0;
    for (const qa_unified_held *held = s->held; held; held = held->next) {
        if (++count > (size_t)limits.reliable_window_messages + 1 || !held->document ||
            held->bytes != held->wire.size || (held->wire.size && !held->wire.data) ||
            held->wire.size > limits.message_bytes || held->wire.size > limits.queued_reliable_bytes + limits.message_bytes - bytes || !held->sequence)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Invalid held production document extent");
        qa_unified_document_kind kind = qa_unified_document_type(held->document);
        if ((held != s->held && held->source_finished) || !qa_unified_session_continuation_valid(s, held, e))
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Held Source continuation is not the actual processing head");
        if (kind == QA_UNIFIED_CONTROL_DOCUMENT) {
            if (held->required || held->sequence != reliable + 1 || held->sequence > progress.reliable_received)
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Held control differs from its actual reliable receipt");
            reliable = held->sequence;
        } else {
            if (kind != (s->server ? QA_UNIFIED_INPUT_DOCUMENT : QA_UNIFIED_FRAME_DOCUMENT) || ++frames > 1 ||
                held->required > progress.reliable_received || held->sequence != progress.frame_received || held->sequence <= s->frame_applied)
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Held production frame changes its actual receipt, role or dependency");
        }
        bytes += held->bytes; tail = held;
    }
    if (count != s->held_count || bytes != s->held_bytes || tail != s->tail || reliable != progress.reliable_received ||
        (!frames && s->frame_applied != progress.frame_received))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Held production queue counters disagree");
    if (s->timeout_delivery) {
        const qa_unified_held *held = s->timeout_delivery;
        const qa_json_document *json = qa_unified_document_json(held->document);
        if (!s->timeout_pending || held->next || held->sequence || held->required || !held->wire.data ||
            held->wire.size != held->bytes || held->wire.size > 512 ||
            !qa_unified_session_kind(held->document, "disconnect") ||
            !qa_json_string_equal(json, qa_json_get(json, qa_unified_session_value(held->document), "reason"), "Connection timed out") ||
            !qa_unified_session_continuation_valid(s, held, e))
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained timeout is not the actual local closure continuation");
    }
    for (size_t i = 0; i < s->inputs.count; ++i) {
        const qa_unified_input *input = s->inputs.commands + i;
        if (input->sequence > QA_UNIFIED_SAFE_INTEGER ||
            (s->acknowledged >= 0 && input->sequence <= (uint64_t)s->acknowledged))
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained production input is already acknowledged or exceeds its source domain");
        for (size_t j = 0; j < i; ++j) if (s->inputs.commands[j].sequence == input->sequence)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Duplicate retained production input sequence");
    }
    return true;
}

bool qa_unified_session_checkpoint(const qa_unified_session *s, qa_buffer *out, qa_error *e)
{
    const qa_net_client *client = s ? qa_net_connections_get(qa_network_connections(s->runtime), s->id) : NULL;
    if (!out || !qa_unified_session_idle(s) || !qa_unified_channel_idle(s->channel) || !qa_unified_session_qualified(s, client, e))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production capture requires the actual idle session graph");
    qa_buffer channel = {0}, inputs = {0};
    qa_unified_document *document = NULL;
    bool ok = qa_unified_channel_checkpoint(s->channel, &channel, e);
    if (ok && s->epoch) ok = qa_unified_inputs_document(s->epoch, s->inputs.commands, s->inputs.count, &document, e) &&
        qa_unified_document_encode(document, &inputs, e);
    qa_unified_document_destroy(document);
    size_t capacity = 65;
    if (channel.size > UINT32_MAX || inputs.size > UINT32_MAX || channel.size > SIZE_MAX - capacity ||
        inputs.size > SIZE_MAX - capacity - channel.size || s->held_bytes > SIZE_MAX - capacity - channel.size - inputs.size ||
        s->held_count > (SIZE_MAX - capacity - channel.size - inputs.size - s->held_bytes) / 17)
        ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Production continuation exceeds storage");
    if (ok) capacity += channel.size + inputs.size + s->held_bytes + s->held_count * 17;
    for (const qa_unified_held *held = s->held; ok && held; held = held->next)
        ok = qa_unified_session_continuation_extent(held, &capacity, e);
    if (ok && s->timeout_delivery) {
        if (capacity > SIZE_MAX - 4 || s->timeout_delivery->wire.size > SIZE_MAX - capacity - 4)
            ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Timeout continuation exceeds storage");
        else capacity += s->timeout_delivery->wire.size + 4;
        if (ok) ok = qa_unified_session_continuation_extent(s->timeout_delivery, &capacity, e);
    }
    uint8_t *data = ok ? malloc(capacity) : NULL;
    if (ok && !data) ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Capturing complete production session");
    qa_net_writer w; qa_net_writer_init(&w, data, ok ? capacity : 0, e);
    ok = ok && qa_net_write_data(&w, "QAUS2", 5) && qa_net_write_u8(&w, s->server) &&
        qa_net_write_u32(&w, s->epoch) && qa_net_write_u32(&w, s->required) && qa_net_write_u64(&w, (uint64_t)s->acknowledged) &&
        qa_net_write_u64(&w, s->now_ns) && qa_net_write_u64(&w, s->closing_ns) &&
        qa_net_write_u8(&w, s->admitted) && qa_net_write_u8(&w, s->disconnected) && qa_net_write_u8(&w, s->closing) &&
        qa_net_write_u8(&w, s->timeout_pending) &&
        qa_net_write_u32(&w, s->reliable_applied) && qa_net_write_u32(&w, s->frame_applied) &&
        qa_net_write_u32(&w, (uint32_t)channel.size) && qa_net_write_data(&w, channel.data, channel.size) &&
        qa_net_write_u32(&w, (uint32_t)inputs.size) && qa_net_write_data(&w, inputs.data, inputs.size) &&
        qa_net_write_u16(&w, (uint16_t)s->held_count);
    for (const qa_unified_held *held = s->held; ok && held; held = held->next)
        ok = qa_net_write_u8(&w, (uint8_t)qa_unified_document_type(held->document)) &&
            qa_net_write_u32(&w, held->sequence) && qa_net_write_u32(&w, held->required) &&
            qa_net_write_u32(&w, (uint32_t)held->wire.size) && qa_net_write_data(&w, held->wire.data, held->wire.size) &&
            qa_unified_session_continuation_write(&w, held);
    ok = ok && qa_net_write_u8(&w, s->timeout_delivery != NULL);
    if (ok && s->timeout_delivery) ok = qa_net_write_u32(&w, (uint32_t)s->timeout_delivery->wire.size) &&
        qa_net_write_data(&w, s->timeout_delivery->wire.data, s->timeout_delivery->wire.size) &&
        qa_unified_session_continuation_write(&w, s->timeout_delivery);
    qa_buffer_free(&channel); qa_buffer_free(&inputs);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}

bool qa_unified_session_source_ready(const qa_unified_session *s, qa_error *e)
{
    const qa_net_client *client = s ? qa_net_connections_get(qa_network_connections(s->runtime), s->id) : NULL;
    if (!qa_unified_session_idle(s) || !qa_unified_session_qualified(s, client, e)) return false;
    if (!s->admitted || (!s->server && client->phase != QA_NET_ACTIVE)) return true;
    qa_unified_session_player player;
    return qa_unified_session_player_read(s, &player, e);
}

void qa_unified_session_source_publish(qa_unified_session *s) { s->bound_source = true; }
void qa_unified_session_source_retire(qa_unified_session *s) { s->bound_source = false; }
bool qa_unified_session_source_retired(const qa_unified_session *s)
{
    return qa_unified_session_idle(s) && !s->bound_source;
}

static bool flag(qa_net_reader *r, bool *out)
{
    uint8_t value = qa_net_read_u8(r);
    if (value > 1) return qa_net_reader_fail(r, "Invalid production session flag");
    *out = value != 0; return !r->failed;
}

static bool blob(qa_net_reader *r, qa_bytes *out) { return qa_net_read_bytes(r, qa_net_read_u32(r), out); }

bool qa_unified_session_restore(qa_bytes bytes, qa_network_runtime *runtime, const qa_net_client *client,
    const qa_unified_session_hooks *hooks, qa_unified_session **out, qa_network_peer_ops *ops, qa_error *e)
{
    if (!runtime || !client || !hooks || !out || !ops || !qa_network_callbacks_idle(runtime) ||
        client->seat_count != 1 || !client->seats || client->protocol.kind != QA_NET_UNIFIED_1)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restore lacks its actual candidate connection");
    qa_unified_session *s = calloc(1, sizeof(*s));
    if (!s) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring complete production session");
    s->runtime = runtime; s->id = client->id; s->seat = client->seats[0].seat; s->hooks = *hooks;
    qa_net_reader r; qa_net_reader_init(&r, bytes, e);
    char magic[5]; bool ok = qa_net_read_data(&r, magic, 5);
    if (ok && memcmp(magic, "QAUS2", 5)) ok = qa_net_reader_fail(&r, "Unknown production session continuation");
    ok = ok && flag(&r, &s->server);
    s->epoch = qa_net_read_u32(&r); s->required = qa_net_read_u32(&r);
    uint64_t acknowledged = qa_net_read_u64(&r);
    if (acknowledged != UINT64_MAX && acknowledged > QA_UNIFIED_SAFE_INTEGER)
        ok = qa_net_reader_fail(&r, "Invalid production input acknowledgement");
    s->acknowledged = acknowledged == UINT64_MAX ? -1 : (int64_t)acknowledged;
    s->now_ns = qa_net_read_u64(&r); s->closing_ns = qa_net_read_u64(&r);
    ok = ok && flag(&r, &s->admitted) && flag(&r, &s->disconnected) && flag(&r, &s->closing) && flag(&r, &s->timeout_pending);
    s->reliable_applied = qa_net_read_u32(&r); s->frame_applied = qa_net_read_u32(&r);
    qa_bytes channel = {0}, inputs = {0};
    ok = ok && blob(&r, &channel) && qa_unified_channel_restore(channel, &s->channel, e) &&
        qa_unified_channel_descriptor(s->channel, &s->token, &s->limits, e) && blob(&r, &inputs);
    if (ok && ((s->epoch != 0) != (inputs.size != 0)))
        ok = qa_net_reader_fail(&r, "Production input continuation changes its retained epoch presence");
    if (ok && inputs.size) {
        qa_unified_document *document = NULL;
        ok = qa_unified_document_decode(QA_UNIFIED_INPUT_DOCUMENT, inputs, &document, e) &&
            qa_unified_inputs_read(document, &s->inputs, e);
        if (ok && s->inputs.epoch != s->epoch)
            ok = qa_net_reader_fail(&r, "Production input continuation changes its retained epoch");
        qa_unified_document_destroy(document);
    }
    size_t count = qa_net_read_u16(&r);
    if (ok && count > (size_t)s->limits.reliable_window_messages + 1) ok = qa_net_reader_fail(&r, "Held production document count exceeds its source window");
    for (size_t i = 0; ok && i < count; ++i) {
        qa_unified_document_kind kind = (qa_unified_document_kind)qa_net_read_u8(&r);
        qa_unified_held *held = calloc(1, sizeof(*held));
        if (!held) { ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring held production delivery"); break; }
        if (s->tail) s->tail->next = held; else s->held = held; s->tail = held; ++s->held_count;
        held->sequence = qa_net_read_u32(&r); held->required = qa_net_read_u32(&r);
        qa_bytes wire = {0}; ok = blob(&r, &wire);
        if (ok && (wire.size > s->limits.message_bytes || s->limits.queued_reliable_bytes > SIZE_MAX - s->limits.message_bytes ||
            wire.size > s->limits.queued_reliable_bytes + s->limits.message_bytes - s->held_bytes))
            ok = qa_net_reader_fail(&r, "Held production document bytes exceed its authentic budget");
        if (ok && kind != QA_UNIFIED_CONTROL_DOCUMENT && kind != (s->server ? QA_UNIFIED_INPUT_DOCUMENT : QA_UNIFIED_FRAME_DOCUMENT))
            ok = qa_net_reader_fail(&r, "Held production document changes its authenticated role");
        if (ok) {
            held->wire.data = malloc(wire.size ? wire.size : 1);
            if (!held->wire.data) ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring full production delivery bytes");
        }
        if (ok) {
            held->wire.size = held->bytes = wire.size; s->held_bytes += wire.size;
            if (wire.size) memcpy(held->wire.data, wire.data, wire.size);
            ok = qa_unified_document_decode(kind, (qa_bytes){held->wire.data, held->wire.size}, &held->document, e) &&
                qa_unified_session_continuation_read(&r, held);
        }
    }
    bool timeout = false;
    ok = ok && flag(&r, &timeout);
    if (ok && timeout) {
        qa_unified_held *held = calloc(1, sizeof(*held));
        if (!held) ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring actual timeout continuation");
        s->timeout_delivery = held;
        qa_bytes wire = {0};
        if (ok) ok = blob(&r, &wire);
        if (ok && wire.size > 512) ok = qa_net_reader_fail(&r, "Timeout continuation exceeds its actual control extent");
        if (ok) {
            held->wire.data = malloc(wire.size ? wire.size : 1);
            if (!held->wire.data) ok = qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring actual timeout control bytes");
        }
        if (ok) {
            held->wire.size = held->bytes = wire.size;
            if (wire.size) memcpy(held->wire.data, wire.data, wire.size);
            ok = qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT, wire, &held->document, e) &&
                qa_unified_session_continuation_read(&r, held);
        }
    }
    ok = ok && qa_net_reader_finish(&r) && qa_unified_session_qualified(s, client, e);
    if (!ok) { qa_unified_session_release(s); return false; }
    *out = s; *ops = qa_unified_session_operations(); return true;
}
