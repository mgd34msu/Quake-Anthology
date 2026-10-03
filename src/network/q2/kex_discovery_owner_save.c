#include "kex_discovery_owner_internal.h"
#include "qa/text.h"

#include <stdlib.h>
#include <string.h>

static bool text_write(qa_net_writer *w, qa_buffer text)
{
    return qa_net_write_u32(w, (uint32_t)text.size) && qa_net_write_data(w, text.data, text.size);
}

static bool text_read(qa_net_reader *r, qa_buffer *out, size_t maximum, bool folded, qa_error *e)
{
    uint32_t size = qa_net_read_u32(r);
    if (r->failed || size >= maximum || size > qa_net_reader_remaining(r))
        return qa_net_reader_fail(r, "Invalid retained DNS name extent");
    uint8_t *data = malloc((size_t)size + 1);
    if (!data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring retained DNS name"); return false; }
    if (!qa_net_read_data(r, data, size) || !qa_kex_text_valid((qa_bytes){data, size})) {
        free(data); return qa_net_reader_fail(r, "Invalid retained DNS name UTF-8");
    }
    data[size] = 0;
    if (folded) {
        qa_buffer lower = {0};
        if (!qa_utf8_lower((qa_bytes){data, size}, &lower, e)) { free(data); return false; }
        bool same = lower.size == size && (!size || !memcmp(lower.data, data, size));
        qa_buffer_free(&lower);
        if (!same) { free(data); return qa_net_reader_fail(r, "Retained DNS target does not preserve its actual Unicode key"); }
    }
    *out = (qa_buffer){data, size};
    return true;
}

bool qa_kex_mdns_owner_checkpoint(const qa_kex_mdns_owner *o, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_kex_mdns_owner_idle(o)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX mDNS continuation requires an idle owner"); return false;
    }
    size_t capacity = 32;
    for (size_t i = 0; i < o->endpoint_count; ++i)
        capacity += 10 + o->endpoints[i].instance.size + o->endpoints[i].target.size;
    for (size_t i = 0; i < o->address_count; ++i) capacity += 25 + o->addresses[i].target.size;
    uint8_t *bytes = malloc(capacity);
    if (!bytes) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding KEX mDNS continuation"); return false; }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, capacity, e);
    bool ok = qa_net_write_data(&w, "QAMD", 4) &&
        qa_net_write_u16(&w, o->advertised_port) && qa_net_write_u8(&w, o->closed) &&
        qa_net_write_u8(&w, o->published) && qa_net_write_u8(&w, o->announce_pending) &&
        qa_net_write_u8(&w, o->found_pending) && qa_net_write_u16(&w, (uint16_t)o->found_cursor) &&
        qa_net_write_u16(&w, (uint16_t)o->endpoint_count) && qa_net_write_u16(&w, (uint16_t)o->address_count);
    for (size_t i = 0; ok && i < o->endpoint_count; ++i) {
        const qa_kex_mdns_endpoint *p = &o->endpoints[i];
        ok = text_write(&w, p->instance) && text_write(&w, p->target) && qa_net_write_u16(&w, p->port);
    }
    for (size_t i = 0; ok && i < o->address_count; ++i) {
        const qa_kex_mdns_address *a = &o->addresses[i];
        ok = text_write(&w, a->target) && qa_net_write_u8(&w, (uint8_t)a->address.kind);
        if (ok && a->address.kind == QA_NET_IPV4) ok = qa_net_write_data(&w, a->address.host.ipv4, 4);
        else if (ok) ok = qa_net_write_data(&w, a->address.host.ipv6.bytes, 16) &&
            qa_net_write_u32(&w, a->address.host.ipv6.scope);
    }
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool qa_kex_mdns_owner_restore(qa_bytes bytes, const qa_kex_mdns_hooks *hooks,
    qa_kex_mdns_owner **out, qa_error *e)
{
    const size_t maximum = 32 + 256u * (10u + QA_KEX_DNS_NAME_BYTES + QA_KEX_DNS_FOLDED_BYTES +
        25u + QA_KEX_DNS_FOLDED_BYTES);
    if (!hooks || !out || !bytes.data || bytes.size < 16 || bytes.size > maximum || memcmp(bytes.data, "QAMD", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX mDNS continuation envelope"); return false;
    }
    qa_kex_mdns_owner *o = calloc(1, sizeof(*o));
    if (!o) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX mDNS continuation"); return false; }
    o->hooks = *hooks;
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = true;
    o->advertised_port = qa_net_read_u16(&r);
    uint8_t closed = qa_net_read_u8(&r), published = qa_net_read_u8(&r), pending = qa_net_read_u8(&r);
    o->closed = closed != 0;
    o->published = published != 0;
    o->announce_pending = pending != 0;
    uint8_t found_pending=qa_net_read_u8(&r);
    o->found_pending=found_pending!=0;
    o->found_cursor=qa_net_read_u16(&r);
    uint16_t endpoints = qa_net_read_u16(&r), addresses = qa_net_read_u16(&r);
    ok = ok && !r.failed && closed <= 1 && published <= 1 && pending <= 1 && found_pending<=1 && endpoints <= 256 &&
        addresses <= 256 && (o->advertised_port || hooks->found);
    for (size_t i = 0; ok && i < endpoints; ++i) {
        qa_kex_mdns_endpoint *p = &o->endpoints[o->endpoint_count++];
        ok = text_read(&r, &p->instance, QA_KEX_DNS_NAME_BYTES, false, e) &&
            text_read(&r, &p->target, QA_KEX_DNS_FOLDED_BYTES, true, e);
        p->port = qa_net_read_u16(&r);
        ok = ok && !r.failed;
    }
    for (size_t i = 0; ok && i < addresses; ++i) {
        qa_kex_mdns_address *a = &o->addresses[o->address_count++];
        ok = text_read(&r, &a->target, QA_KEX_DNS_FOLDED_BYTES, true, e);
        a->address.kind = (qa_net_address_kind)qa_net_read_u8(&r);
        a->address.port = QA_KEX_LAN_PORT;
        if (a->address.kind == QA_NET_IPV4) ok = ok && qa_net_read_data(&r, a->address.host.ipv4, 4);
        else if (a->address.kind == QA_NET_IPV6) {
            ok = ok && qa_net_read_data(&r, a->address.host.ipv6.bytes, 16);
            a->address.host.ipv6.scope = qa_net_read_u32(&r);
        } else ok = false;
        ok = ok && !r.failed;
    }
    if (!ok || !qa_net_reader_finish(&r) || !qa_kex_mdns_owner_valid(o)) {
        qa_kex_mdns_owner_release_records(o);
        free(o);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX mDNS retained records");
        return false;
    }
    *out = o;
    return true;
}
