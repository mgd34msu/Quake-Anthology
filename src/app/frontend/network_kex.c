#include "network_kex_private.h"

#include <stdlib.h>
#include <string.h>

static bool found(void *context, const qa_net_address *address, uint64_t now, qa_error *e)
{
    frontend_kex_browser *b = context;
    size_t at = 0;
    while (at < b->query_count && !qa_net_address_equal(&b->queries[at].address, address, true)) ++at;
    if (b->query_count == 256 || (at < b->query_count &&
        (now < b->queries[at].sent_ns || now - b->queries[at].sent_ns < UINT64_C(1000000000)))) return true;
    uint8_t bytes[256];
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, sizeof(bytes), e);
    if (!qa_kex_discovery_query(&w)) return false;
    if (!b->hooks.send(b->hooks.context, address, (qa_bytes){bytes, qa_net_writer_size(&w)}, e)) return false;
    if (at == b->query_count) ++b->query_count;
    b->queries[at] = (frontend_kex_query){.address = *address, .sent_ns = now};
    return true;
}

bool frontend_kex_browser_open(qa_server_browser *browser,
    const frontend_kex_browser_hooks *hooks, frontend_kex_browser **out, qa_error *e)
{
    if (!browser || !hooks || !hooks->send || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing retail browser service or socket sender");
        return false;
    }
    frontend_kex_browser *b = calloc(1, sizeof(*b));
    if (!b) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating retail browser owner"); return false; }
    b->browser = browser;
    b->hooks = *hooks;
    const qa_kex_mdns_hooks mdns = {.context = b, .found = found};
    if (!qa_kex_mdns_owner_open(0, &mdns, &b->discovery, e)) { free(b); return false; }
    *out = b;
    return true;
}

bool frontend_kex_browser_scan(frontend_kex_browser *b, qa_error *e)
{
    if (!b || b->entered) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser scan requires idle ownership"); return false; }
    return qa_kex_mdns_owner_query(b->discovery, e);
}

bool frontend_kex_browser_pump(frontend_kex_browser *b, uint64_t now, qa_error *e)
{
    if (!b || b->entered) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser pump requires idle ownership"); return false; }
    b->entered = true;
    bool ok = qa_kex_mdns_owner_pump(b->discovery, now, e);
    b->entered = false;
    return ok;
}

bool frontend_kex_browser_receive(frontend_kex_browser *b, const qa_net_datagram *packet,
    bool *recognized, qa_error *e)
{
    if (!b || b->entered || !packet || !recognized) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser response requires idle ownership"); return false;
    }
    *recognized = false;
    if (packet->kind != QA_NET_POLL_PACKET) return true;
    size_t at = 0;
    while (at < b->query_count && !qa_net_address_equal(&b->queries[at].address, &packet->from, true)) ++at;
    if (at == b->query_count) return true;
    b->entered = true;
    bool ok = qa_server_browser_receive_kex_discovery(b->browser, packet, b->queries[at].sent_ns, recognized, e);
    if (ok && *recognized) {
        memmove(b->queries + at, b->queries + at + 1, (b->query_count - at - 1) * sizeof(*b->queries));
        --b->query_count;
    }
    b->entered = false;
    return ok;
}

bool frontend_kex_browser_idle(const frontend_kex_browser *b)
{
    return b && !b->entered && b->browser && b->hooks.send &&
        b->query_count <= 256 && qa_kex_mdns_owner_idle(b->discovery);
}

bool frontend_kex_browser_activate(frontend_kex_browser *b, qa_error *e)
{
    if (!frontend_kex_browser_idle(b)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser activation requires idle ownership"); return false;
    }
    return qa_kex_mdns_owner_activate(b->discovery, e);
}

bool frontend_kex_browser_publish(frontend_kex_browser *b, qa_error *e)
{
    if (!frontend_kex_browser_idle(b)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser publication requires idle ownership"); return false;
    }
    return qa_kex_mdns_owner_publish(b->discovery, e);
}

void frontend_kex_browser_destroy(frontend_kex_browser *b)
{
    if (!b || b->entered) return;
    qa_kex_mdns_owner_destroy(b->discovery);
    free(b);
}

/* The detached codec rebinds this genuine callback; it never invokes it. */
qa_kex_mdns_hooks frontend_kex_browser_mdns_hooks(frontend_kex_browser *b)
{
    return (qa_kex_mdns_hooks){.context = b, .found = found};
}
