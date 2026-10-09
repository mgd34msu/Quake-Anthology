#include "session_internal.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_events.h"
#include "channel_internal.h"

#include <string.h>

static const qa_unified_document *response(const qa_unified_session_commit *commit, size_t at)
{
    return commit->reply && !at ? commit->reply : commit->followups[at - (commit->reply != NULL)];
}

bool qa_unified_session_continuation_valid(const qa_unified_session *s, const qa_unified_held *held, qa_error *e)
{
    const qa_unified_session_commit *commit = &held->commit;
    if (!held->source_finished) {
        if (held->responses_queued || held->response_first || held->response_last || commit->applied ||
            commit->acknowledged_input || commit->reply || commit->followup_count)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Unprocessed production delivery retains a fabricated Source commit");
        for (size_t i = 0; i < 8; ++i) if (commit->followups[i])
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Unprocessed production delivery retains a Source reply");
        return true;
    }
    qa_unified_document_kind kind = qa_unified_document_type(held->document);
    if (commit->followup_count > 8 || commit->acknowledged_input < -1 ||
        (uint64_t)(commit->acknowledged_input < 0 ? 0 : commit->acknowledged_input) > QA_UNIFIED_SAFE_INTEGER ||
        (kind != QA_UNIFIED_CONTROL_DOCUMENT && (s->server || kind != QA_UNIFIED_FRAME_DOCUMENT)))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Source commit changes its actual delivery role or extent");
    for (size_t i = commit->followup_count; i < 8; ++i) if (commit->followups[i])
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Source commit retains a reply outside its actual return extent");
    if (kind == QA_UNIFIED_CONTROL_DOCUMENT) {
        if (qa_unified_session_kind(held->document, "offer")) {
            uint32_t epoch;
            if (!commit->applied || s->server || !qa_unified_document_epoch(held->document, &epoch, e) || epoch != s->epoch)
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained offer commit changes its published epoch");
        }
        if (qa_unified_session_kind(held->document, "ready") && commit->applied &&
            (!s->server || !commit->reply || !qa_unified_session_kind(commit->reply, "admitted")))
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained readiness commit loses its admitted reply");
        if (qa_unified_session_kind(held->document, "disconnect") && commit->applied && s->admitted)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained disconnect commit still admits its retired Source player");
    } else if (commit->applied) {
        const qa_unified_frame *frame=qa_unified_document_frame(held->document);
        if (!frame || commit->acknowledged_input != frame->acknowledged_input)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained frame commit changes its genuine input acknowledgement");
    }
    size_t count = commit->followup_count + (commit->reply != NULL);
    if (held->responses_queued ? (count ? (!held->response_first ||
            (uint64_t)held->response_first + count - 1 != held->response_last || held->response_last > s->required) :
            (held->response_first || held->response_last)) : (held->response_first || held->response_last))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Source reply continuation changes its actual reliable receipts");
    bool disconnect = false;
    for (size_t i = 0; i < count; ++i) {
        const qa_unified_document *document = response(commit, i);
        if (disconnect)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Source reply continuation follows a queued disconnect");
        if (!qa_unified_session_reply_valid(s, document, e)) return false;
        disconnect = qa_unified_session_kind(document, "disconnect");
        qa_buffer wire = {0};
        if (!qa_unified_document_encode(document, &wire, e)) return false;
        bool okay = wire.size <= s->limits.message_bytes;
        if (okay && held->responses_queued) {
            uint32_t sequence = held->response_first + (uint32_t)i;
            if (sequence > s->channel->reliable_acknowledged) {
                const outgoing *queued = s->channel->reliable;
                while (queued && queued->sequence != sequence) queued = queued->next;
                okay = queued && qa_unified_payload_equal(&queued->payload, (qa_bytes){wire.data, wire.size});
            }
        }
        qa_buffer_free(&wire);
        if (!okay) return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Retained Source reply differs from its authentic reliable bytes");
    }
    return true;
}

static bool extent_add(size_t *capacity, size_t amount, qa_error *e)
{
    if (amount > SIZE_MAX - *capacity)
        return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Source reply continuation exceeds storage");
    *capacity += amount; return true;
}

bool qa_unified_session_continuation_extent(const qa_unified_held *held, size_t *capacity, qa_error *e)
{
    if (!extent_add(capacity, 21, e)) return false;
    size_t count = held->commit.followup_count + (held->commit.reply != NULL);
    for (size_t i = 0; i < count; ++i) {
        qa_buffer wire = {0};
        if (!qa_unified_document_encode(response(&held->commit, i), &wire, e)) return false;
        bool okay = wire.size <= UINT32_MAX && extent_add(capacity, 4, e) && extent_add(capacity, wire.size, e);
        qa_buffer_free(&wire);
        if (!okay) return false;
    }
    return true;
}

static bool document_write(qa_net_writer *w, const qa_unified_document *document)
{
    qa_buffer wire = {0};
    if (!qa_unified_document_encode(document, &wire, w->error)) return false;
    bool okay = wire.size <= UINT32_MAX && qa_net_write_u32(w, (uint32_t)wire.size) && qa_net_write_data(w, wire.data, wire.size);
    qa_buffer_free(&wire); return okay;
}

bool qa_unified_session_continuation_write(qa_net_writer *w, const qa_unified_held *held)
{
    const qa_unified_session_commit *commit = &held->commit;
    bool okay = qa_net_write_u8(w, held->source_finished) && qa_net_write_u8(w, held->responses_queued) &&
        qa_net_write_u32(w, held->response_first) && qa_net_write_u32(w, held->response_last) &&
        qa_net_write_u8(w, commit->applied) && qa_net_write_u64(w, (uint64_t)commit->acknowledged_input) &&
        qa_net_write_u8(w, commit->reply != NULL) && (!commit->reply || document_write(w, commit->reply)) &&
        qa_net_write_u8(w, (uint8_t)commit->followup_count);
    for (size_t i = 0; okay && i < commit->followup_count; ++i) okay = document_write(w, commit->followups[i]);
    return okay;
}

static bool flag(qa_net_reader *r, bool *out)
{
    uint8_t value = qa_net_read_u8(r);
    if (value > 1) return qa_net_reader_fail(r, "Invalid Source reply continuation flag");
    *out = value != 0; return !r->failed;
}

static bool document_read(qa_net_reader *r, qa_unified_document **out, qa_strings *strings)
{
    qa_bytes wire;
    return qa_net_read_bytes(r, qa_net_read_u32(r), &wire) &&
        qa_unified_document_decode(QA_UNIFIED_CONTROL_DOCUMENT, wire, strings, out, r->error);
}

bool qa_unified_session_continuation_read(qa_net_reader *r, qa_unified_held *held, qa_strings *strings)
{
    qa_unified_session_commit *commit = &held->commit;
    bool okay = flag(r, &held->source_finished) && flag(r, &held->responses_queued);
    held->response_first = qa_net_read_u32(r); held->response_last = qa_net_read_u32(r);
    okay = okay && flag(r, &commit->applied);
    uint64_t acknowledged = qa_net_read_u64(r);
    if (acknowledged != UINT64_MAX && acknowledged > QA_UNIFIED_SAFE_INTEGER)
        okay = qa_net_reader_fail(r, "Source reply acknowledgement exceeds its genuine input domain");
    commit->acknowledged_input = acknowledged == UINT64_MAX ? -1 : (int64_t)acknowledged;
    bool present = false;
    okay = okay && flag(r, &present) && (!present || document_read(r, &commit->reply, strings));
    commit->followup_count = qa_net_read_u8(r);
    if (commit->followup_count > 8) return qa_net_reader_fail(r, "Source reply continuation exceeds its actual return extent");
    for (size_t i = 0; okay && i < commit->followup_count; ++i) okay = document_read(r, commit->followups + i, strings);
    return okay && !r->failed;
}
