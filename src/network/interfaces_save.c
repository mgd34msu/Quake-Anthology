#include "interfaces_internal.h"

#include <stdlib.h>
#include <string.h>

static bool address_write(qa_net_writer *w, const qa_net_address *a)
{
    if (a->kind == QA_NET_IPV4) return qa_net_write_data(w, a->host.ipv4, 4);
    return qa_net_write_data(w, a->host.ipv6.bytes, 16) && qa_net_write_u32(w, a->host.ipv6.scope);
}

static bool address_read(qa_net_reader *r, qa_net_address_kind kind, qa_net_address *a)
{
    a->kind = kind;
    if (kind == QA_NET_IPV4) return qa_net_read_data(r, a->host.ipv4, 4);
    if (kind != QA_NET_IPV6) return false;
    bool ok = qa_net_read_data(r, a->host.ipv6.bytes, 16);
    a->host.ipv6.scope = qa_net_read_u32(r);
    return ok && !r->failed;
}

bool qa_net_interfaces_checkpoint(const qa_net_interfaces *o, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_net_interfaces_valid(o)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid native interface continuation owner"); return false;
    }
    size_t capacity = 12;
    for (size_t i = 0; i < o->count; ++i) {
        const qa_net_interface *a = &o->entries[i];
        size_t name = strlen(a->name);
        size_t addresses = (a->address.kind == QA_NET_IPV4 ? 4u : 20u) * (a->has_netmask ? 2u : 1u);
        if (name > UINT32_MAX || name > SIZE_MAX - 10 - addresses || capacity > SIZE_MAX - name - 10 - addresses) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Native interface continuation extent overflow"); return false;
        }
        capacity += name + 10 + addresses;
    }
    uint8_t *bytes = malloc(capacity);
    if (!bytes) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding native interface continuation"); return false; }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, capacity, e);
    bool ok = qa_net_write_data(&w, "QAIF", 4) && qa_net_write_u64(&w, o->count);
    for (size_t i = 0; ok && i < o->count; ++i) {
        const qa_net_interface *a = &o->entries[i];
        size_t name = strlen(a->name);
        uint8_t flags = (uint8_t)((a->up ? 1u : 0u) | (a->loopback ? 2u : 0u) |
            (a->has_netmask ? 4u : 0u) | (a->running ? 8u : 0u));
        ok = qa_net_write_u32(&w, (uint32_t)name) && qa_net_write_data(&w, a->name, name) &&
            qa_net_write_u8(&w, flags) && qa_net_write_u32(&w, a->index) &&
            qa_net_write_u8(&w, (uint8_t)a->address.kind) && address_write(&w, &a->address) &&
            (!a->has_netmask || address_write(&w, &a->netmask));
    }
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool qa_net_interfaces_restore(qa_bytes bytes, qa_net_interfaces **out, qa_error *e)
{
    if (!out || !bytes.data || bytes.size < 12 || memcmp(bytes.data, "QAIF", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid native interface continuation envelope"); return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = true;
    uint64_t count = qa_net_read_u64(&r);
    ok = ok && !r.failed && count <= SIZE_MAX / sizeof(qa_net_interface) &&
        count <= qa_net_reader_remaining(&r) / 15;
    if (!ok) { qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid native interface record extent"); return false; }
    qa_net_interfaces *o = calloc(1, sizeof(*o));
    if (!o) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring native interface continuation"); return false; }
    for (uint64_t i = 0; ok && i < count; ++i) {
        uint32_t extent = qa_net_read_u32(&r);
        qa_bytes name;
        ok = extent && (uint64_t)extent + 1 <= SIZE_MAX && qa_net_read_bytes(&r, extent, &name) &&
            !memchr(name.data, 0, name.size);
        if (!ok) break;
        char *owned = malloc((size_t)extent + 1);
        if (!owned) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring native interface name"); ok = false; break; }
        memcpy(owned, name.data, extent);
        owned[extent] = 0;
        uint8_t flags = qa_net_read_u8(&r);
        qa_net_interface a = {.name = owned, .up = (flags & 1u) != 0,
            .loopback = (flags & 2u) != 0, .has_netmask = (flags & 4u) != 0, .running = (flags & 8u) != 0};
        a.index = qa_net_read_u32(&r);
        qa_net_address_kind kind = (qa_net_address_kind)qa_net_read_u8(&r);
        ok = flags <= 15 && address_read(&r, kind, &a.address) &&
            (!a.has_netmask || address_read(&r, kind, &a.netmask)) && qa_net_interfaces_append(o, &a, e);
        free(owned);
    }
    if (!ok || !qa_net_reader_finish(&r) || !qa_net_interfaces_valid(o)) {
        qa_net_interfaces_destroy(o);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid native interface continuation records");
        return false;
    }
    *out = o;
    return true;
}
