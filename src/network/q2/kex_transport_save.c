#include "kex_transport_internal.h"

#include <stdlib.h>
#include <string.h>

bool qa_kex_transport_checkpoint(const qa_kex_transport *o, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_kex_transport_idle(o)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX continuation requires its complete idle owners"); return false;
    }
    qa_buffer lan = {0}, mdns = {0};
    bool ok = qa_kex_lan_checkpoint(o->lobby, &lan, e) &&
        (!o->discovery || qa_kex_mdns_owner_checkpoint(o->discovery, &mdns, e));
    size_t capacity = 20;
    if (ok && (lan.size > UINT32_MAX || mdns.size > UINT32_MAX ||
        lan.size > SIZE_MAX - capacity || mdns.size > SIZE_MAX - capacity - lan.size)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "KEX continuation envelope extent overflow"); ok = false;
    }
    if (ok) capacity += lan.size + mdns.size;
    uint8_t *bytes = ok ? malloc(capacity) : NULL;
    if (ok && !bytes) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding KEX transport continuation"); ok = false; }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, ok ? capacity : 0, e);
    ok = ok && qa_net_write_data(&w, "QAKT", 4) &&
        qa_net_write_u32(&w, (uint32_t)o->raw_limit) && qa_net_write_u32(&w, (uint32_t)lan.size) &&
        qa_net_write_data(&w, lan.data, lan.size) && qa_net_write_u32(&w, (uint32_t)mdns.size) &&
        qa_net_write_data(&w, mdns.data, mdns.size);
    qa_buffer_free(&lan);
    qa_buffer_free(&mdns);
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool qa_kex_transport_restore(qa_bytes bytes, const qa_kex_transport_hooks *hooks,
    qa_net_transport **out, qa_kex_transport **control, qa_error *e)
{
    if (!hooks || !out || !bytes.data || bytes.size < 16 || bytes.size > SIZE_MAX / 8 || memcmp(bytes.data, "QAKT", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX transport continuation envelope"); return false;
    }
    qa_kex_transport *o = calloc(1, sizeof(*o));
    if (!o) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX transport owner"); return false; }
    o->hooks = *hooks;
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = true;
    o->raw_limit = qa_net_read_u32(&r);
    uint32_t extent = qa_net_read_u32(&r);
    qa_bytes nested;
    ok = ok && o->raw_limit && o->raw_limit <= 65535 && qa_net_read_bytes(&r, extent, &nested) &&
        qa_kex_lan_restore(nested, &o->lobby, e);
    extent = qa_net_read_u32(&r);
    if (ok && extent) {
        const qa_kex_mdns_hooks mdns = {0};
        ok = qa_net_read_bytes(&r, extent, &nested) && qa_kex_mdns_owner_restore(nested, &mdns, &o->discovery, e);
    }
    if (!ok || !qa_net_reader_finish(&r) || !qa_kex_transport_valid(o)) {
        qa_kex_transport_destroy(o);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX transport retained owner joins");
        return false;
    }
    if (!qa_kex_transport_finish(o, out, e)) { qa_kex_transport_destroy(o); return false; }
    if (control) *control = o;
    return true;
}
