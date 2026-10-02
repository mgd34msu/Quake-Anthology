#include "bootstrap_internal.h"
#include "handshake_internal.h"
#include "qa/network_q2_bootstrap_save.h"
#include "qa/network_q2_wire_save.h"

static bool invalid(qa_source_save_io *io, const char *message)
{ io->failed = true; return q2_fail(io->error, QA_ERROR_FORMAT, message); }
static bool address(qa_source_save_io *io, qa_net_address *value)
{
    uint32_t kind = (uint32_t)value->kind;
    if (!qa_source_save_u32(io, &kind) || !qa_source_save_u16(io, &value->port)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) value->kind = (qa_net_address_kind)kind;
    bool ok;
    switch (value->kind) {
    case QA_NET_IPV4: ok = qa_source_save_bytes(io, value->host.ipv4, 4); break;
    case QA_NET_IPV6: ok = qa_source_save_bytes(io, value->host.ipv6.bytes, 16) && qa_source_save_u32(io, &value->host.ipv6.scope); break;
    case QA_NET_IPX: ok = qa_source_save_u32(io, &value->host.ipx.network) && qa_source_save_bytes(io, value->host.ipx.node, 6); break;
    case QA_NET_LOOPBACK: ok = qa_source_save_bytes(io, value->host.loopback, sizeof(value->host.loopback)); break;
    default: return invalid(io, "Saved Q2 bootstrap endpoint has no actual address family");
    }
    char text[256]; return ok && (qa_net_address_format(value, text, sizeof(text), io->error) || invalid(io, "Saved Q2 bootstrap endpoint is invalid"));
}
static bool connect(qa_source_save_io *io, qa_q2_connect_request *value, bool optional)
{
    bool selected = qa_q2_protocol_version(value->protocol) != 0;
    if (!qa_source_save_bool(io, &selected)) return false;
    if (selected) {
        if (!qa_q2_save_protocol(io, &value->protocol)) return false;
        qa_q2_codec codec;
        if (!qa_q2_codec_init(&codec, value->protocol, io->error) || value->protocol.kind == QA_NET_Q2KEX_DEMO_2022)
            return invalid(io, "Saved Q2 admission has no actual live codec profile");
    }
    else if (!optional) return invalid(io, "Saved Q2 admission has no selected live dialect");
    else if (io->direction == QA_SOURCE_SAVE_READ) value->protocol = (qa_net_protocol_id){0};
    if (!qa_source_save_u16(io, &value->qport) || !qa_source_save_i32(io, &value->challenge) ||
        !qa_source_save_bytes(io, value->userinfo, sizeof(value->userinfo)) ||
        !qa_source_save_bytes(io, value->social_ids, sizeof(value->social_ids)) ||
        !qa_source_save_count(io, &value->social_count, QA_Q2_MAX_SEATS) ||
        !qa_source_save_count(io, &value->payload_bytes, 65527) ||
        !qa_source_save_bool(io, &value->new_channel) || !qa_source_save_bool(io, &value->compression)) return false;
    if (!memchr(value->userinfo, 0, sizeof(value->userinfo))) return invalid(io, "Saved Q2 userinfo lacks termination");
    for (size_t i = 0; i < value->social_count; ++i)
        if (!memchr(value->social_ids[i], 0, sizeof(value->social_ids[i]))) return invalid(io, "Saved Q2 social identity lacks termination");
    return true;
}
bool qa_q2_save_challenges(qa_source_save_io *io, qa_q2_random_fn random, void *context,
    qa_q2_challenges **value)
{
    if (!io || !value || (io->direction == QA_SOURCE_SAVE_READ ? *value != NULL || !random : *value == NULL)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t capacity = reading ? 0 : (*value)->capacity, count = reading ? 0 : (*value)->count;
    if (!qa_source_save_count(io, &capacity, 65536) || !capacity || !qa_source_save_count(io, &count, capacity)) return false;
    if (reading && count > (io->input.size - io->offset) / 18) return invalid(io, "Saved Q2 challenges exceed their actual document");
    qa_q2_challenges *owner = reading ? NULL : *value;
    if (reading && !qa_q2_challenges_create(capacity, random, context, &owner, io->error)) return false;
    if (reading) owner->count = count;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        struct challenge_entry *entry = owner->entries + i;
        ok = address(io, &entry->address) && qa_source_save_i32(io, &entry->value) && qa_source_save_u64(io, &entry->time);
        if (ok && (entry->value < 0 || entry->value > 32767)) ok = invalid(io, "Saved Q2 challenge leaves its original random range");
        for (size_t j = 0; ok && j < i; ++j)
            if (qa_net_address_equal(&entry->address, &owner->entries[j].address, false)) ok = invalid(io, "Saved Q2 challenges repeat an actual base endpoint");
    }
    if (reading) { if (ok) *value = owner; else qa_q2_challenges_destroy(owner); }
    return ok;
}
static bool handshake(qa_source_save_io *io, qa_q2_handshake *value)
{
    uint32_t phase = (uint32_t)value->phase;
    if (!qa_source_save_u32(io, &phase) || phase > QA_Q2_REFUSED || !address(io, &value->remote) ||
        !qa_source_save_count(io, &value->preference_count, 8) || !value->preference_count) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) value->phase = (qa_q2_handshake_phase)phase;
    for (size_t i = 0; i < value->preference_count; ++i)
        if (!qa_q2_save_protocol(io, value->preferences + i)) return false;
    if (!qa_source_save_u16(io, &value->qport) || !connect(io, &value->request,
        phase == QA_Q2_CHALLENGING || phase == QA_Q2_REFUSED) ||
        !qa_source_save_u64(io, &value->last_sent_ns) || !qa_source_save_u64(io, &value->retry_ns) ||
        !qa_source_save_bool(io, &value->sent) ||
        !qa_source_save_bytes(io, value->download_server, sizeof(value->download_server)) ||
        !qa_source_save_bytes(io, value->refusal, sizeof(value->refusal))) return false;
    return (value->retry_ns && memchr(value->download_server, 0, sizeof(value->download_server)) &&
        memchr(value->refusal, 0, sizeof(value->refusal))) || invalid(io, "Saved Q2 handshake lacks its retained retry or text fields");
}
static bool fields(qa_source_save_io *io, qa_network_q2_bootstrap *owner,
    const qa_q2_server_bootstrap_options *server, const qa_q2_client_bootstrap_options *client)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, role = owner->server;
    uint32_t tag = UINT32_C(0x3242514e), version = 1;
    size_t count = owner->protocol_count;
    if (!qa_source_save_u32(io, &tag) || !qa_source_save_u32(io, &version) ||
        !qa_source_save_bool(io, &role) || tag != UINT32_C(0x3242514e) || version != 1 || role != owner->server ||
        !qa_source_save_count(io, &count, 8) || count != owner->protocol_count ||
        !qa_source_save_bool(io, &owner->canceled)) return invalid(io, "Q2 bootstrap candidate role or offers differ");
    for (size_t i = 0; i < count; ++i) {
        qa_net_protocol_id protocol = owner->protocols[i];
        if (!qa_q2_save_protocol(io, &protocol) || protocol.kind != owner->protocols[i].kind ||
            protocol.revision != owner->protocols[i].revision || protocol.flags != owner->protocols[i].flags)
            return invalid(io, "Q2 bootstrap candidate dialect differs");
    }
    if (owner->server) {
        size_t capacity = owner->state.server.pending_capacity;
        if (!qa_source_save_count(io, &capacity, 65536) || !capacity || capacity != owner->state.server.pending_capacity ||
            !qa_source_save_count(io, &owner->state.server.pending_count, capacity) ||
            !qa_q2_save_challenges(io, reading ? server->random : NULL, reading ? server->random_context : NULL,
                &owner->state.server.challenges)) return false;
        if (reading && owner->state.server.challenges->capacity != server->challenge_capacity)
            return invalid(io, "Q2 candidate challenge policy differs");
        for (size_t i = 0; i < owner->state.server.pending_count; ++i) {
            qa_q2_pending_connect *pending = owner->state.server.pending + i;
            if (!address(io, &pending->from) || !connect(io, &pending->request, false) ||
                !qa_source_save_u64(io, &pending->received_ns)) return false;
            bool offered = false;
            for (size_t j = 0; j < owner->protocol_count; ++j)
                if (owner->protocols[j].kind == pending->request.protocol.kind) offered = true;
            if (!offered || (pending->request.protocol.kind == QA_NET_Q2KEX_2023 && !pending->request.social_count))
                return invalid(io, "Saved Q2 pending admission differs from its actual family offers");
            for (size_t j = 0; j < i; ++j)
                if (qa_net_address_equal(&pending->from, &owner->state.server.pending[j].from, true))
                    return invalid(io, "Saved Q2 pending admissions repeat an actual endpoint");
        }
        return true;
    }
    uint64_t timeout = owner->state.client.timeout_ns;
    if (!qa_source_save_u64(io, &timeout) || timeout != owner->state.client.timeout_ns ||
        !handshake(io, &owner->state.client.handshake) ||
        !qa_source_save_u64(io, &owner->state.client.generation) || !owner->state.client.generation ||
        !qa_source_save_u64(io, &owner->state.client.started_ns) || !qa_source_save_u64(io, &owner->state.client.received_ns) ||
        !qa_source_save_bool(io, &owner->state.client.attached) || !qa_source_save_bool(io, &owner->state.client.started) ||
        !qa_source_save_bool(io, &owner->state.client.received) || !qa_source_save_bool(io, &owner->state.client.notified) ||
        !qa_source_save_u32(io, &owner->state.client.id.slot) || !qa_source_save_u64(io, &owner->state.client.id.generation)) return false;
    if (reading) owner->state.client.id.owner = owner->runtime->options.owner;
    qa_q2_handshake *saved = &owner->state.client.handshake;
    if (reading && (!qa_net_address_equal(&saved->remote, &client->remote, true) || saved->qport != client->qport ||
        saved->preference_count != owner->protocol_count)) return invalid(io, "Q2 CLIENT candidate endpoint or preferences differ");
    bool offered = !qa_q2_protocol_version(saved->request.protocol);
    for (size_t i = 0; i < owner->protocol_count; ++i)
        if (owner->protocols[i].kind == saved->request.protocol.kind) offered = true;
    if (!offered || (saved->sent && saved->request.protocol.kind == QA_NET_Q2KEX_2023 && !saved->request.social_count))
        return invalid(io, "Saved Q2 handshake request differs from its actual family offers");
    if (reading) {
        size_t payload = owner->protocols[0].kind == QA_NET_Q2KEX_2023 ? 65527 : client->payload_bytes ? client->payload_bytes : 1390;
        if (saved->request.payload_bytes != payload) return invalid(io, "Q2 CLIENT candidate payload policy differs");
    }
    for (size_t i = 0; i < saved->preference_count; ++i)
        if (saved->preferences[i].kind != owner->protocols[i].kind || saved->preferences[i].revision != owner->protocols[i].revision ||
            saved->preferences[i].flags != owner->protocols[i].flags) return invalid(io, "Q2 retained handshake preferences differ");
    if (owner->state.client.attached) {
        qa_network_peer *peer = qa_network_peer_get(owner->runtime, owner->state.client.id, io->error);
        if (!qa_network_q2_peer(peer) || ((q2_session *)peer->state)->server || saved->phase != QA_Q2_CONNECTED)
            return invalid(io, "Q2 bootstrap CLIENT continuation has no actual restored native peer");
    } else if (owner->state.client.id.generation) return invalid(io, "Q2 unattached bootstrap retains another client identity");
    return true;
}
bool qa_network_q2_bootstrap_capture(qa_network_q2_bootstrap *owner, qa_buffer *out, qa_error *error)
{
    if (!q2_bootstrap_idle(owner, error) || !out || out->data || out->size) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bootstrap capture requires an empty idle owner output");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool ok = fields(&io, owner, NULL, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
static bool restore(qa_network_runtime *runtime, const qa_q2_server_bootstrap_options *server,
    const qa_q2_client_bootstrap_options *client, qa_bytes bytes, qa_network_q2_bootstrap **out, qa_error *error)
{
    if (!runtime || !out || *out || !qa_network_callbacks_idle(runtime) || (!server && !client))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bootstrap restore requires actual isolated candidate operations");
    qa_network_q2_bootstrap *owner = calloc(1, sizeof(*owner));
    if (!owner) return q2_fail(error, QA_ERROR_MEMORY, "Restoring Q2 bootstrap owner");
    owner->runtime = runtime; owner->server = server != NULL;
    owner->protocol_count = server ? server->protocol_count : client->protocol_count;
    const qa_net_protocol_id *values = server ? server->protocols : client->protocols;
    bool ok = values && owner->protocol_count && owner->protocol_count <= 8 &&
        (server ? q2_bootstrap_server_hooks_valid(&server->hooks) :
            client->timeout_ns && q2_bootstrap_client_hooks_valid(&client->hooks));
    if (ok) memcpy(owner->protocols, values, owner->protocol_count * sizeof(*values));
    for (size_t i = 0; ok && i < owner->protocol_count; ++i) {
        qa_q2_codec codec;
        ok = qa_q2_codec_init(&codec, values[i], error) && values[i].kind != QA_NET_Q2KEX_DEMO_2022 &&
            ((values[i].kind == QA_NET_Q2KEX_2023) == (values[0].kind == QA_NET_Q2KEX_2023));
        if (ok && values[i].kind == QA_NET_Q2KEX_2023)
            ok = server ? server->hooks.transport_admitted != NULL : client->hooks.transport_ready != NULL;
    }
    if (server) {
        owner->state.server.hooks = server->hooks; owner->state.server.pending_capacity = server->pending_capacity;
        ok = ok && server->pending_capacity && server->pending_capacity <= 65536;
        if (ok) { owner->state.server.pending = calloc(server->pending_capacity, sizeof(*owner->state.server.pending)); ok = owner->state.server.pending != NULL; }
    } else { owner->state.client.hooks = client->hooks; owner->state.client.timeout_ns = client->timeout_ns; }
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, owner, server, client) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        if (!error || error->code == QA_OK) q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bootstrap candidate operations or storage are invalid");
        qa_network_q2_bootstrap_destroy(owner); return false;
    }
    *out = owner; return true;
}
bool qa_network_q2_bootstrap_restore_server(qa_network_runtime *runtime,
    const qa_q2_server_bootstrap_options *options, qa_bytes bytes, qa_network_q2_bootstrap **out, qa_error *error)
{ return restore(runtime, options, NULL, bytes, out, error); }
bool qa_network_q2_bootstrap_restore_client(qa_network_runtime *runtime,
    const qa_q2_client_bootstrap_options *options, qa_bytes bytes, qa_network_q2_bootstrap **out, qa_error *error)
{ return restore(runtime, NULL, options, bytes, out, error); }
