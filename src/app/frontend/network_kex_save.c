#include "network_kex_private.h"

#include <stdlib.h>
#include <string.h>

static bool query_valid(const frontend_kex_browser *b)
{
    if (!frontend_kex_browser_idle(b)) return false;
    for (size_t i = 0; i < b->query_count; ++i) {
        const qa_net_address *a = &b->queries[i].address;
        if (!a->port || (a->kind != QA_NET_IPV4 && a->kind != QA_NET_IPV6)) return false;
        for (size_t j = 0; j < i; ++j)
            if (qa_net_address_equal(a, &b->queries[j].address, true)) return false;
    }
    return true;
}

bool frontend_kex_browser_checkpoint(const frontend_kex_browser *b, qa_buffer *out, qa_error *e)
{
    if (!out || !query_valid(b)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Retail browser continuation requires idle native ownership");
        return false;
    }
    qa_buffer discovery = {0};
    if (!qa_kex_mdns_owner_checkpoint(b->discovery, &discovery, e)) return false;
    size_t capacity = 12 + discovery.size + b->query_count * 31;
    uint8_t *bytes = malloc(capacity);
    if (!bytes) {
        qa_buffer_free(&discovery);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding retail browser continuation"); return false;
    }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, capacity, e);
    bool ok = qa_net_write_data(&w, "QAKB", 4) &&
        qa_net_write_u32(&w, (uint32_t)discovery.size) &&
        qa_net_write_data(&w, discovery.data, discovery.size) &&
        qa_net_write_u16(&w, (uint16_t)b->query_count);
    qa_buffer_free(&discovery);
    for (size_t i = 0; ok && i < b->query_count; ++i) {
        const frontend_kex_query *q = &b->queries[i];
        const qa_net_address *a = &q->address;
        ok = qa_net_write_u8(&w, (uint8_t)a->kind) && qa_net_write_u16(&w, a->port);
        if (ok && a->kind == QA_NET_IPV4) ok = qa_net_write_data(&w, a->host.ipv4, 4);
        else if (ok) ok = qa_net_write_data(&w, a->host.ipv6.bytes, 16) && qa_net_write_u32(&w, a->host.ipv6.scope);
        ok = ok && qa_net_write_u64(&w, q->sent_ns);
    }
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool frontend_kex_browser_restore(qa_bytes bytes, qa_server_browser *browser,
    const frontend_kex_browser_hooks *hooks, frontend_kex_browser **out, qa_error *e)
{
    if (!browser || !hooks || !hooks->send || !out || !bytes.data || bytes.size < 10 ||
        bytes.size > 48 + 256u * (10u + QA_KEX_DNS_NAME_BYTES + QA_KEX_DNS_FOLDED_BYTES +
            25u + QA_KEX_DNS_FOLDED_BYTES + 31u) || memcmp(bytes.data, "QAKB", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid retail browser continuation envelope"); return false;
    }
    frontend_kex_browser *b = calloc(1, sizeof(*b));
    if (!b) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring retail browser owner"); return false; }
    b->browser = browser;
    b->hooks = *hooks;
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = true;
    uint32_t extent = qa_net_read_u32(&r);
    qa_bytes discovery;
    const qa_kex_mdns_hooks mdns = frontend_kex_browser_mdns_hooks(b);
    ok = ok && qa_net_read_bytes(&r, extent, &discovery) &&
        qa_kex_mdns_owner_restore(discovery, &mdns, &b->discovery, e);
    b->query_count = qa_net_read_u16(&r);
    ok = ok && !r.failed && b->query_count <= 256;
    for (size_t i = 0; ok && i < b->query_count; ++i) {
        frontend_kex_query *q = &b->queries[i];
        qa_net_address *a = &q->address;
        a->kind = (qa_net_address_kind)qa_net_read_u8(&r);
        a->port = qa_net_read_u16(&r);
        if (a->kind == QA_NET_IPV4) ok = qa_net_read_data(&r, a->host.ipv4, 4);
        else if (a->kind == QA_NET_IPV6) {
            ok = qa_net_read_data(&r, a->host.ipv6.bytes, 16);
            a->host.ipv6.scope = qa_net_read_u32(&r);
        } else ok = false;
        q->sent_ns = qa_net_read_u64(&r);
        ok = ok && !r.failed;
    }
    if (!ok || !qa_net_reader_finish(&r) || !query_valid(b)) {
        frontend_kex_browser_destroy(b);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid retail browser query receipts");
        return false;
    }
    *out = b;
    return true;
}
