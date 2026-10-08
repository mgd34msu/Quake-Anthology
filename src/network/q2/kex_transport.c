#include "kex_transport_internal.h"

#include <stdlib.h>

static bool raw_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!o->raw || !o->raw_owned) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX raw dispatch has no physical socket owner"); return false;
    }
    return qa_net_transport_send(o->raw, to, bytes, e);
}

static bool raw_collect(void *context, uint64_t now, qa_net_transport_event *out, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!o->raw || !o->raw_owned) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX raw collection has no physical socket owner"); return false;
    }
    return qa_net_transport_collect(o->raw, now, out, e);
}

static bool raw_dispatch(void *context, const qa_net_transport_event *event,
    qa_net_datagram *out, bool *present, qa_error *e)
{
    qa_kex_transport *o = context;
    for (;;) {
        if (!qa_net_transport_dispatch(o->raw, event, out, present, e)) return false;
        bool recognized = false;
        if (*present && out->kind == QA_NET_POLL_PACKET && o->hooks.connectionless &&
            !o->hooks.connectionless(o->hooks.context, out, &recognized, e)) return false;
        if (!recognized) return true;
        *present = false;
        if (event) return true;
    }
}

static bool raw_maintenance(void *context, uint64_t now, qa_error *e)
{
    qa_kex_transport *o = context;
    return qa_net_transport_maintenance(o->raw, now, e);
}

static bool raw_ready(const void *context)
{
    const qa_kex_transport *o = context;
    return o->raw && o->raw_owned && qa_net_transport_ready(o->raw);
}

static void raw_close(void *context)
{
    qa_kex_transport *o = context;
    if (o->raw_owned) qa_net_transport_close(o->raw);
    o->raw = NULL;
    o->raw_owned = false;
    o->dispatch = NULL;
}

bool qa_kex_transport_valid(const qa_kex_transport *o)
{
    if (!o || o->entered || !o->raw_limit || o->raw_limit > 65535 ||
        !qa_kex_lan_valid(o->lobby) ||
        (!!o->raw != o->raw_owned) || (o->published && !o->raw_owned) ||
        (o->lobby->transport != o->dispatch) ||
        (o->raw && (qa_net_transport_limit(o->raw) != o->raw_limit ||
            !qa_net_address_equal(qa_net_transport_address(o->raw), &o->lobby->local_address, true)))) return false;
    bool host_dns = o->lobby->options.host && (o->lobby->local_address.kind == QA_NET_IPV4 ||
        o->lobby->local_address.kind == QA_NET_IPV6);
    return host_dns == !!o->discovery && (!o->discovery ||
        (qa_kex_mdns_owner_valid(o->discovery) && !o->discovery->closed &&
         o->discovery->advertised_port == o->lobby->local_address.port &&
         (!o->published || o->discovery->bound_published)));
}

static bool enter(qa_kex_transport *o, qa_error *e)
{
    if (!o || o->entered || !o->published || !o->raw_owned || !o->lobby) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX game requires its published physical transport"); return false;
    }
    o->entered = true;
    return true;
}

static bool game_send(void *context, const qa_net_address *to, qa_bytes bytes, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!enter(o, e)) return false;
    bool ok = qa_kex_lan_send(o->lobby, to, bytes, e);
    o->entered = false;
    return ok;
}

static bool game_collect(void *context, uint64_t now, qa_net_transport_event *out, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!enter(o, e)) return false;
    bool ok = qa_net_transport_collect(o->dispatch, now, out, e);
    if (ok && out->packet.kind != QA_NET_POLL_EMPTY) out->source <<= 2;
    else if (ok && o->discovery) {
        ok = qa_kex_mdns_owner_collect(o->discovery, now, out, e);
        if (ok && out->packet.kind != QA_NET_POLL_EMPTY) out->source = (out->source << 2) | 1u;
    }
    o->entered = false;
    return ok;
}

static bool game_dispatch(void *context, const qa_net_transport_event *event,
    qa_net_datagram *out, bool *present, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!enter(o, e)) return false;
    bool ok = true;
    *present = false;
    *out = (qa_net_datagram){0};
    qa_net_transport_event child;
    if (event) { child = *event; child.source >>= 2; }
    if (!event || (event->source & 3u) == 0) {
        qa_net_datagram packet;
        bool packet_present;
        ok = qa_net_transport_dispatch(o->dispatch, event ? &child : NULL, &packet, &packet_present, e);
        if (ok && packet_present) ok = qa_kex_lan_dispatch(o->lobby, &packet, e);
    }
    if (ok && o->discovery && (!event || (event->source & 3u) == 1u))
        ok = qa_kex_mdns_owner_dispatch(o->discovery, event ? &child : NULL, e);
    if (ok) {
        ok = qa_kex_lan_receive(o->lobby, out, e);
        *present = ok && out->kind != QA_NET_POLL_EMPTY;
    }
    o->entered = false;
    return ok;
}

static bool game_maintenance(void *context, uint64_t now, qa_error *e)
{
    qa_kex_transport *o = context;
    if (!enter(o, e)) return false;
    bool ok = (!o->discovery || qa_kex_mdns_owner_maintenance(o->discovery, now, e)) &&
        qa_kex_lan_tick(o->lobby, now, e);
    o->entered = false;
    return ok;
}

static bool game_ready(const void *context)
{
    const qa_kex_transport *o = context;
    return o && o->published && o->raw_owned && qa_kex_lan_ready(o->lobby);
}

void qa_kex_transport_destroy(qa_kex_transport *o)
{
    if (!o || o->entered) return;
    if (o->lobby) {
        if (o->published && o->raw_owned) qa_kex_lan_close(o->lobby);
        else {
            o->lobby->transport = NULL;
            qa_kex_lan_destroy_detached(o->lobby);
            qa_net_transport_close(o->dispatch);
        }
    } else qa_net_transport_close(o->dispatch);
    qa_kex_mdns_owner_destroy(o->discovery);
    free(o);
}

static void game_close(void *context) { qa_kex_transport_destroy(context); }

bool qa_kex_transport_finish(qa_kex_transport *o, qa_net_transport **out, qa_error *e)
{
    const qa_net_transport_ops raw_ops = {
        .send = raw_send, .collect = raw_collect, .dispatch = raw_dispatch,
        .maintenance = raw_maintenance, .ready = raw_ready, .close = raw_close
    };
    const qa_net_transport_ops game_ops = {
        .send = game_send, .collect = game_collect, .dispatch = game_dispatch,
        .maintenance = game_maintenance, .ready = game_ready, .close = game_close
    };
    qa_net_limits limits = {o->raw_limit, 256};
    if (!qa_net_transport_create(&o->lobby->local_address, limits, &raw_ops, o, &o->dispatch, e)) return false;
    if (!qa_kex_lan_bind(o->lobby, o->dispatch, e)) return false;
    limits.datagram_bytes = 65535;
    return qa_net_transport_create(&o->lobby->local_address, limits, &game_ops, o, out, e);
}

bool qa_kex_transport_open(qa_net_transport *raw, const qa_kex_lan_options *options,
    const qa_kex_transport_hooks *hooks, qa_net_transport **out, qa_kex_transport **control, qa_error *e)
{
    if (!raw || !options || !hooks || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing KEX transport construction owner"); return false;
    }
    qa_kex_transport *o = calloc(1, sizeof(*o));
    if (!o) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating KEX transport owner"); return false; }
    o->raw_limit = qa_net_transport_limit(raw);
    o->hooks = *hooks;
    if (!qa_kex_lan_open(raw, options, &o->lobby, e)) { free(o); return false; }
    o->lobby->transport = NULL;
    const qa_net_address *address = &o->lobby->local_address;
    if (options->host && (address->kind == QA_NET_IPV4 || address->kind == QA_NET_IPV6)) {
        const qa_kex_mdns_hooks discovery = {0};
        if (!address->port || !qa_kex_mdns_owner_open(address->port, &discovery, &o->discovery, e)) {
            if (!address->port) qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX host has no actual game socket port");
            qa_kex_transport_destroy(o); return false;
        }
    }
    if (!qa_kex_transport_finish(o, out, e)) { qa_kex_transport_destroy(o); return false; }
    o->raw = raw;
    o->raw_owned = o->published = true;
    if (control) *control = o;
    return true;
}

qa_kex_lan *qa_kex_transport_lobby(qa_kex_transport *o) { return o ? o->lobby : NULL; }

bool qa_kex_transport_udp_policy(const qa_kex_transport *o, qa_net_udp_policy *out, bool *present, qa_error *e)
{
    if (!o || !out || !present) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX native socket policy requires its actual transport owner"); return false;
    }
    *present = false;
    if (!o->raw || !o->raw_owned || !o->published) return true;
    return qa_net_udp_policy_read(o->raw, out, present, e);
}

bool qa_kex_transport_idle(const qa_kex_transport *o)
{
    return qa_kex_transport_valid(o) && qa_kex_lan_idle(o->lobby) &&
        (!o->discovery || qa_kex_mdns_owner_idle(o->discovery));
}
bool qa_kex_transport_set_maximum(qa_kex_transport *o, uint8_t maximum, qa_error *e)
{
    if (!enter(o, e)) return false;
    bool okay = qa_kex_lan_set_maximum(o->lobby, maximum, e);
    o->entered = false;
    return okay;
}

bool qa_kex_transport_send_connectionless(qa_kex_transport *o,
    const qa_net_address *to, qa_bytes bytes, qa_error *e)
{
    if (!o || !o->published || !o->raw_owned || !o->raw) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX connectionless send requires its actual published raw socket"); return false;
    }
    return qa_net_transport_send(o->raw, to, bytes, e);
}

bool qa_kex_transport_bind(qa_kex_transport *o, qa_net_transport *raw, qa_error *e)
{
    if (!qa_kex_transport_idle(o) || o->raw || o->published || !raw ||
        o->raw_limit != qa_net_transport_limit(raw) ||
        !qa_net_address_equal(&o->lobby->local_address, qa_net_transport_address(raw), true)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX cold binding requires the actual saved raw socket endpoint"); return false;
    }
    if (o->discovery && !qa_kex_mdns_owner_activate(o->discovery, e)) return false;
    o->raw = raw;
    o->raw_owned = true;
    return true;
}

bool qa_kex_transport_publish(qa_kex_transport *o, qa_error *e)
{
    if (!qa_kex_transport_idle(o) || !o->raw_owned) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX cold publication requires its actual bound idle socket"); return false;
    }
    if (o->discovery && !qa_kex_mdns_owner_publish(o->discovery, e)) return false;
    o->published = true;
    return true;
}
