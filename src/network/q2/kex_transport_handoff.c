#include "kex_transport_internal.h"
#include "kex_channel_internal.h"

#include <string.h>

static bool bytes_equal(const void *a, const void *b, size_t size)
{
    return !size || !memcmp(a, b, size);
}

static bool channel_equal(const qa_kex_channel *a, const qa_kex_channel *b)
{
    if (a->sequence != b->sequence || a->reliable != b->reliable ||
        a->incoming_sequence != b->incoming_sequence || a->incoming_reliable != b->incoming_reliable ||
        a->fragment_sequence != b->fragment_sequence || a->fragment_kind != b->fragment_kind ||
        a->ack != b->ack || a->fragmented != b->fragmented || a->pending_bytes != b->pending_bytes ||
        a->pending_count != b->pending_count || a->fragment_size != b->fragment_size ||
        a->fragment_capacity != b->fragment_capacity || a->expanded_size != b->expanded_size ||
        a->expanded_capacity != b->expanded_capacity || a->retries != b->retries ||
        a->retry_at != b->retry_at || a->received_at != b->received_at ||
        !bytes_equal(a->fragments, b->fragments, a->fragment_capacity) ||
        !bytes_equal(a->expanded, b->expanded, a->expanded_capacity)) return false;
    const struct pending *p = a->head, *q = b->head;
    for (; p && q; p = p->next, q = q->next)
        if (p->reliable != q->reliable || p->sent != q->sent || p->size != q->size ||
            !bytes_equal(p->bytes, q->bytes, p->size)) return false;
    return !p && !q;
}

static bool attributes_equal(const struct attributes *a, const struct attributes *b)
{
    if (a->count != b->count) return false;
    for (size_t i = 0; i < a->count; ++i)
        if (strcmp(a->data[i].key, b->data[i].key) || strcmp(a->data[i].value, b->data[i].value)) return false;
    return true;
}

static bool queued_equal(const struct queued *a, const struct queued *b)
{
    if (!a || !b) return a == b;
    return a->kind == b->kind && a->received == b->received && a->size == b->size &&
        qa_net_address_equal(&a->address, &b->address, true) && bytes_equal(a->bytes, b->bytes, a->size);
}

static bool lan_equal(const qa_kex_lan *a, const qa_kex_lan *b)
{
    if (!qa_net_address_equal(&a->local_address, &b->local_address, true) ||
        !qa_net_address_equal(&a->options.server, &b->options.server, true) ||
        a->options.host != b->options.host || a->options.max_players != b->options.max_players ||
        a->options.local_players != b->options.local_players || strcmp(a->name, b->name) ||
        a->peer_count != b->peer_count || a->player_count != b->player_count ||
        a->next_id != b->next_id || a->clock != b->clock || a->retry_at != b->retry_at ||
        a->joined != b->joined || a->retried != b->retried || a->local_first != b->local_first ||
        !bytes_equal(a->local_ids, b->local_ids, sizeof(a->local_ids)) ||
        !attributes_equal(&a->attributes, &b->attributes) || a->queued_count != b->queued_count ||
        a->dropped != b->dropped || !qa_net_address_equal(&a->dropped_from, &b->dropped_from, true) ||
        !queued_equal(a->borrowed, b->borrowed)) return false;
    for (size_t i = 0; i < a->player_count; ++i)
        if (a->players[i].id != b->players[i].id ||
            !attributes_equal(&a->players[i].attributes, &b->players[i].attributes)) return false;
    for (size_t i = 0; i < a->peer_count; ++i) {
        const struct peer *p = a->peers[i], *q = b->peers[i];
        if (p->count != q->count || !qa_net_address_equal(&p->address, &q->address, true) ||
            !bytes_equal(p->players, q->players, sizeof(p->players)) || !channel_equal(p->channel, q->channel)) return false;
    }
    const struct queued *p = a->head, *q = b->head;
    for (; p && q; p = p->next, q = q->next) if (!queued_equal(p, q)) return false;
    return !p && !q;
}

bool qa_kex_transport_handoff_ready(const qa_kex_transport *active, const qa_kex_transport *candidate, qa_error *e)
{
    if (!active || !candidate || active == candidate || !qa_kex_transport_idle(active) ||
        !qa_kex_transport_idle(candidate) || !active->published || !active->raw_owned ||
        candidate->raw || candidate->published || candidate->raw_owned ||
        active->raw_limit != candidate->raw_limit ||
        (active->discovery != NULL) != (candidate->discovery != NULL) ||
        !lan_equal(active->lobby, candidate->lobby)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX active transport advanced beyond the complete saved continuation"); return false;
    }
    return !active->discovery || qa_kex_mdns_owner_handoff_ready(active->discovery, candidate->discovery, e);
}

bool qa_kex_transport_handoff(qa_kex_transport *active, qa_kex_transport *candidate, qa_error *e)
{
    if (!qa_kex_transport_handoff_ready(active, candidate, e)) return false;
    if (active->discovery && !qa_kex_mdns_owner_handoff(active->discovery, candidate->discovery, e)) return false;
    candidate->raw = active->raw;
    candidate->raw_owned = candidate->published = true;
    active->raw = NULL;
    active->raw_owned = active->published = false;
    return true;
}
