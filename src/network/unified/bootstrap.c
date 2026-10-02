#include "bootstrap_internal.h"
#include "session_internal.h"

#include <stdlib.h>
#include <string.h>

static bool token_equal(qa_unified_token a, qa_unified_token b)
{
    unsigned mismatch = 0;
    for (size_t i = 0; i < 16; ++i) mismatch |= (unsigned)(a.bytes[i] ^ b.bytes[i]);
    return mismatch == 0;
}
static bool address_valid(const qa_net_address *a)
{
    char text[256];
    return qa_net_address_format(a, text, sizeof(text), NULL) && (a->kind == QA_NET_LOOPBACK || a->port);
}

bool qa_unified_bootstrap_idle(const qa_unified_bootstrap *b)
{
    return b && !b->entered && qa_network_callbacks_idle(b->runtime);
}
bool qa_unified_bootstrap_closed(const qa_unified_bootstrap *b) { return !b || b->closed; }

bool qa_unified_bootstrap_valid(const qa_unified_bootstrap *b, bool bound, qa_error *e)
{
    if (!b || !b->options.hooks.attach || !b->options.max_clients || b->options.max_clients > 256 ||
        b->pending_count > 256 || b->peer_count > 264 || (!b->options.server &&
        (!address_valid(&b->options.remote) || b->pending_count || b->peer_count > 1)) ||
        (b->sent && !b->started) || (b->attach_pending && (!b->has_token || b->options.server)) ||
        (b->closed && (b->pending_count || b->peer_count || b->attach_pending)) ||
        (b->started && (b->handshake_started > b->now_ns || (b->sent && b->last_handshake > b->now_ns))))
        return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Invalid retained production handshake owner");
    for (size_t i = 0; i < b->pending_count; ++i) {
        if (!address_valid(&b->pending[i].address) || b->pending[i].created > b->now_ns)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Invalid retained production challenge");
        for (size_t j = 0; j < i; ++j)
            if (qa_net_address_equal(&b->pending[i].address, &b->pending[j].address, true))
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Duplicate production challenge endpoint");
    }
    for (size_t i = 0; i < b->peer_count; ++i) {
        const qa_unified_peer_receipt *peer = b->peers + i;
        if (!address_valid(&peer->address) || !peer->id.owner || !peer->id.generation)
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Invalid retained production peer receipt");
        for (size_t j = 0; j < i; ++j)
            if (qa_net_client_id_equal(peer->id, b->peers[j].id) || token_equal(peer->token, b->peers[j].token))
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Duplicate production handshake peer identity");
        if (!b->options.server && (!b->has_token || !token_equal(peer->nonce, b->nonce) ||
            !token_equal(peer->token, b->token) || !qa_net_address_equal(&peer->address, &b->options.remote, true)))
            return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production client handshake differs from its actual retained challenge");
        if (bound) {
            const qa_net_client *client = qa_net_connections_get(qa_network_connections(b->runtime), peer->id);
            qa_unified_session *installed = NULL;
            if (!client || !peer->session || peer->session->runtime != b->runtime ||
                !qa_unified_session_find(b->runtime, peer->id, &installed, e) || installed != peer->session ||
                !qa_net_client_id_equal(peer->session->id, peer->id) || peer->session->server != b->options.server ||
                !token_equal(peer->session->token, peer->token) || !qa_net_address_equal(&client->endpoint, &peer->address, true))
                return qa_unified_session_fail(e, QA_ERROR_FORMAT, "Production handshake receipt lacks its actual restored session");
        }
    }
    return true;
}

bool qa_unified_bootstrap_create(qa_network_runtime *runtime, const qa_unified_bootstrap_options *options,
    qa_unified_bootstrap **out, qa_error *e)
{
    if (!runtime || !options || !out || !qa_network_callbacks_idle(runtime))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake constructor requires its idle runtime");
    qa_unified_bootstrap *b = calloc(1, sizeof(*b));
    if (!b) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Allocating production handshake owner");
    b->runtime = runtime; b->options = *options;
    if (!qa_unified_bootstrap_valid(b, false, e) ||
        (!options->server && !qa_unified_token_random(&b->nonce, e))) { free(b); return false; }
    *out = b; return true;
}
void qa_unified_bootstrap_destroy(qa_unified_bootstrap *b) { if (b && !b->entered) free(b); }

static bool send_handshake(qa_unified_bootstrap *b, const qa_net_address *address,
    qa_unified_handshake_kind kind, qa_unified_token nonce, qa_unified_token token, qa_error *e)
{
    qa_unified_handshake message = {kind, nonce, token};
    qa_buffer wire = {0};
    if (!qa_unified_handshake_write(&message, &wire, e)) return false;
    bool ok = qa_network_send_address(b->runtime, address, (qa_bytes){wire.data, wire.size}, e);
    qa_buffer_free(&wire); return ok;
}

static void expire_pending(qa_unified_bootstrap *b, uint64_t now)
{
    for (size_t i = 0; i < b->pending_count; ) {
        if (now >= b->pending[i].created && now - b->pending[i].created > UINT64_C(10000000000)) {
            memmove(b->pending + i, b->pending + i + 1, (--b->pending_count - i) * sizeof(b->pending[0]));
        } else ++i;
    }
}

static bool server_receive(qa_unified_bootstrap *b, const qa_net_datagram *packet,
    const qa_unified_handshake *h, qa_error *e)
{
    if (h->kind == QA_UNIFIED_CHALLENGE) return true;
    expire_pending(b, packet->received_ns);
    size_t at = 0;
    while (at < b->pending_count && !qa_net_address_equal(&b->pending[at].address, &packet->from, true)) ++at;
    if (h->kind == QA_UNIFIED_HELLO) {
        if (at == b->pending_count && b->pending_count == 256) return true;
        if (at == b->pending_count || !token_equal(b->pending[at].nonce, h->nonce)) {
            qa_unified_pending pending = {.address = packet->from, .nonce = h->nonce, .created = packet->received_ns};
            if (!qa_unified_token_random(&pending.token, e)) return false;
            b->pending[at] = pending;
            if (at == b->pending_count) ++b->pending_count;
        }
        return send_handshake(b, &packet->from, QA_UNIFIED_CHALLENGE, b->pending[at].nonce, b->pending[at].token, e);
    }
    for (size_t i = 0; i < b->peer_count; ++i) {
        qa_unified_peer_receipt *peer = b->peers + i;
        if (token_equal(peer->token, h->token) && token_equal(peer->nonce, h->nonce) &&
            qa_net_address_equal(&peer->address, &packet->from, true)) { peer->flush = true; return true; }
    }
    if (at < b->pending_count && b->peer_count < (size_t)b->options.max_clients + 8 &&
        token_equal(b->pending[at].token, h->token) && token_equal(b->pending[at].nonce, h->nonce)) b->pending[at].connect = true;
    return true;
}

static bool client_receive(qa_unified_bootstrap *b, const qa_net_datagram *packet,
    const qa_unified_handshake *h, qa_error *e)
{
    bool attached = b->peer_count && qa_net_connections_get(qa_network_connections(b->runtime), b->peers[0].id);
    if (h->kind != QA_UNIFIED_CHALLENGE || !qa_net_address_equal(&packet->from, &b->options.remote, true) ||
        !token_equal(h->nonce, b->nonce) || (attached && qa_unified_session_epoch(b->peers[0].session)) ||
        (b->has_token && !token_equal(b->token, h->token))) return true;
    b->token = h->token; b->has_token = true; b->attach_pending = !attached;
    b->last_handshake = packet->received_ns; b->sent = true;
    return send_handshake(b, &b->options.remote, QA_UNIFIED_CONNECT, b->nonce, b->token, e);
}

bool qa_unified_bootstrap_receive(qa_unified_bootstrap *b, const qa_net_datagram *packet,
    bool *recognized, qa_error *e)
{
    if (!b || !packet || !recognized || b->entered || packet->received_ns < b->now_ns)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake receive is recursive or moves its clock backwards");
    *recognized = false;
    qa_unified_handshake h;
    if (!qa_unified_handshake_read(packet->payload, &h, NULL)) return true;
    *recognized = true;
    if (b->closed) return true;
    b->entered = true; b->now_ns = packet->received_ns;
    if (!b->started) { b->started = true; b->handshake_started = b->now_ns; }
    bool ok = b->options.server ? server_receive(b, packet, &h, e) : client_receive(b, packet, &h, e);
    b->entered = false; return ok;
}

static void prune_peers(qa_unified_bootstrap *b)
{
    for (size_t i = 0; i < b->peer_count; ) {
        if (!qa_net_connections_get(qa_network_connections(b->runtime), b->peers[i].id)) {
            if (!b->options.server) { b->closed = true; b->attach_pending = false; }
            memmove(b->peers + i, b->peers + i + 1, (--b->peer_count - i) * sizeof(b->peers[0]));
        } else ++i;
    }
}

static bool attach(qa_unified_bootstrap *b, const qa_unified_pending *pending, qa_error *e)
{
    qa_unified_peer_receipt peer = {.address = pending->address, .nonce = pending->nonce, .token = pending->token};
    if (!b->options.hooks.attach(b->options.hooks.context, b->runtime, &pending->address,
        pending->token, pending->nonce, b->options.server, b->now_ns, &peer.id, &peer.session, e)) return false;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(b->runtime), peer.id);
    qa_unified_session *installed = NULL;
    if (!client || !peer.session || peer.session->runtime != b->runtime ||
        !qa_unified_session_find(b->runtime, peer.id, &installed, e) || installed != peer.session ||
        !qa_net_client_id_equal(peer.session->id, peer.id) || peer.session->server != b->options.server ||
        !token_equal(peer.session->token, peer.token) || !qa_net_address_equal(&client->endpoint, &peer.address, true) ||
        (b->options.server && !qa_unified_session_epoch(peer.session))) {
        if (client) qa_network_detach(b->runtime, peer.id, "invalid production handshake attachment", NULL);
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake callback did not attach its real offered session");
    }
    b->peers[b->peer_count++] = peer;
    return true;
}

bool qa_unified_bootstrap_process(qa_unified_bootstrap *b, uint64_t now, bool *waiting, qa_error *e)
{
    if (!waiting || !qa_unified_bootstrap_idle(b) || now < b->now_ns)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake processing requires its idle monotonic runtime");
    *waiting = false; b->entered = true; b->now_ns = now;
    if (!b->started) { b->started = true; b->handshake_started = now; }
    prune_peers(b); expire_pending(b, now);
    if (b->closed) { b->entered = false; return true; }
    bool ok = true;
    if (b->options.server) {
        for (size_t i = 0; ok && i < b->pending_count; ) {
            if (!b->pending[i].connect || b->peer_count >= (size_t)b->options.max_clients + 8) { ++i; continue; }
            ok = attach(b, b->pending + i, e);
            if (ok) memmove(b->pending + i, b->pending + i + 1, (--b->pending_count - i) * sizeof(b->pending[0]));
        }
    } else {
        if (b->attach_pending && !b->peer_count) {
            qa_unified_pending pending = {.address = b->options.remote, .nonce = b->nonce, .token = b->token};
            ok = attach(b, &pending, e);
            if (ok) b->attach_pending = false;
        }
        const qa_net_client *client = b->peer_count ?
            qa_net_connections_get(qa_network_connections(b->runtime), b->peers[0].id) : NULL;
        uint64_t received = client ? client->received_ns : b->handshake_started;
        if (ok && !client && now >= received && now - received > UINT64_C(120000000000)) {
            b->closed = true; b->attach_pending = false;
            ok = qa_unified_session_fail(e, QA_ERROR_IO, "Production client connection timed out");
        }
    }
    for (size_t i = 0; ok && i < b->peer_count; ) {
        qa_unified_peer_receipt *peer = b->peers + i;
        bool source_waiting = false;
        ok = qa_unified_session_process(peer->session, &source_waiting, e);
        if (!ok) break;
        *waiting = *waiting || source_waiting;
        if (!qa_unified_session_disconnected(peer->session)) {
            if (!source_waiting && peer->flush) {
                ok = qa_unified_session_flush(peer->session, now, e);
                if (ok) peer->flush = false;
            }
            ++i; continue;
        }
        ok = qa_network_detach(b->runtime, peer->id, "production session closed", e);
        if (ok) {
            if (!b->options.server) { b->closed = true; b->attach_pending = false; }
            memmove(b->peers + i, b->peers + i + 1, (--b->peer_count - i) * sizeof(b->peers[0]));
        }
    }
    if (ok && !b->options.server && !b->closed) {
        bool pending_offer = !b->peer_count || !qa_unified_session_epoch(b->peers[0].session);
        if (!*waiting && pending_offer && (!b->sent || now - b->last_handshake >= UINT64_C(1000000000))) {
            ok = send_handshake(b, &b->options.remote, b->has_token ? QA_UNIFIED_CONNECT : QA_UNIFIED_HELLO,
                b->nonce, b->token, e);
            if (ok) { b->last_handshake = now; b->sent = true; }
        }
        *waiting = *waiting || (ok && pending_offer);
    }
    b->entered = false; return ok;
}

bool qa_unified_bootstrap_client(const qa_unified_bootstrap *b, qa_net_client_id *id, qa_unified_session **out)
{
    qa_unified_session *installed = NULL;
    if (!b || b->closed || b->options.server || !b->peer_count || !id || !out ||
        !qa_unified_session_find(b->runtime, b->peers[0].id, &installed, NULL) || installed != b->peers[0].session) return false;
    *id = b->peers[0].id; *out = b->peers[0].session; return true;
}

bool qa_unified_bootstrap_current_limits(qa_unified_bootstrap *b, uint32_t max_clients, qa_error *e)
{
    if (!qa_unified_bootstrap_idle(b) || !b->options.server || !max_clients || max_clients > 256)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production capacity update lacks its idle actual server");
    b->options.max_clients = max_clients; return true;
}
