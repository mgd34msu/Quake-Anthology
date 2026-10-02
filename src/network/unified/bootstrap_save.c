#include "bootstrap_internal.h"
#include "session_internal.h"

#include <stdlib.h>
#include <string.h>

static bool flag(qa_net_reader *r, bool *out)
{
    uint8_t value = qa_net_read_u8(r);
    if (value > 1) return qa_net_reader_fail(r, "Invalid production handshake flag");
    *out = value != 0; return !r->failed;
}

static bool address_write(qa_net_writer *w, const qa_net_address *a)
{
    bool ok = qa_net_write_u8(w, (uint8_t)a->kind) && qa_net_write_u16(w, a->port);
    if (a->kind == QA_NET_IPV4) return ok && qa_net_write_data(w, a->host.ipv4, 4);
    if (a->kind == QA_NET_IPV6) return ok && qa_net_write_data(w, a->host.ipv6.bytes, 16) && qa_net_write_u32(w, a->host.ipv6.scope);
    if (a->kind == QA_NET_LOOPBACK) return ok && qa_net_write_string(w, a->host.loopback);
    return qa_net_writer_fail(w, "Invalid production handshake address");
}

static bool address_read(qa_net_reader *r, qa_net_address *a)
{
    a->kind = (qa_net_address_kind)qa_net_read_u8(r); a->port = qa_net_read_u16(r);
    if (a->kind == QA_NET_IPV4) return qa_net_read_data(r, a->host.ipv4, 4);
    if (a->kind == QA_NET_IPV6) {
        bool ok = qa_net_read_data(r, a->host.ipv6.bytes, 16); a->host.ipv6.scope = qa_net_read_u32(r);
        return ok && !r->failed;
    }
    if (a->kind == QA_NET_LOOPBACK) return qa_net_read_string(r, a->host.loopback, sizeof(a->host.loopback));
    return qa_net_reader_fail(r, "Invalid production handshake address family");
}

bool qa_unified_bootstrap_checkpoint(const qa_unified_bootstrap *b, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_unified_bootstrap_idle(b) || !qa_unified_bootstrap_valid(b, true, e))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake capture requires its actual idle bindings");
    uint8_t *data = malloc(65536);
    if (!data) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Capturing production handshake continuation");
    qa_net_writer w; qa_net_writer_init(&w, data, 65536, e);
    bool ok = qa_net_write_data(&w, "QAUH1", 5) && qa_net_write_u8(&w, b->options.server) &&
        qa_net_write_u32(&w, b->options.max_clients) &&
        (b->options.server || address_write(&w, &b->options.remote)) &&
        qa_net_write_data(&w, b->nonce.bytes, 16) && qa_net_write_data(&w, b->token.bytes, 16) &&
        qa_net_write_u64(&w, b->now_ns) && qa_net_write_u64(&w, b->handshake_started) && qa_net_write_u64(&w, b->last_handshake) &&
        qa_net_write_u8(&w, b->has_token) && qa_net_write_u8(&w, b->started) &&
        qa_net_write_u8(&w, b->sent) && qa_net_write_u8(&w, b->attach_pending) && qa_net_write_u8(&w, b->closed) &&
        qa_net_write_u16(&w, (uint16_t)b->pending_count) && qa_net_write_u16(&w, (uint16_t)b->peer_count);
    for (size_t i = 0; ok && i < b->pending_count; ++i) {
        const qa_unified_pending *pending = b->pending + i;
        ok = address_write(&w, &pending->address) && qa_net_write_data(&w, pending->nonce.bytes, 16) &&
            qa_net_write_data(&w, pending->token.bytes, 16) && qa_net_write_u64(&w, pending->created) && qa_net_write_u8(&w, pending->connect);
    }
    for (size_t i = 0; ok && i < b->peer_count; ++i) {
        const qa_unified_peer_receipt *peer = b->peers + i;
        ok = address_write(&w, &peer->address) && qa_net_write_data(&w, peer->nonce.bytes, 16) &&
            qa_net_write_data(&w, peer->token.bytes, 16) && qa_net_write_u32(&w, peer->id.slot) &&
            qa_net_write_u64(&w, peer->id.generation) && qa_net_write_u8(&w, peer->flush);
    }
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}

bool qa_unified_bootstrap_restore(qa_bytes bytes, qa_network_runtime *runtime, uint64_t owner,
    const qa_unified_bootstrap_hooks *hooks, qa_unified_bootstrap_resolve resolve, void *context,
    qa_unified_bootstrap **out, qa_error *e)
{
    if (!runtime || !owner || !hooks || !hooks->attach || !resolve || !out ||
        !qa_network_callbacks_idle(runtime) || bytes.size > 65536)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production handshake restore lacks its actual candidate bindings");
    qa_unified_bootstrap *b = calloc(1, sizeof(*b));
    if (!b) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Restoring production handshake continuation");
    b->runtime = runtime; b->options.hooks = *hooks;
    qa_net_reader r; qa_net_reader_init(&r, bytes, e);
    char magic[5];
    bool ok = qa_net_read_data(&r, magic, 5);
    if (ok && memcmp(magic, "QAUH1", 5)) ok = qa_net_reader_fail(&r, "Unknown production handshake continuation");
    ok = ok && flag(&r, &b->options.server);
    b->options.max_clients = qa_net_read_u32(&r);
    if (ok && !b->options.server) ok = address_read(&r, &b->options.remote);
    ok = ok && qa_net_read_data(&r, b->nonce.bytes, 16) && qa_net_read_data(&r, b->token.bytes, 16);
    b->now_ns = qa_net_read_u64(&r); b->handshake_started = qa_net_read_u64(&r); b->last_handshake = qa_net_read_u64(&r);
    ok = ok && flag(&r, &b->has_token) && flag(&r, &b->started) && flag(&r, &b->sent) && flag(&r, &b->attach_pending) && flag(&r, &b->closed);
    b->pending_count = qa_net_read_u16(&r); b->peer_count = qa_net_read_u16(&r);
    if (b->pending_count > 256 || b->peer_count > 264) ok = qa_net_reader_fail(&r, "Production handshake extent exceeds its source bounds");
    for (size_t i = 0; ok && i < b->pending_count; ++i) {
        qa_unified_pending *pending = b->pending + i;
        ok = address_read(&r, &pending->address) && qa_net_read_data(&r, pending->nonce.bytes, 16) &&
            qa_net_read_data(&r, pending->token.bytes, 16);
        pending->created = qa_net_read_u64(&r);
        ok = ok && flag(&r, &pending->connect);
    }
    for (size_t i = 0; ok && i < b->peer_count; ++i) {
        qa_unified_peer_receipt *peer = b->peers + i;
        ok = address_read(&r, &peer->address) && qa_net_read_data(&r, peer->nonce.bytes, 16) &&
            qa_net_read_data(&r, peer->token.bytes, 16);
        peer->id.owner = owner; peer->id.slot = qa_net_read_u32(&r); peer->id.generation = qa_net_read_u64(&r);
        ok = ok && flag(&r, &peer->flush);
    }
    ok = ok && qa_net_reader_finish(&r) && qa_unified_bootstrap_valid(b, false, e);
    for (size_t i = 0; ok && i < b->peer_count; ++i) ok = resolve(context, b->peers[i].id, &b->peers[i].session, e);
    if (ok) ok = qa_unified_bootstrap_valid(b, true, e);
    if (!ok) { free(b); return false; }
    *out = b; return true;
}
