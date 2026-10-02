#include "interfaces_internal.h"

#include <stdlib.h>
#include <string.h>

bool qa_net_interfaces_append(qa_net_interfaces *o, const qa_net_interface *record, qa_error *e)
{
    if (!record->name || o->count >= SIZE_MAX / sizeof(*o->entries)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Native interface extent is invalid"); return false;
    }
    size_t extent = strlen(record->name);
    if (extent == SIZE_MAX) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Native interface name extent overflow"); return false; }
    char *name = malloc(extent + 1);
    if (!name) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining native interface name"); return false; }
    qa_net_interface *entries = realloc(o->entries, (o->count + 1) * sizeof(*entries));
    if (!entries) { free(name); qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining native interface addresses"); return false; }
    memcpy(name, record->name, extent + 1);
    o->entries = entries;
    o->entries[o->count] = *record;
    o->entries[o->count].name = name;
    ++o->count;
    return true;
}

bool qa_net_interfaces_valid(const qa_net_interfaces *o)
{
    if (!o || (o->count && !o->entries) || o->count > SIZE_MAX / sizeof(*o->entries)) return false;
    for (size_t i = 0; i < o->count; ++i) {
        const qa_net_interface *a = &o->entries[i];
        if (!a->name || !*a->name || a->address.port || a->netmask.port ||
            (a->address.kind != QA_NET_IPV4 && a->address.kind != QA_NET_IPV6) ||
            (a->has_netmask && a->netmask.kind != a->address.kind)) return false;
    }
    return true;
}

void qa_net_interfaces_destroy(qa_net_interfaces *o)
{
    if (!o) return;
    for (size_t i = 0; i < o->count; ++i) free((char *)o->entries[i].name);
    free(o->entries);
    free(o);
}

size_t qa_net_interfaces_count(const qa_net_interfaces *o) { return o ? o->count : 0; }
const qa_net_interface *qa_net_interfaces_at(const qa_net_interfaces *o, size_t index)
{
    return o && index < o->count ? &o->entries[index] : NULL;
}

static bool wildcard(const qa_net_address *a)
{
    size_t bytes = a->kind == QA_NET_IPV4 ? 4 : 16;
    const uint8_t *host = a->kind == QA_NET_IPV4 ? a->host.ipv4 : a->host.ipv6.bytes;
    for (size_t i = 0; i < bytes; ++i) if (host[i]) return false;
    return a->kind != QA_NET_IPV6 || a->host.ipv6.scope == 0;
}

bool qa_net_interfaces_local(const qa_net_interfaces *o, const qa_net_address *bound,
                              bool ipv6_only, const qa_net_address *remote)
{
    if (!o || !o->native_enumerated || !bound || !remote ||
        (bound->kind != QA_NET_IPV4 && bound->kind != QA_NET_IPV6) ||
        (remote->kind != QA_NET_IPV4 && remote->kind != QA_NET_IPV6)) return false;
    for (size_t i = 0; i < o->count; ++i) {
        const qa_net_interface *a = &o->entries[i];
        if (!a->up || !a->running || a->address.kind != remote->kind) continue;
        bool bound_family = bound->kind == a->address.kind ||
            (bound->kind == QA_NET_IPV6 && !ipv6_only && wildcard(bound) && a->address.kind == QA_NET_IPV4);
        if (!bound_family || (!wildcard(bound) && !qa_net_address_equal(bound, &a->address, false))) continue;
        if (remote->kind == QA_NET_IPV6 && remote->host.ipv6.scope &&
            remote->host.ipv6.scope != a->address.host.ipv6.scope && remote->host.ipv6.scope != a->index) continue;
        if (qa_net_address_equal(remote, &a->address, false)) return true;
        if (!a->has_netmask) continue;
        const uint8_t *host = remote->kind == QA_NET_IPV4 ? a->address.host.ipv4 : a->address.host.ipv6.bytes;
        const uint8_t *other = remote->kind == QA_NET_IPV4 ? remote->host.ipv4 : remote->host.ipv6.bytes;
        const uint8_t *mask = remote->kind == QA_NET_IPV4 ? a->netmask.host.ipv4 : a->netmask.host.ipv6.bytes;
        size_t bytes = remote->kind == QA_NET_IPV4 ? 4 : 16;
        bool matches = true;
        for (size_t j = 0; j < bytes; ++j) if ((host[j] ^ other[j]) & mask[j]) { matches = false; break; }
        if (matches) return true;
    }
    return false;
}
