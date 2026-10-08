#include "session_internal.h"
#include "channel_internal.h"
#include "qa/network_unified_save.h"
#include "../runtime/internal.h"

#include <string.h>

static bool actual_ops(const qa_network_peer_ops *ops)
{
    qa_network_peer_ops actual = qa_unified_session_operations();
    return ops && ops->receive == actual.receive && ops->flush == actual.flush &&
        ops->command == actual.command && ops->restart == actual.restart &&
        ops->rebind == actual.rebind && ops->close == actual.close &&
        ops->receive_pending == actual.receive_pending;
}
uint32_t qa_unified_session_required(const qa_unified_session *s) { return s ? s->required : 0; }
uint32_t qa_unified_session_reliable_acknowledged(const qa_unified_session *s)
{ return qa_unified_channel_acknowledged(s ? s->channel : NULL); }
bool qa_unified_session_restart_admit(const qa_unified_session *s, const qa_network_runtime *runtime,
    const qa_net_connect *request, const qa_unified_document *offer, qa_error *e)
{
    const qa_net_client *client = s && runtime ? qa_net_connections_get(qa_network_connections(runtime), s->id) : NULL;
    const qa_network_peer *installed = s && runtime && s->id.slot < runtime->options.clients ?
        runtime->peers + s->id.slot : NULL;
    if (!s || s->runtime != runtime || !runtime || qa_network_callbacks_idle(runtime) || !s->bound_source ||
        s->entered || s->closing || s->disconnected || s->timeout_pending || !request || !client ||
        !qa_unified_session_peer(installed) || installed->state != s ||
        request->attachment != client->attachment ||
        request->protocol.kind != QA_NET_UNIFIED_1 || request->protocol.flags || request->protocol.revision ||
        request->seats != client->seats || request->seat_count != 1 || client->seat_count != 1 ||
        !qa_net_address_equal(&request->endpoint, &client->endpoint, true) || !qa_unified_session_kind(offer, "offer"))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart admission lacks its actual canonical peer and offer");
    if (!qa_unified_session_qualified(s, client, e)) return false;
    uint32_t epoch;
    if (!qa_unified_document_epoch(offer, &epoch, e)) return false;
    if (s->server) {
        if (s->processing || s->epoch == UINT32_MAX || epoch != s->epoch + 1)
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Server restart admission changes its prepared next offer epoch");
        for (const qa_unified_held *held = s->held; held; held = held->next)
            if (held->source_finished)
                return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Server restart admission retains a completed prior reply batch");
    } else {
        const qa_unified_held *held = s->held;
        if (!s->processing || !held || held->source_finished || held->kind != QA_UNIFIED_CONTROL_DOCUMENT ||
            !qa_unified_session_kind(held->document, "offer") || epoch <= s->epoch)
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Client restart admission has no actual unfinished received offer");
        qa_bytes actual = qa_json_source(qa_unified_document_json(held->document), qa_unified_document_root(held->document));
        qa_bytes proposed = qa_json_source(qa_unified_document_json(offer), qa_unified_document_root(offer));
        if (actual.size != proposed.size || (actual.size && memcmp(actual.data, proposed.data, actual.size)))
            return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Client restart admission changes its authentic received offer bytes");
    }
    const qa_json_document *json = qa_unified_document_json(offer);
    qa_json_id composition = qa_json_get(json, qa_unified_session_value(offer), "composition");
    uint64_t generation;
    if (!qa_json_u64(json, qa_json_get(json, composition, "generation"), &generation, e) || !generation) return false;
    return request->composition == generation ||
        qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production restart admission changes the actual offered generation");
}
bool qa_unified_session_client_disconnect_pending(const qa_unified_session *s,
    qa_network_runtime *runtime, qa_net_client_id id, uint32_t epoch,
    const qa_unified_document *disconnect, const qa_unified_document *offer, qa_error *e)
{
    qa_unified_session *installed = NULL;
    const qa_unified_held *held = s ? s->held : NULL;
    if (!s || s->server || s->runtime != runtime || !s->bound_source || !s->processing ||
        s->entered || s->closing || s->disconnected || !s->timeout_pending || epoch != s->epoch ||
        !qa_net_client_id_equal(id, s->id) || !s->timeout_delivery || s->timeout_delivery->source_finished ||
        s->timeout_delivery->document != disconnect || !qa_unified_session_kind(disconnect, "disconnect") ||
        !held || held->source_finished || held->kind != QA_UNIFIED_CONTROL_DOCUMENT ||
        !qa_unified_session_kind(held->document, "offer") || !qa_unified_session_kind(offer, "offer") ||
        !qa_unified_session_find(runtime, id, &installed, e) || installed != s)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "CLIENT retirement lacks its actual unfinished offer and local disconnect callback");
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(runtime), id);
    if (!qa_unified_session_qualified(s, client, e)) return false;
    uint32_t offered;
    if (!qa_unified_document_epoch(offer, &offered, e)) return false;
    qa_bytes actual = qa_json_source(qa_unified_document_json(held->document), qa_unified_document_root(held->document));
    qa_bytes retained = qa_json_source(qa_unified_document_json(offer), qa_unified_document_root(offer));
    return (offered > epoch && actual.size == retained.size &&
        (!actual.size || !memcmp(actual.data, retained.data, actual.size))) ||
        qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "CLIENT retirement changes its authentic newer received offer");
}
bool qa_unified_session_control_receipt(const qa_unified_session *s, uint32_t sequence,
    const qa_unified_document *document, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !sequence || sequence > s->required || !document ||
        qa_unified_document_type(document) != QA_UNIFIED_CONTROL_DOCUMENT)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Source control receipt lacks its real queued sequence");
    if (sequence <= s->channel->reliable_acknowledged) return true;
    const outgoing *queued = s->channel->reliable;
    while (queued && queued->sequence != sequence) queued = queued->next;
    qa_buffer wire = {0};
    if (!qa_unified_document_encode(document, &wire, e)) return false;
    bool okay = queued && queued->payload.size == wire.size &&
        (!wire.size || !memcmp(queued->payload.data, wire.data, wire.size));
    qa_buffer_free(&wire);
    return okay || qa_unified_session_fail(e, QA_ERROR_FORMAT, "Source control receipt differs from its actual retained reliable bytes");
}

bool qa_unified_session_attachment(const qa_network_runtime *runtime, const qa_network_peer_ops *ops, const void *state, const qa_net_connect *request)
{
    if (!state || !request || !actual_ops(ops) || request->protocol.kind != QA_NET_UNIFIED_1 ||
        request->protocol.flags || request->protocol.revision || request->seat_count != 1 || !request->seats) return false;
    const qa_unified_session *s = state;
    return s->runtime == runtime && runtime && s->channel && !s->id.owner && !s->id.generation && !s->epoch &&
        s->seat.owner == request->seats[0].seat.owner && s->seat.index == request->seats[0].seat.index &&
        s->hooks.player && s->hooks.control &&
        (s->server ? (s->hooks.input && s->hooks.restart) : (s->hooks.prepare && s->hooks.frame));
}

bool qa_unified_session_peer(const qa_network_peer *peer)
{
    if (!peer || !peer->occupied || !peer->state) return false;
    if (!actual_ops(&peer->ops)) return false;
    const qa_unified_session *s = peer->state;
    return s->runtime && s->channel && qa_net_client_id_equal(s->id, peer->id) &&
        peer->seat_count == 1 && peer->seats && peer->seats[0].id.owner == s->seat.owner && peer->seats[0].id.index == s->seat.index;
}

bool qa_unified_session_find(qa_network_runtime *runtime, qa_net_client_id id, qa_unified_session **out, qa_error *e)
{
    if (!runtime || !out || !qa_network_callbacks_idle(runtime))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production lookup requires its returned runtime and output");
    qa_network_peer *peer = qa_network_peer_get(runtime, id, e);
    if (!qa_unified_session_peer(peer) || ((qa_unified_session *)peer->state)->runtime != runtime)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production lookup does not name its actual installed session");
    *out = peer->state; return true;
}

bool qa_unified_session_token_conflict(const void *state, const qa_network_peer *peer)
{
    return state && qa_unified_session_peer(peer) &&
        !memcmp(((const qa_unified_session *)state)->token.bytes,
            ((const qa_unified_session *)peer->state)->token.bytes, 16);
}

bool qa_unified_session_peer_tokens_equal(const qa_network_peer *a, const qa_network_peer *b)
{
    return qa_unified_session_peer(a) && qa_unified_session_peer(b) && qa_unified_session_token_conflict(b->state, a);
}

bool qa_unified_session_peer_matches(const qa_network_peer *peer, const qa_net_datagram *packet)
{
    if (!packet || !qa_unified_session_peer(peer)) return false;
    const qa_unified_session *s = peer->state;
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(s->runtime), s->id);
    qa_unified_packet wire;
    if (!client || client->protocol.kind != QA_NET_UNIFIED_1 || client->protocol.flags || client->protocol.revision ||
        !qa_net_address_equal(&client->endpoint, &packet->from, true) ||
        !qa_unified_packet_decode(packet->payload, &wire, NULL)) return false;
    unsigned mismatch = 0;
    for (size_t i = 0; i < 16; ++i) mismatch |= (unsigned)(wire.token.bytes[i] ^ s->token.bytes[i]);
    return mismatch == 0;
}

bool qa_unified_session_peer_checkpoint(const qa_network_peer *peer, qa_buffer *out, qa_error *e)
{
    if (!qa_unified_session_peer(peer))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production codec lacks its genuine installed peerops owner");
    return qa_unified_session_checkpoint(peer->state, out, e);
}

bool qa_unified_session_peer_source_ready(const qa_network_peer *peer, qa_error *e)
{
    return !qa_unified_session_peer(peer) || qa_unified_session_source_ready(peer->state, e);
}
void qa_unified_session_peer_source_publish(qa_network_peer *peer)
{
    if (qa_unified_session_peer(peer)) qa_unified_session_source_publish(peer->state);
}
void qa_unified_session_peer_source_retire(qa_network_peer *peer)
{
    if (qa_unified_session_peer(peer)) qa_unified_session_source_retire(peer->state);
}
