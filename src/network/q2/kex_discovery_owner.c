#include "kex_discovery_owner_internal.h"
#include "qa/network_interfaces.h"

#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e, qa_status status, const char *message)
{
    qa_error_set(e, status, 0, "%s", message);
    return false;
}

bool qa_kex_mdns_text_equal(qa_buffer a, qa_buffer b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

static bool text_valid(qa_buffer text, size_t maximum)
{
    return text.data && text.size < maximum && text.data[text.size] == 0 &&
        qa_kex_text_valid((qa_bytes){text.data, text.size});
}

void qa_kex_mdns_owner_release_records(qa_kex_mdns_owner *o)
{
    for (size_t i = 0; i < o->endpoint_count; ++i) {
        qa_buffer_free(&o->endpoints[i].instance);
        qa_buffer_free(&o->endpoints[i].target);
    }
    for (size_t i = 0; i < o->address_count; ++i) qa_buffer_free(&o->addresses[i].target);
    o->endpoint_count = o->address_count = 0;
}

bool qa_kex_mdns_owner_valid(const qa_kex_mdns_owner *o)
{
    if (!o || o->endpoint_count > 256 || o->address_count > 256 ||
        (o->closed && o->socket) || (o->published && o->closed) ||
        (o->bound_published && !o->socket) ||
        (o->announce_pending && (!o->published || !o->advertised_port))) return false;
    for (size_t i = 0; i < o->endpoint_count; ++i) {
        const qa_kex_mdns_endpoint *p = &o->endpoints[i];
        if (!p->port || !p->instance.size || !text_valid(p->instance, QA_KEX_DNS_NAME_BYTES) ||
            !text_valid(p->target, QA_KEX_DNS_FOLDED_BYTES)) return false;
        for (size_t j = 0; j < i; ++j)
            if (qa_kex_mdns_text_equal(p->instance, o->endpoints[j].instance)) return false;
    }
    for (size_t i = 0; i < o->address_count; ++i) {
        const qa_kex_mdns_address *a = &o->addresses[i];
        if (!text_valid(a->target, QA_KEX_DNS_FOLDED_BYTES) ||
            (a->address.kind != QA_NET_IPV4 && a->address.kind != QA_NET_IPV6) ||
            a->address.port != QA_KEX_LAN_PORT) return false;
        for (size_t j = 0; j < i; ++j)
            if (qa_kex_mdns_text_equal(a->target, o->addresses[j].target)) return false;
    }
    return true;
}

static bool send_bytes(qa_kex_mdns_owner *o, qa_bytes bytes, qa_error *e)
{
    return qa_kex_mdns_native_send(o->socket, bytes, e);
}

static bool announce(qa_kex_mdns_owner *o, uint32_t ttl, qa_error *e)
{
    if (!o->advertised_port) return true;
    char hostname[64];
    if (!qa_kex_mdns_native_hostname(hostname, e)) return false;
    qa_net_interfaces *interfaces = NULL;
    if (!qa_net_interfaces_capture(&interfaces, e)) return false;
    size_t count = 0;
    for (size_t i = 0; i < qa_net_interfaces_count(interfaces); ++i) {
        const qa_net_interface *a = qa_net_interfaces_at(interfaces, i);
        if (a->up && a->running && !a->loopback && a->address.kind == QA_NET_IPV4) ++count;
    }
    if (count > 253) {
        qa_net_interfaces_destroy(interfaces);
        return fail(e, QA_ERROR_FORMAT, "KEX mDNS native interface count exceeds DNS packet limits");
    }
    qa_net_address *addresses = count ? calloc(count, sizeof(*addresses)) : NULL;
    if (count && !addresses) {
        qa_net_interfaces_destroy(interfaces);
        return fail(e, QA_ERROR_MEMORY, "Allocating KEX mDNS native addresses");
    }
    size_t at = 0;
    for (size_t i = 0; i < qa_net_interfaces_count(interfaces); ++i) {
        const qa_net_interface *a = qa_net_interfaces_at(interfaces, i);
        if (!a->up || !a->running || a->loopback || a->address.kind != QA_NET_IPV4) continue;
        addresses[at] = a->address;
        addresses[at].port = o->advertised_port;
        ++at;
    }
    qa_net_interfaces_destroy(interfaces);
    uint8_t bytes[65507];
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, sizeof(bytes), e);
    bool ok = qa_kex_mdns_announce(&w, hostname, o->advertised_port, addresses, count, ttl);
    free(addresses);
    return ok && send_bytes(o, (qa_bytes){bytes, qa_net_writer_size(&w)}, e);
}

bool qa_kex_mdns_owner_activate(qa_kex_mdns_owner *o, qa_error *e)
{
    if (!qa_kex_mdns_owner_valid(o) || o->entered || o->closed || o->socket)
        return fail(e, QA_ERROR_ARGUMENT, "KEX mDNS socket activation requires a detached idle owner");
    return qa_kex_mdns_native_open(&o->socket, e);
}

bool qa_kex_mdns_owner_publish(qa_kex_mdns_owner *o, qa_error *e)
{
    if (!qa_kex_mdns_owner_valid(o) || o->entered || o->closed || !o->socket)
        return fail(e, QA_ERROR_ARGUMENT, "KEX mDNS publication requires an active idle socket");
    if (o->bound_published) return true;
    o->published = true;
    o->bound_published = true;
    o->announce_pending = o->advertised_port != 0;
    return true;
}

bool qa_kex_mdns_owner_open(uint16_t port, const qa_kex_mdns_hooks *hooks,
                            qa_kex_mdns_owner **out, qa_error *e)
{
    if (!hooks || !out || (!port && !hooks->found))
        return fail(e, QA_ERROR_ARGUMENT, "Missing KEX mDNS browser callback or output");
    qa_kex_mdns_owner *o = calloc(1, sizeof(*o));
    if (!o) return fail(e, QA_ERROR_MEMORY, "Allocating KEX mDNS owner");
    o->advertised_port = port;
    o->hooks = *hooks;
    if (!qa_kex_mdns_owner_activate(o, e) || !qa_kex_mdns_owner_publish(o, e)) {
        qa_kex_mdns_owner_destroy(o);
        return false;
    }
    *out = o;
    return true;
}

bool qa_kex_mdns_owner_query(qa_kex_mdns_owner *o, qa_error *e)
{
    if (!qa_kex_mdns_owner_valid(o) || o->entered || o->closed || !o->bound_published || !o->socket)
        return fail(e, QA_ERROR_ARGUMENT, "KEX mDNS query requires a published idle socket");
    uint8_t bytes[512];
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, sizeof(bytes), e);
    return qa_kex_mdns_query(&w) && send_bytes(o, (qa_bytes){bytes, qa_net_writer_size(&w)}, e);
}

static void retain_records(qa_kex_mdns_owner *o, qa_kex_mdns_result *r)
{
    for (size_t i = 0; i < r->endpoint_count; ++i) {
        if (o->endpoint_count == 256) continue;
        size_t at = 0;
        while (at < o->endpoint_count && !qa_kex_mdns_text_equal(o->endpoints[at].instance, r->endpoints[i].instance)) ++at;
        if (at == o->endpoint_count) {
            if (at == 256) continue;
            ++o->endpoint_count;
        }
        qa_buffer_free(&o->endpoints[at].instance);
        qa_buffer_free(&o->endpoints[at].target);
        o->endpoints[at] = r->endpoints[i];
        r->endpoints[i] = (qa_kex_mdns_endpoint){0};
    }
    for (size_t i = 0; i < r->address_count; ++i) {
        if (o->address_count == 256) continue;
        size_t at = 0;
        while (at < o->address_count && !qa_kex_mdns_text_equal(o->addresses[at].target, r->addresses[i].target)) ++at;
        if (at == o->address_count) {
            if (at == 256) continue;
            ++o->address_count;
        }
        qa_buffer_free(&o->addresses[at].target);
        o->addresses[at] = r->addresses[i];
        r->addresses[i] = (qa_kex_mdns_address){0};
    }
}

bool qa_kex_mdns_owner_pump(qa_kex_mdns_owner *o, uint64_t now, qa_error *e)
{
    if (!qa_kex_mdns_owner_valid(o) || o->entered || o->closed || !o->bound_published || !o->socket)
        return fail(e, QA_ERROR_ARGUMENT, "KEX mDNS pump requires a published idle socket");
    qa_kex_mdns_result *result = calloc(1, sizeof(*result));
    if (!result) return fail(e, QA_ERROR_MEMORY, "Allocating KEX mDNS decode records");
    o->entered = true;
    bool ok = true;
    if (o->announce_pending) {
        ok = announce(o, 120, e);
        if (ok) o->announce_pending = false;
    }
    for (unsigned drained = 0; ok && drained < 4096; ++drained) {
        uint8_t bytes[9000];
        size_t n = 0;
        bool available, oversize;
        if (!qa_kex_mdns_native_receive(o->socket, bytes, sizeof(bytes), &n, &available, &oversize, e)) { ok = false; break; }
        if (!available) break;
        if (oversize) continue;
        qa_error malformed = {0};
        qa_kex_mdns_result_free(result);
        if (!qa_kex_mdns_read((qa_bytes){bytes, n}, result, &malformed)) {
            if (malformed.code == QA_ERROR_MEMORY) { if (e) *e = malformed; ok = false; break; }
            continue;
        }
        if (result->question && !announce(o, 120, e)) { ok = false; break; }
        retain_records(o, result);
        if (!o->hooks.found) continue;
        for (size_t i = 0; ok && i < o->endpoint_count; ++i) {
            const qa_kex_mdns_endpoint *endpoint = &o->endpoints[i];
            for (size_t j = 0; j < o->address_count; ++j) {
                if (!qa_kex_mdns_text_equal(endpoint->target, o->addresses[j].target)) continue;
                qa_net_address address = o->addresses[j].address;
                address.port = endpoint->port;
                ok = o->hooks.found(o->hooks.context, &address, now, e);
                break;
            }
        }
        if (!ok) break;
    }
    o->entered = false;
    qa_kex_mdns_result_free(result);
    free(result);
    return ok;
}

bool qa_kex_mdns_owner_idle(const qa_kex_mdns_owner *o)
{
    return qa_kex_mdns_owner_valid(o) && !o->entered;
}

bool qa_kex_mdns_owner_shutdown(qa_kex_mdns_owner *o, qa_error *e)
{
    if (!o) return true;
    if (!qa_kex_mdns_owner_valid(o) || o->entered)
        return fail(e, QA_ERROR_ARGUMENT, "KEX mDNS shutdown requires idle ownership");
    if (o->closed) return true;
    bool ok = !o->bound_published || !o->socket || announce(o, 0, e);
    if (o->socket) {
        qa_kex_mdns_socket *native = o->socket;
        o->socket = NULL;
        if (!qa_kex_mdns_native_close(native, ok ? e : NULL)) ok = false;
    }
    o->closed = true;
    o->published = false;
    o->announce_pending = false;
    o->bound_published = false;
    return ok;
}

void qa_kex_mdns_owner_destroy(qa_kex_mdns_owner *o)
{
    if (!o || o->entered) return;
    qa_error ignored = {0};
    qa_kex_mdns_owner_shutdown(o, &ignored);
    qa_kex_mdns_owner_release_records(o);
    free(o);
}
