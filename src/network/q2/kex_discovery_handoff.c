#include "kex_discovery_owner_internal.h"

static bool records_equal(const qa_kex_mdns_owner *a, const qa_kex_mdns_owner *b)
{
    if (a->advertised_port != b->advertised_port || a->closed != b->closed ||
        a->published != b->published || a->announce_pending != b->announce_pending ||
        a->endpoint_count != b->endpoint_count || a->address_count != b->address_count) return false;
    for (size_t i = 0; i < a->endpoint_count; ++i) {
        const qa_kex_mdns_endpoint *p = a->endpoints + i, *q = b->endpoints + i;
        if (p->port != q->port || !qa_kex_mdns_text_equal(p->instance, q->instance) ||
            !qa_kex_mdns_text_equal(p->target, q->target)) return false;
    }
    for (size_t i = 0; i < a->address_count; ++i) {
        const qa_kex_mdns_address *p = a->addresses + i, *q = b->addresses + i;
        if (!qa_kex_mdns_text_equal(p->target, q->target) ||
            !qa_net_address_equal(&p->address, &q->address, true)) return false;
    }
    return true;
}

bool qa_kex_mdns_owner_handoff_ready(const qa_kex_mdns_owner *active,
    const qa_kex_mdns_owner *candidate, qa_error *e)
{
    if (!active || !candidate || active == candidate ||
        !qa_kex_mdns_owner_idle(active) || !qa_kex_mdns_owner_idle(candidate) ||
        active->closed || !active->published || !active->bound_published || !active->socket ||
        candidate->socket || candidate->bound_published || !records_equal(active, candidate)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX mDNS socket advanced beyond its complete saved continuation");
        return false;
    }
    return true;
}

bool qa_kex_mdns_owner_handoff(qa_kex_mdns_owner *active,
    qa_kex_mdns_owner *candidate, qa_error *e)
{
    if (!qa_kex_mdns_owner_handoff_ready(active, candidate, e)) return false;
    candidate->socket = active->socket;
    candidate->bound_published = active->bound_published;
    active->socket = NULL;
    active->bound_published = active->published = active->announce_pending = false;
    return true;
}
