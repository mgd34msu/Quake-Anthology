#include "bootstrap_internal.h"
#include <stdio.h>
#include <errno.h>

bool q2_bootstrap_idle(qa_network_q2_bootstrap *owner, qa_error *error)
{
    return owner && !owner->busy && qa_network_callbacks_idle(owner->runtime) ? true :
        q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bootstrap requires its idle enclosing receiver");
}
bool q2_bootstrap_server_hooks_valid(const qa_q2_server_bootstrap_hooks *hooks)
{
    return hooks && hooks->enabled && hooks->rejects && hooks->prepare && hooks->committed &&
        hooks->abort && hooks->discovery && hooks->download_server;
}
bool q2_bootstrap_client_hooks_valid(const qa_q2_client_bootstrap_hooks *hooks)
{
    return hooks && hooks->identity && hooks->prepare && hooks->committed && hooks->abort &&
        hooks->cancel && hooks->print && hooks->failed;
}
static bool protocols(qa_network_q2_bootstrap *owner, const qa_net_protocol_id *values,
    size_t count, qa_error *error)
{
    if (!values || !count || count > 8) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bootstrap needs actual live Source offers");
    for (size_t i = 0; i < count; ++i) {
        qa_q2_codec codec;
        if (!qa_q2_codec_init(&codec, values[i], error) || values[i].kind == QA_NET_Q2KEX_DEMO_2022) return false;
        if ((values[i].kind == QA_NET_Q2KEX_2023) != (values[0].kind == QA_NET_Q2KEX_2023))
            return q2_fail(error, QA_ERROR_ARGUMENT, "Native KEX offers require their actual LAN transport family");
    }
    memcpy(owner->protocols, values, count * sizeof(*values)); owner->protocol_count = count; return true;
}
static bool offers(const qa_network_q2_bootstrap *owner, qa_net_protocol_id protocol)
{
    for (size_t i = 0; i < owner->protocol_count; ++i)
        if (owner->protocols[i].kind == protocol.kind) return true;
    return false;
}
static void negotiated_minor(const qa_network_q2_bootstrap *owner, qa_q2_connect_request *request)
{
    if (request->protocol.kind != QA_NET_R1Q2_35 && request->protocol.kind != QA_NET_Q2PRO_36) return;
    uint32_t maximum = 0;
    for (size_t i = 0; i < owner->protocol_count; ++i)
        if (owner->protocols[i].kind == request->protocol.kind && owner->protocols[i].revision > maximum)
            maximum = owner->protocols[i].revision;
    if (maximum < request->protocol.revision) request->protocol.revision = maximum;
}
static bool kex_offer(const qa_network_q2_bootstrap *owner)
{
    for (size_t i = 0; i < owner->protocol_count; ++i)
        if (owner->protocols[i].kind == QA_NET_Q2KEX_2023) return true;
    return false;
}
static bool identity_valid(const qa_q2_client_identity *identity, bool ready_kex, qa_error *error)
{
    if (!memchr(identity->userinfo, 0, sizeof(identity->userinfo)) || identity->social_count > QA_Q2_MAX_SEATS ||
        (ready_kex && !identity->social_count))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT identity leaves its authentic wire extent");
    for (size_t i = 0; i < identity->social_count; ++i)
        if (!memchr(identity->social_ids[i], 0, sizeof(identity->social_ids[i])))
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 social identity lacks termination");
    return true;
}
static bool send_text(qa_network_q2_bootstrap *owner, const qa_net_address *to,
    const char *text, qa_error *error)
{
    size_t length = strlen(text);
    if (length > SIZE_MAX - 4) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 connectionless reply overflows");
    uint8_t *bytes = malloc(length + 4);
    if (!bytes) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 connectionless reply");
    qa_net_writer writer; qa_net_writer_init(&writer, bytes, length + 4, error);
    bool ok = qa_q2_oob_write(&writer, text) && qa_network_send_address(owner->runtime, to,
        (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    free(bytes); return ok;
}
static bool refusal(qa_network_q2_bootstrap *owner, const qa_net_address *to,
    const char *reason, qa_error *error)
{
    size_t length = strlen(reason);
    if (length > SIZE_MAX - 8) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 refusal reason overflows");
    char *text = malloc(length + 8);
    if (!text) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 refusal reason");
    memcpy(text, "print\n", 6); memcpy(text + 6, reason, length); text[6 + length] = '\n'; text[7 + length] = 0;
    bool ok = send_text(owner, to, text, error); free(text); return ok;
}
static bool acceptance(qa_network_q2_bootstrap *owner, const qa_net_address *to,
    qa_net_protocol_id protocol, qa_error *error)
{
    const char *url = NULL;
    if (!owner->state.server.hooks.download_server(owner->state.server.hooks.context, &url, error)) return false;
    if (url && strpbrk(url, " \t\r\n\"")) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source download advertisement is not one validated URL");
    const char *prefix = protocol.kind == QA_NET_Q2KEX_2023 ? "client_connect 2023" : "client_connect";
    size_t length = strlen(prefix), extra = url && *url ? strlen(url) : 0;
    if (extra > SIZE_MAX - length - 11) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 acceptance advertisement overflows");
    char *text = malloc(length + extra + 11);
    if (!text) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 acceptance advertisement");
    if (extra) snprintf(text, length + extra + 11, "%s dlserver=%s\n", prefix, url);
    else snprintf(text, length + extra + 11, "%s\n", prefix);
    bool ok = send_text(owner, to, text, error); free(text); return ok;
}
bool qa_network_q2_bootstrap_server(qa_network_runtime *runtime,
    const qa_q2_server_bootstrap_options *options, qa_network_q2_bootstrap **out, qa_error *error)
{
    if (!runtime || !options || !out || *out || !qa_network_callbacks_idle(runtime) ||
        !options->pending_capacity || options->pending_capacity > 65536 || !q2_bootstrap_server_hooks_valid(&options->hooks))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 host bootstrap lacks its actual Source admission and services");
    qa_network_q2_bootstrap *owner = calloc(1, sizeof(*owner));
    if (!owner) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 host bootstrap");
    owner->runtime = runtime; owner->server = true; owner->state.server.hooks = options->hooks;
    owner->state.server.pending_capacity = options->pending_capacity;
    bool ok = protocols(owner, options->protocols, options->protocol_count, error);
    if (ok && kex_offer(owner) && !options->hooks.transport_admitted)
        ok = q2_fail(error, QA_ERROR_ARGUMENT, "Q2 KEX host has no actual LAN admission proof");
    if (ok) ok = qa_q2_challenges_create(options->challenge_capacity, options->random,
        options->random_context, &owner->state.server.challenges, error);
    if (ok) {
        owner->state.server.pending = calloc(options->pending_capacity, sizeof(*owner->state.server.pending));
        if (!owner->state.server.pending) ok = q2_fail(error, QA_ERROR_MEMORY, "Allocating actual pending Q2 admission receipts");
    }
    if (!ok) { qa_network_q2_bootstrap_destroy(owner); return false; }
    *out = owner; return true;
}
bool qa_network_q2_bootstrap_client(qa_network_runtime *runtime,
    const qa_q2_client_bootstrap_options *options, qa_network_q2_bootstrap **out, qa_error *error)
{
    if (!runtime || !options || !out || *out || !qa_network_callbacks_idle(runtime) ||
        !options->timeout_ns || !q2_bootstrap_client_hooks_valid(&options->hooks))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 client bootstrap lacks its genuine Source factory");
    qa_network_q2_bootstrap *owner = calloc(1, sizeof(*owner));
    if (!owner) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 client bootstrap");
    owner->runtime = runtime; owner->state.client.hooks = options->hooks;
    owner->state.client.timeout_ns = options->timeout_ns; owner->state.client.generation = 1;
    bool ok = protocols(owner, options->protocols, options->protocol_count, error);
    if (ok && kex_offer(owner) && !options->hooks.transport_ready)
        ok = q2_fail(error, QA_ERROR_ARGUMENT, "Q2 KEX client lacks its genuine LAN join");
    qa_q2_client_identity identity = {0};
    if (ok) ok = options->hooks.identity(options->hooks.context, &identity, error);
    if (ok) ok = identity_valid(&identity, false, error);
    if (ok) ok =
        qa_q2_handshake_init(&owner->state.client.handshake, &options->remote, options->protocols,
            options->protocol_count, options->qport, identity.userinfo,
            identity.social_count ? identity.social_ids[0] : "", options->payload_bytes, error);
    if (ok && kex_offer(owner)) {
        owner->state.client.handshake.request.social_count = identity.social_count;
        memcpy(owner->state.client.handshake.request.social_ids, identity.social_ids, sizeof(identity.social_ids));
    }
    if (!ok) { qa_network_q2_bootstrap_destroy(owner); return false; }
    *out = owner; return true;
}
void qa_network_q2_bootstrap_destroy(qa_network_q2_bootstrap *owner)
{
    if (!owner) return;
    if (owner->server) { qa_q2_challenges_destroy(owner->state.server.challenges); free(owner->state.server.pending); }
    free(owner);
}
static bool queue_connect(qa_network_q2_bootstrap *owner, const qa_net_datagram *packet,
    const qa_q2_connect_request *request, qa_error *error)
{
    size_t index = owner->state.server.pending_count;
    for (size_t i = 0; i < index; ++i)
        if (qa_net_address_equal(&owner->state.server.pending[i].from, &packet->from, true)) { index = i; break; }
    if (index == owner->state.server.pending_capacity) return refusal(owner, &packet->from, "Server admission queue is full.", error);
    if (index == owner->state.server.pending_count) ++owner->state.server.pending_count;
    owner->state.server.pending[index] = (qa_q2_pending_connect){packet->from, *request, packet->received_ns}; return true;
}
static bool server_receive(qa_network_q2_bootstrap *owner, const qa_net_datagram *packet,
    const qa_q2_oob *message, bool *recognized, qa_error *error)
{
    qa_q2_server_bootstrap_hooks *hooks = &owner->state.server.hooks;
    bool enabled = false, blocked = false;
    if (!hooks->enabled(hooks->context, &enabled, error)) return false;
    if (!enabled) return true;
    if (!hooks->rejects(hooks->context, &packet->from, &blocked, error)) return false;
    if (blocked) { *recognized = true; return true; }
    const char *command = message->command;
    if (strcmp(command, "getchallenge") && strcmp(command, "connect") && strcmp(command, "status") &&
        strcmp(command, "info") && strcmp(command, "ping")) return true;
    *recognized = true;
    if (!strcmp(command, "ping")) return send_text(owner, &packet->from, "ack", error);
    if (!strcmp(command, "connect")) {
        qa_q2_connect_request request;
        qa_error parse = {0};
        if (!qa_q2_connect_read(message, &request, &parse)) {
            if (parse.code == QA_ERROR_MEMORY || parse.code == QA_ERROR_IO) { if (error) *error = parse; return false; }
            return refusal(owner, &packet->from, *parse.message ? parse.message : "Invalid connect request.", error);
        }
        if (!offers(owner, request.protocol)) return refusal(owner, &packet->from, "Unsupported protocol.", error);
        bool admitted = request.protocol.kind != QA_NET_Q2KEX_2023 &&
            qa_q2_challenge_validate(owner->state.server.challenges, &packet->from, request.challenge);
        if (request.protocol.kind == QA_NET_Q2KEX_2023 &&
            !hooks->transport_admitted(hooks->context, &packet->from, &admitted, error)) return false;
        if (!admitted) return refusal(owner, &packet->from, "Bad challenge.", error);
        negotiated_minor(owner, &request);
        return queue_connect(owner, packet, &request, error);
    }
    uint8_t bytes[QA_Q2_MAX_OOB_TEXT + 4]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!strcmp(command, "getchallenge")) return qa_q2_challenge_reply(owner->state.server.challenges,
        &packet->from, packet->received_ns, owner->protocols, owner->protocol_count, &writer) &&
        qa_network_send_address(owner->runtime, &packet->from, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    qa_q2_discovery discovery = {0};
    if (!hooks->discovery(hooks->context, &discovery, error)) return false;
    bool present = true, ok;
    if (!strcmp(command, "status")) ok = qa_q2_status_write(&writer, &discovery.status, 1400);
    else {
        errno = 0; char *end; unsigned long version = strtoul(message->argc ? message->argv[0] : "", &end, 10);
        if (errno || *end || version > UINT32_MAX) version = 0;
        ok = qa_q2_info_write(&writer, discovery.name, discovery.map, discovery.players, discovery.maximum,
            owner->protocols, owner->protocol_count, (uint32_t)version, &present);
    }
    return ok && (!present || qa_network_send_address(owner->runtime, &packet->from,
        (qa_bytes){bytes, qa_net_writer_size(&writer)}, error));
}
static bool native_oob(const qa_network_q2_bootstrap *owner, qa_bytes bytes)
{
    if (!bytes.data || bytes.size < 5 || qa_load_u32le(bytes.data) != UINT32_MAX) return false;
    static const char *const server[] = {"getchallenge", "connect", "status", "info", "ping"};
    static const char *const client[] = {"challenge", "client_connect", "print"};
    const char *const *commands = owner->server ? server : client;
    size_t count = owner->server ? sizeof(server) / sizeof(*server) : sizeof(client) / sizeof(*client);
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(commands[i]);
        if (bytes.size - 4 < length || memcmp(bytes.data + 4, commands[i], length)) continue;
        if (bytes.size - 4 == length) return strcmp(commands[i], "connect") != 0;
        unsigned c = bytes.data[4 + length];
        if (c && c != ' ' && c != '\t' && c != '\r' && c != '\n') continue;
        if (owner->server && (!strcmp(commands[i], "connect") || !strcmp(commands[i], "getchallenge"))) {
            size_t at = 4 + length;
            while (at < bytes.size && bytes.data[at] && bytes.data[at] <= 32) ++at;
            if (!strcmp(commands[i], "getchallenge")) return at == bytes.size || !bytes.data[at];
            if (at == bytes.size) return false;
            c = bytes.data[at]; return (c >= '0' && c <= '9') || c == '+' || c == '-';
        }
        return true;
    }
    return false;
}
static bool receive_oob(qa_network_q2_bootstrap *owner,
    const qa_net_datagram *packet, bool *recognized, qa_error *error)
{
    qa_q2_oob message; bool oob;
    qa_error parse = {0};
    if (!qa_q2_oob_read(packet->payload, kex_offer(owner), &message, &oob, &parse)) {
        if (parse.code != QA_ERROR_FORMAT) { if (error) *error = parse; return false; }
        *recognized = true; return true;
    }
    if (!oob) return true;
    if (owner->server) return server_receive(owner, packet, &message, recognized, error);
    qa_q2_handshake *handshake = &owner->state.client.handshake;
    if (!qa_net_address_equal(&packet->from, &handshake->remote, true)) return true;
    bool accepted = false;
    if (!qa_q2_handshake_receive(handshake, &packet->from, &message, &accepted, error)) return false;
    if (!strcmp(message.command, "print")) {
        if (!owner->state.client.hooks.print(owner->state.client.hooks.context, message.text + message.body_offset, error)) return false;
        accepted = true;
    }
    if (accepted) { owner->state.client.received = true; owner->state.client.received_ns = packet->received_ns; }
    *recognized = accepted; return true;
}
bool qa_network_q2_bootstrap_receive(qa_network_q2_bootstrap *owner,
    const qa_net_datagram *packet, bool *recognized, qa_error *error)
{
    if (!owner || !packet || !recognized || owner->busy || (packet->payload.size && !packet->payload.data))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 bootstrap reception owner");
    *recognized = false;
    /* Binary master/browser and other-family OOB payloads belong to the
     * enclosing dispatcher; never pass them through a KEX UTF-8 parser. */
    if (owner->canceled || !native_oob(owner, packet->payload)) return true;
    owner->busy = true; bool ok = receive_oob(owner, packet, recognized, error);
    owner->busy = false; return ok;
}
static bool same_connection(const qa_net_connect *connection, const qa_net_address *from,
    const qa_q2_connect_request *wire)
{
    size_t seats = wire->protocol.kind == QA_NET_Q2KEX_2023 ? wire->social_count : 1;
    return qa_net_address_equal(&connection->endpoint, from, true) && connection->protocol.kind == wire->protocol.kind &&
        connection->protocol.revision == wire->protocol.revision && connection->protocol.flags == wire->protocol.flags &&
        connection->seat_count == seats && seats && connection->seats;
}
static bool same_channel(const qa_q2_channel_options *channel, const qa_q2_connect_request *wire, bool server)
{
    return channel->server == server && channel->protocol.kind == wire->protocol.kind &&
        channel->protocol.revision == wire->protocol.revision && channel->protocol.flags == wire->protocol.flags &&
        channel->qport == wire->qport && channel->payload_bytes == wire->payload_bytes &&
        channel->new_channel == wire->new_channel && channel->compress == wire->compression;
}
static bool server_continue(qa_network_q2_bootstrap *owner, uint64_t now, qa_error *error)
{
    qa_q2_server_bootstrap_hooks *hooks = &owner->state.server.hooks;
    while (owner->state.server.pending_count) {
        qa_q2_pending_connect pending = owner->state.server.pending[0];
        memmove(owner->state.server.pending, owner->state.server.pending + 1,
            (--owner->state.server.pending_count) * sizeof(pending));
        bool enabled = false, blocked = false;
        if (!hooks->enabled(hooks->context, &enabled, error) ||
            !hooks->rejects(hooks->context, &pending.from, &blocked, error)) return false;
        if (!enabled || blocked) continue;
        if (pending.request.protocol.kind == QA_NET_Q2KEX_2023) {
            bool admitted = false;
            if (!hooks->transport_admitted(hooks->context, &pending.from, &admitted, error)) return false;
            if (!admitted) continue;
        }
        uint32_t cursor = 0; const qa_net_client *existing;
        bool found = false;
        while (qa_net_connections_next(qa_network_connections(owner->runtime), &cursor, &existing))
            if (qa_net_address_equal(&existing->endpoint, &pending.from, true)) { found = true; break; }
        if (found) {
            qa_network_peer *peer = qa_network_peer_get(owner->runtime, existing->id, error);
            if (!qa_network_q2_peer(peer) || !((q2_session *)peer->state)->server ||
                ((q2_session *)peer->state)->retiring || existing->protocol.kind != pending.request.protocol.kind)
                return refusal(owner, &pending.from, "Endpoint already owns another connection.", error);
            if (!acceptance(owner, &pending.from, existing->protocol, error)) return false;
            continue;
        }
        qa_q2_server_admission claim = {0}; bool allowed = false; char reason[1024] = {0};
        bool ok = hooks->prepare(hooks->context, &pending.from, &pending.request, &claim, &allowed, reason, error);
        if (ok && !memchr(reason, 0, sizeof(reason))) ok = q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source refusal lacks termination");
        if (ok && !allowed) { ok = refusal(owner, &pending.from, *reason ? reason : "Connection refused.", error); }
        qa_net_client_id id = {0};
        if (ok && allowed) {
            ok = same_connection(&claim.connection, &pending.from, &pending.request) && same_channel(&claim.policy.channel, &pending.request, true);
            if (!ok) q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source host claim differs from authenticated wire admission");
            if (ok) ok = qa_network_attach_q2_server(owner->runtime, &claim.connection, &claim.policy, &claim.hooks, now, &id, error);
            if (ok) ok = hooks->committed(hooks->context, &claim, id, error);
            if (ok) ok = qa_network_q2_server_userinfo(owner->runtime, id, pending.request.userinfo, error);
            if (ok) ok = acceptance(owner, &pending.from, pending.request.protocol, error);
        }
        if (!ok || !allowed) {
            qa_error cleanup = {0};
            if (id.generation) qa_network_detach(owner->runtime, id, "Q2 Source admission did not commit", &cleanup);
            bool aborted = hooks->abort(hooks->context, &claim, ok ? error : &cleanup);
            if (!ok || !aborted) return false;
        }
    }
    return true;
}
static bool client_continue(qa_network_q2_bootstrap *owner, uint64_t now, qa_error *error)
{
    qa_q2_client_bootstrap_hooks *hooks = &owner->state.client.hooks;
    qa_q2_handshake *handshake = &owner->state.client.handshake;
    if (handshake->phase == QA_Q2_REFUSED && !owner->state.client.notified) {
        if (!hooks->cancel(hooks->context, owner->state.client.generation, error)) return false;
        owner->state.client.notified = true; return hooks->failed(hooks->context, handshake->refusal, error);
    }
    if (handshake->phase != QA_Q2_CONNECTED || owner->state.client.attached) return true;
    qa_q2_client_admission claim = {0}; qa_q2_preparation result = QA_Q2_PREPARATION_WAITING;
    if (!hooks->prepare(hooks->context, &handshake->remote, &handshake->request, handshake->download_server,
        owner->state.client.generation, &claim, &result, error)) {
        qa_error cleanup = {0}; hooks->abort(hooks->context, &claim, &cleanup); return false;
    }
    if (result == QA_Q2_PREPARATION_WAITING) return true;
    if (result == QA_Q2_PREPARATION_CANCELED) {
        bool ok = hooks->cancel(hooks->context, owner->state.client.generation, error);
        if (ok) owner->canceled = true;
        return ok;
    }
    bool ok = result == QA_Q2_PREPARATION_READY && same_connection(&claim.connection, &handshake->remote, &handshake->request) &&
        same_channel(&claim.policy.channel, &handshake->request, false);
    if (!ok) q2_fail(error, QA_ERROR_ARGUMENT, "Q2 actual CLIENT Source claim differs from accepted wire connection");
    qa_net_client_id id = {0};
    if (ok) ok = qa_network_attach_q2_client(owner->runtime, &claim.connection, &claim.policy, &claim.hooks, now, &id, error);
    if (ok) ok = hooks->committed(hooks->context, &claim, id, error);
    if (ok) { owner->state.client.id = id; owner->state.client.attached = true; return true; }
    qa_error cleanup = {0};
    if (id.generation) qa_network_detach(owner->runtime, id, "Q2 CLIENT Source admission did not commit", &cleanup);
    hooks->abort(hooks->context, &claim, &cleanup); return false;
}
bool qa_network_q2_bootstrap_continue(qa_network_q2_bootstrap *owner, uint64_t now, qa_error *error)
{
    if (!q2_bootstrap_idle(owner, error)) return false;
    if (owner->canceled) return true;
    owner->busy = true;
    bool ok = owner->server ? server_continue(owner, now, error) : client_continue(owner, now, error);
    owner->busy = false; return ok;
}
static bool client_tick(qa_network_q2_bootstrap *owner, uint64_t now, qa_error *error)
{
    qa_q2_handshake *handshake = &owner->state.client.handshake;
    if (!owner->state.client.started) { owner->state.client.started = true; owner->state.client.started_ns = now; }
    uint64_t received = owner->state.client.received ? owner->state.client.received_ns : owner->state.client.started_ns;
    if (!owner->state.client.attached && now >= received && now - received > owner->state.client.timeout_ns) {
        handshake->phase = QA_Q2_REFUSED; memcpy(handshake->refusal, "Connection timed out", 21);
    }
    if (handshake->phase != QA_Q2_CONNECTED && handshake->phase != QA_Q2_REFUSED) {
        bool ready = true;
        if (kex_offer(owner) && !owner->state.client.hooks.transport_ready(owner->state.client.hooks.context, &ready, error)) return false;
        if (ready) {
            qa_q2_client_identity identity = {0};
            if (!owner->state.client.hooks.identity(owner->state.client.hooks.context, &identity, error) ||
                !identity_valid(&identity, kex_offer(owner), error)) return false;
            memcpy(handshake->request.userinfo, identity.userinfo, sizeof(identity.userinfo));
            if (handshake->request.protocol.kind == QA_NET_Q2KEX_2023) {
                handshake->request.social_count = identity.social_count;
                memcpy(handshake->request.social_ids, identity.social_ids, sizeof(identity.social_ids));
            }
            uint8_t bytes[QA_Q2_MAX_OOB_TEXT + 4]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
            bool present;
            if (!qa_q2_handshake_poll(handshake, now, &writer, &present) || (present &&
                !qa_network_send_address(owner->runtime, &handshake->remote, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error))) return false;
        }
    }
    return client_continue(owner, now, error);
}
bool qa_network_q2_bootstrap_tick(qa_network_q2_bootstrap *owner, uint64_t now, qa_error *error)
{
    if (!q2_bootstrap_idle(owner, error)) return false;
    if (owner->canceled) return true;
    owner->busy = true;
    bool ok = owner->server ? server_continue(owner, now, error) : client_tick(owner, now, error);
    owner->busy = false; return ok;
}
bool qa_network_q2_bootstrap_cancel(qa_network_q2_bootstrap *owner, qa_error *error)
{
    if (!q2_bootstrap_idle(owner, error)) return false;
    if (owner->canceled) return true;
    owner->busy = true;
    bool ok = owner->server || owner->state.client.hooks.cancel(owner->state.client.hooks.context,
        owner->state.client.generation, error);
    owner->busy = false; if (!ok) return false;
    owner->canceled = true; if (owner->server) owner->state.server.pending_count = 0; return true;
}
bool qa_network_q2_bootstrap_client_id(const qa_network_q2_bootstrap *owner,
    qa_net_client_id *id, bool *present, qa_error *error)
{
    if (!owner || owner->server || !id || !present) return q2_fail(error, QA_ERROR_ARGUMENT, "Missing Q2 bootstrap client identity");
    *present = owner->state.client.attached;
    *id = *present ? owner->state.client.id : (qa_net_client_id){0}; return true;
}
bool qa_network_q2_bootstrap_disconnected(qa_network_q2_bootstrap *owner,
    qa_net_client_id id, qa_error *error)
{
    if (!q2_bootstrap_idle(owner, error) || owner->server)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 retirement lacks its actual CLIENT bootstrap owner");
    if (!owner->state.client.attached) return true;
    qa_net_client_id committed = owner->state.client.id;
    if (id.owner != committed.owner || id.slot != committed.slot || id.generation != committed.generation ||
        qa_net_connections_get(qa_network_connections(owner->runtime), id))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 retirement does not match its detached canonical client");
    owner->state.client.id = (qa_net_client_id){0}; owner->state.client.attached = false;
    owner->canceled = true; return true;
}
