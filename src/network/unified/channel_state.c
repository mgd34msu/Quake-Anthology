#include "channel_internal.h"

static bool fail(qa_error *e, const char *message)
{
    qa_error_set(e, QA_ERROR_FORMAT, 0, "%s", message); return false;
}
static uint32_t channel_window(const qa_unified_channel *c)
{
    return c->limits.reliable_window_messages < c->limits.queued_reliable_messages ?
        c->limits.reliable_window_messages : c->limits.queued_reliable_messages;
}

static bool outgoing_valid(const qa_unified_channel *c, const outgoing *m, bool reliable, qa_error *e)
{
    if (!m || !m->payload.data || m->payload.size > c->limits.message_bytes || !m->sequence ||
        m->required >= c->next_reliable || (reliable && m->required) ||
        m->fragments != (m->payload.size ? (m->payload.size - 1) / (c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES) + 1 : 1) ||
        m->fragments > c->limits.fragments || m->next_fragment > m->fragments ||
        (reliable ? !m->sent : (m->sent || m->acknowledged || m->next || m->next_fragment == m->fragments)))
        return fail(e, "Invalid unified outgoing message continuation");
    if (!reliable) return true;
    uint32_t acknowledged = 0;
    for (uint32_t i = 0; i < m->fragments; ++i) {
        const sent_fragment *sent = m->sent + i;
        if (sent->attempts > c->limits.maximum_transmissions ||
            (i < m->next_fragment ? !sent->attempts : (sent->attempts || sent->at || sent->acknowledged)) ||
            (sent->acknowledged && !sent->attempts))
            return fail(e, "Invalid unified sent fragment receipt");
        acknowledged += sent->acknowledged;
    }
    return acknowledged == m->acknowledged || fail(e, "Unified acknowledged fragment counter disagrees");
}

static bool assembly_valid(const qa_unified_channel *c, const assembly *a, bool reliable, qa_error *e)
{
    if (!a || !a->payload.data || !a->received || !a->received_count || (reliable ? !a->pending_ack : a->pending_ack != NULL) ||
        !a->sequence || a->payload.size > c->limits.message_bytes ||
        !a->fragment_bytes || a->fragment_bytes > QA_UNIFIED_MAX_DATAGRAM - QA_UNIFIED_HEADER_BYTES ||
        a->fragments != (a->payload.size ? (a->payload.size - 1) / a->fragment_bytes + 1 : 1) ||
        !a->fragments || a->fragments > c->limits.fragments || (reliable && a->required))
        return fail(e, "Invalid unified incoming assembly continuation");
    uint32_t received = 0;
    for (uint32_t i = 0; i < a->fragments; ++i) {
        size_t offset = (size_t)i * a->fragment_bytes;
        size_t bytes = a->payload.size - offset;
        if (bytes > a->fragment_bytes) bytes = a->fragment_bytes;
        if (a->received[i] > 1 || (a->pending_ack && (a->pending_ack[i] > 1 || (a->pending_ack[i] && !a->received[i]))) ||
            (a->received[i] && bytes > c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES))
            return fail(e, "Invalid unified fragment bitmap or actual datagram extent");
        if (!a->received[i]) {
            for (size_t byte = 0; byte < bytes; ++byte)
                if (a->payload.data[offset + byte]) return fail(e, "Unreceived unified fragment changes its initialized backing");
        }
        received += a->received[i];
    }
    return received == a->received_count || fail(e, "Unified assembly received counter disagrees");
}

bool qa_unified_channel_valid(const qa_unified_channel *c, qa_error *e)
{
    if (!c || !c->packet || c->limits.datagram_bytes <= QA_UNIFIED_HEADER_BYTES ||
        c->limits.datagram_bytes > QA_UNIFIED_MAX_DATAGRAM || !c->limits.message_bytes ||
        c->limits.queued_reliable_bytes < c->limits.message_bytes || !c->limits.queued_reliable_messages ||
        !c->limits.reliable_window_messages || c->limits.reliable_window_messages > 64 ||
        !c->limits.fragments || !c->limits.packets_per_flush || !c->limits.maximum_transmissions ||
        !c->limits.retry_ns || !c->limits.assembly_ns || !c->next_reliable || !c->next_frame ||
        c->next_reliable > (uint64_t)UINT32_MAX + 1 || c->next_frame > (uint64_t)UINT32_MAX + 1 ||
        c->reliable_acknowledged >= c->next_reliable || c->frame_received > c->newest_frame ||
        c->lane >= 3 || c->reliable_cursor >= channel_window(c) ||
        (c->cumulative_sequence ? (c->cumulative_sequence > c->reliable_received || c->cumulative_fragment >= c->limits.fragments) :
            (c->cumulative_fragment || c->cumulative_pending)))
        return fail(e, "Invalid unified channel counters or source limits");
    size_t queued = 0, received = 0;
    uint32_t count = 0;
    uint64_t sequence = (uint64_t)c->reliable_acknowledged + 1;
    const outgoing *tail = NULL;
    for (const outgoing *m = c->reliable; m; m = m->next) {
        if (++count > c->limits.queued_reliable_messages || !outgoing_valid(c, m, true, e) ||
            m->sequence != sequence++ || m->payload.size > c->limits.queued_reliable_bytes - queued)
            return fail(e, "Unified reliable queue changes its actual ordering or budget");
        queued += m->payload.size; tail = m;
    }
    if (count != c->reliable_count || queued != c->queued_bytes || tail != c->tail || (!c->closed && sequence != c->next_reliable))
        return fail(e, "Unified reliable queue counters disagree");
    if (c->frame && (!outgoing_valid(c, c->frame, false, e) || c->frame->sequence >= c->next_frame))
        return fail(e, "Unified current frame exceeds its actual queue sequence");
    if (c->pending_frame && (!c->frame || !outgoing_valid(c, c->pending_frame, false, e) ||
        c->pending_frame->next_fragment || c->pending_frame->sequence <= c->frame->sequence ||
        (uint64_t)c->pending_frame->sequence + 1 != c->next_frame))
        return fail(e, "Unified pending frame does not retain its latest replacement");
    if (c->frame && !c->pending_frame && (uint64_t)c->frame->sequence + 1 != c->next_frame)
        return fail(e, "Unified current frame changes its actual queue sequence");
    for (size_t i = 0; i < 64; ++i) if (c->assemblies[i]) {
        const assembly *a = c->assemblies[i];
        if (!assembly_valid(c, a, true, e) || a->sequence <= c->reliable_received ||
            (uint64_t)a->sequence > (uint64_t)c->reliable_received + channel_window(c) ||
            a->payload.size > c->limits.queued_reliable_bytes - received)
            return fail(e, "Unified reliable assembly changes its actual source window");
        for (size_t j = 0; j < i; ++j) if (c->assemblies[j] && c->assemblies[j]->sequence == a->sequence)
            return fail(e, "Duplicate unified reliable assembly sequence");
        received += a->payload.size;
    }
    if (received != c->received_bytes) return fail(e, "Unified received assembly budget disagrees");
    if (c->frame_assembly && (!assembly_valid(c, c->frame_assembly, false, e) ||
        c->frame_assembly->sequence != c->newest_frame || c->frame_assembly->sequence <= c->frame_received ||
        c->frame_assembly->received_count == c->frame_assembly->fragments))
        return fail(e, "Invalid unified in-flight frame assembly");
    if (c->waiting_frame && (!assembly_valid(c, c->waiting_frame, false, e) ||
        c->waiting_frame->sequence > c->newest_frame || c->waiting_frame->sequence <= c->frame_received ||
        c->waiting_frame->received_count != c->waiting_frame->fragments ||
        (c->frame_assembly && c->frame_assembly->sequence == c->waiting_frame->sequence)))
        return fail(e, "Invalid unified reliable-dependent waiting frame");
    if (c->closed && (c->reliable || c->tail || c->frame || c->pending_frame || received || c->frame_assembly ||
        c->waiting_frame || c->cumulative_pending)) return fail(e, "Closed unified channel retains live queues");
    if (c->closed) for (size_t i = 0; i < 64; ++i) if (c->assemblies[i]) return fail(e, "Closed unified channel retains an assembly");
    return true;
}

bool qa_unified_channel_idle(const qa_unified_channel *c) { return c && !c->busy; }
bool qa_unified_channel_descriptor(const qa_unified_channel *c, qa_unified_token *token, qa_unified_limits *limits, qa_error *e)
{
    if (!token || !limits || !qa_unified_channel_idle(c) || !qa_unified_channel_valid(c, e)) return fail(e, "Unified descriptor requires the actual idle channel");
    *token = c->token; *limits = c->limits; return true;
}
bool qa_unified_channel_progress_read(const qa_unified_channel *c, qa_unified_progress *out, qa_error *e)
{
    if (!out || !qa_unified_channel_idle(c) || !qa_unified_channel_valid(c, e)) return fail(e, "Unified progress requires the actual idle channel");
    *out = (qa_unified_progress){c->next_reliable, c->next_frame, c->reliable_received,
        c->reliable_acknowledged, c->frame_received, c->newest_frame, 0};
    for (const outgoing *m = c->reliable; m; m = m->next)
        for (uint32_t i = 0; i < m->fragments; ++i)
            if (m->sent[i].at > out->time_ceiling) out->time_ceiling = m->sent[i].at;
    for (size_t i = 0; i < 64; ++i)
        if (c->assemblies[i] && c->assemblies[i]->started > out->time_ceiling) out->time_ceiling = c->assemblies[i]->started;
    if (c->frame_assembly && c->frame_assembly->started > out->time_ceiling) out->time_ceiling = c->frame_assembly->started;
    if (c->waiting_frame && c->waiting_frame->started > out->time_ceiling) out->time_ceiling = c->waiting_frame->started;
    return true;
}
