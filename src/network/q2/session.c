#include "session_internal.h"
#include <math.h>

bool qa_network_q2_server_timeout_policy(qa_network_runtime *runtime, double seconds, qa_error *error)
{
    if (!runtime || !qa_network_callbacks_idle(runtime) || !isfinite(seconds) ||
        (seconds > 0 && seconds * 1000000000.0 >= 18446744073709551616.0))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 timeout policy requires its returned runtime and finite Source duration");
    for (uint32_t i = 0; i < runtime->options.clients; ++i) {
        const qa_network_peer *peer = &runtime->peers[i];
        if (!peer->occupied || qa_network_local_peer(peer)) continue;
        if (!qa_network_q2_peer(peer) || !peer->state || !((const q2_session *)peer->state)->server)
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 server timeout policy cannot replace another transport owner");
    }
    runtime->options.timeout_ns = seconds > 0 ? (uint64_t)(seconds * 1000000000.0) : 0;
    runtime->timeout_enabled = true;
    runtime->timeout_immediate = seconds < 0;
    return true;
}

bool qa_network_q2_peer(const qa_network_peer *peer)
{
    return peer && peer->occupied && peer->ops.receive == qa_network_q2_peer_ops.receive;
}
bool qa_network_q2_retirement_pending(const qa_network_peer *peer)
{
    if (!qa_network_q2_peer(peer) || !peer->state) return false;
    const q2_session *session = peer->state;
    if (!session->retiring) return false;
    if (!session->server) return session->state.client.drop_reason && !session->state.client.drop_hook_done;
    const q2_server *server = &session->state.server;
    return server->drop_reason && ((server->drop_notice && !server->drop_sent) || !server->drop_hook_done);
}
bool qa_network_q2_delivery_pending(const qa_network_peer *peer)
{
    if (!qa_network_q2_peer(peer) || !peer->state) return false;
    const q2_session *session = peer->state;
    return !session->server && !session->retiring && session->state.client.sent_pending;
}
bool qa_network_q2_timeout(qa_network_peer *peer, const char *reason, qa_error *error)
{
    if (!qa_network_q2_peer(peer) || !peer->state || !reason)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 timeout requires its actual Source peer and reason");
    q2_session *session = peer->state;
    if (!session->runtime->pumping || !session->runtime->callback || session->busy)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 timeout requires its returned pump callback");
    session->busy = true;
    bool complete = false, ok;
    if (session->server) {
        ok = q2_server_drop_request(session, reason, error);
        if (ok && session->state.server.drop_reason)
            ok = q2_server_drop_progress(session, session->runtime->now_ns, &complete, error);
        else if (ok) complete = true;
    } else {
        ok = q2_client_drop_progress(session, reason, error); complete = ok;
    }
    session->busy = false;
    return ok && complete;
}
bool q2_server_hooks_valid(const qa_network_q2_server_hooks *hooks)
{
    return hooks && hooks->player && hooks->game_state && hooks->begin && hooks->input && hooks->expand_command && hooks->command &&
        hooks->userinfo && hooks->download_source && hooks->drop;
}
bool q2_client_hooks_valid(const qa_network_q2_client_hooks *hooks)
{
    return hooks && hooks->current && hooks->server_data && hooks->prepare && hooks->frame && hooks->records &&
        hooks->download && hooks->cancel_loading && hooks->acknowledged && hooks->sent && hooks->command &&
        hooks->server_command && hooks->print && hooks->drop;
}

q2_session *q2_get(qa_network_runtime *runtime, qa_net_client_id id, bool server, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer) return NULL;
    if (!qa_network_q2_peer(peer)) { q2_fail(error, QA_ERROR_ARGUMENT, "Connection has no Q2 game channel"); return NULL; }
    q2_session *session = peer->state;
    if (session->server != server || session->retiring) {
        q2_fail(error, QA_ERROR_ARGUMENT, "Q2 connection has the wrong role or is retiring"); return NULL;
    }
    return session;
}

bool qa_network_q2_peer_matches(const qa_network_peer *peer, const qa_net_datagram *packet)
{
    if (!qa_network_q2_peer(peer) || !packet) return false;
    const q2_session *session = peer->state;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client) return false;
    if (!session->server && session->state.client.policy.messages.demo) return false;
    if (qa_net_address_equal(&client->endpoint, &packet->from, true)) return true;
    const qa_q2_channel_options *options = session->server ? &session->state.server.policy.channel : &session->state.client.policy.channel;
    if (!session->server || options->protocol.kind == QA_NET_Q2KEX_2023 ||
        !qa_net_address_equal(&client->endpoint, &packet->from, false)) return false;
    size_t width = !options->new_channel && options->protocol.kind == QA_NET_Q2_34 ? 2 : options->qport ? 1 : 0;
    if (!width || packet->payload.size < 8 + width) return false;
    return width == 2 ? qa_load_u16le(packet->payload.data + 8) == options->qport :
        packet->payload.data[8] == (uint8_t)options->qport;
}

bool q2_send(q2_session *session, qa_bytes bytes, uint64_t now, bool *included, qa_error *error)
{
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 send lost its admitted connection");
    bool ignored = false;
    return qa_q2_channel_send(session->channel, session->runtime->transport, &client->endpoint, bytes, now,
        included ? included : &ignored, error);
}

bool q2_queue_bytes(q2_session *session, qa_bytes bytes, uint8_t seat, bool reliable, qa_error *error)
{
    if (seat >= session->seats || (seat && session->codec.protocol.kind != QA_NET_Q2KEX_2023))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 service does not belong to an admitted wire seat");
    qa_q2_channel_status status;
    qa_q2_channel_get_status(session->channel, &status);
    qa_buffer framed = {0};
    bool ok = true;
    if (seat) {
        const uint8_t prefix[2] = {21, (uint8_t)(seat + 1)}, suffix[2] = {21, 1};
        ok = q2_buffer_append(&framed, (qa_bytes){prefix, sizeof(prefix)}, status.capacity, error) &&
            q2_buffer_append(&framed, bytes, status.capacity, error) &&
            q2_buffer_append(&framed, (qa_bytes){suffix, sizeof(suffix)}, status.capacity, error);
        bytes = (qa_bytes){framed.data, framed.size};
    }
    if (ok) ok = reliable ? qa_q2_channel_queue(session->channel, bytes, error) :
        q2_buffer_append(&session->state.server.datagram, bytes, status.capacity, error);
    qa_buffer_free(&framed); return ok;
}

bool q2_queue_event(q2_session *session, const qa_q2_server_event *event, uint8_t seat, bool reliable, qa_error *error)
{
    qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
    uint8_t *data = malloc(status.capacity);
    if (!data) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 service encoding");
    qa_net_writer writer; qa_net_writer_init(&writer, data, status.capacity, error);
    bool ok = qa_q2_server_event_write(&session->codec, &writer, event) &&
        q2_queue_bytes(session, (qa_bytes){data, qa_net_writer_size(&writer)}, seat, reliable, error);
    free(data); return ok;
}

static bool receive(void *state, qa_network_runtime *runtime, qa_net_client_id id, const qa_net_datagram *packet, qa_error *error)
{
    q2_session *session = state;
    if (!session->server && session->state.client.policy.messages.demo)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Recorded Q2 CLIENT accepts only its demo file receiver");
    qa_q2_received received;
    if (!qa_q2_channel_receive(session->channel, packet->payload, packet->received_ns, &received, error)) return false;
    if (received.kind == QA_Q2_REJECTED) return true;
    if (!qa_network_received(runtime, id, packet->received_ns, error)) return false;
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    if (session->server && !qa_net_address_equal(&client->endpoint, &packet->from, true) &&
        !qa_net_connections_rebind(runtime->connections, id, &packet->from, error)) return false;
    if (session->retiring) return true;
    if (received.kind == QA_Q2_FRAGMENT) return true;
    if (session->server) {
        session->state.server.dropped = received.dropped;
        qa_error boundary = {0};
        bool ok = qa_q2_client_messages_read(&session->codec, received.payload, received.sequence, session->seats,
            q2_server_record, session, &boundary);
        if (!ok && (boundary.code == QA_ERROR_FORMAT || boundary.code == QA_ERROR_UNSUPPORTED) && !session->retiring)
            return qa_network_q2_server_drop(runtime, id, boundary.message, packet->received_ns, error);
        if (!ok && error) *error = boundary;
        return ok;
    }
    return q2_client_receive(session, received.payload, received.acknowledged, packet->received_ns, error);
}
static bool flush(void *state, qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *error)
{
    (void)runtime; (void)id;
    q2_session *session = state;
    if (session->retiring) {
        if (session->server && session->state.server.drop_reason) {
            bool complete;
            return q2_server_drop_progress(session, now, &complete, error);
        }
        if (!session->server && session->state.client.drop_reason)
            return q2_client_drop_progress(session, NULL, error);
        return true;
    }
    if (!session->server) return session->state.client.policy.messages.demo || q2_client_send(session, now, error);
    return !qa_q2_channel_should_update(session->channel, now) || q2_send(session, (qa_bytes){0}, now, NULL, error);
}
static bool command(void *state, const qa_network_command *command, qa_error *error)
{
    q2_session *session = state;
    return !session->server ? (session->state.client.policy.messages.demo || q2_client_submit(session, command, error)) :
        q2_fail(error, QA_ERROR_ARGUMENT, "Host Q2 input belongs to the actual Source player owner");
}
static bool restart(void *state, uint64_t epoch, const qa_sha256_digest *composition, qa_error *error)
{
    (void)epoch; (void)composition;
    q2_session *session = state;
    return session->server ? q2_server_restart(session, error) : q2_client_restart(session, error);
}
static bool rebind(void *state, const qa_net_address *endpoint, qa_error *error)
{
    q2_session *session = state;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    return client && qa_net_address_equal(&client->endpoint, endpoint, true) ? true :
        q2_fail(error, QA_ERROR_ARGUMENT, "Q2 rebind lacks its authenticated connection endpoint");
}
static bool pending(const void *state)
{
    const q2_session *session = state;
    return !session->server && !session->retiring &&
        (session->state.client.receive_held || session->state.client.preparation_held || session->state.client.sent_pending);
}
static void close_session(void *state)
{
    q2_session *session = state;
    if (session->server) q2_server_clear(&session->state.server);
    else q2_client_clear(&session->state.client);
    qa_q2_channel_destroy(session->channel); free(session);
}
const qa_network_peer_ops qa_network_q2_peer_ops = {
    .receive = receive, .flush = flush, .command = command, .restart = restart,
    .rebind = rebind, .close = close_session, .receive_pending = pending
};

static bool attach(qa_network_runtime *runtime, const qa_net_connect *request, q2_session *session,
    const qa_q2_channel_options *channel, uint64_t now, qa_net_client_id *out, qa_error *error)
{
    if (!runtime || !request || !out || !qa_network_callbacks_idle(runtime) || !request->seat_count ||
        request->seat_count > QA_NETWORK_MAX_SEATS || request->protocol.kind != channel->protocol.kind ||
        request->protocol.revision != channel->protocol.revision || request->protocol.flags != channel->protocol.flags ||
        (request->seat_count != 1 && channel->protocol.kind != QA_NET_Q2KEX_2023 &&
            channel->protocol.kind != QA_NET_Q2KEX_DEMO_2022))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 attachment does not match the admitted dialect and seats");
    session->runtime = runtime; session->seats = request->seat_count;
    if (!qa_q2_codec_init(&session->codec, request->protocol, error) ||
        ((session->server || !session->state.client.policy.messages.demo) &&
            !qa_q2_channel_create(channel, &session->channel, error))) return false;
    if (!qa_network_attach(runtime, request, &qa_network_q2_peer_ops, session, now, out, error)) return false;
    session->id = *out; return true;
}
bool qa_network_attach_q2_server(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_q2_server_policy *policy, const qa_network_q2_server_hooks *hooks,
    uint64_t now, qa_net_client_id *out, qa_error *error)
{
    if (!policy || !q2_server_hooks_valid(hooks) || !policy->channel.server || !policy->history_capacity || !policy->max_clients ||
        !policy->source_interval_ns || policy->source_interval_ns > UINT64_C(1000000000))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 host lacks its complete Source consumers");
    q2_session *session = calloc(1, sizeof(*session));
    if (!session) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 host session");
    session->server = true; session->state.server.hooks = *hooks; session->state.server.policy = *policy;
    session->state.server.settings = (qa_q2_server_settings){.source_interval_ns = policy->source_interval_ns,
        .fps = (uint32_t)(UINT64_C(1000000000) / policy->source_interval_ns), .frame_divisor = 1};
    session->state.server.settings.values[4] = (int32_t)session->state.server.settings.fps;
    session->state.server.wire_frame = 1;
    for (size_t i = 0; i < QA_NETWORK_MAX_SEATS; ++i) qa_q2_command_replay_init(&session->state.server.replay[i]);
    bool ok = qa_q2_frame_history_create(policy->history_capacity, &session->state.server.frames, error) &&
        attach(runtime, request, session, &policy->channel, now, out, error);
    if (!ok) close_session(session);
    return ok;
}
bool qa_network_attach_q2_client(qa_network_runtime *runtime, const qa_net_connect *request,
    const qa_network_q2_client_policy *policy, const qa_network_q2_client_hooks *hooks,
    uint64_t now, qa_net_client_id *out, qa_error *error)
{
    if (!policy || !q2_client_hooks_valid(hooks) || policy->channel.server || !policy->pending_commands ||
        (policy->messages.demo && (!request || request->attachment != QA_NET_LOCAL_SEAT ||
            request->endpoint.kind != QA_NET_LOOPBACK)))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 client lacks its actual Source and content consumers");
    q2_session *session = calloc(1, sizeof(*session));
    if (!session) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 client session");
    session->state.client.hooks = *hooks; session->state.client.policy = *policy;
    session->state.client.last_frame = -1;
    bool ok = request && qa_q2_messages_create(request->protocol, &policy->messages, &session->state.client.messages, error) &&
        attach(runtime, request, session, &policy->channel, now, out, error);
    if (!ok) { close_session(session); return false; }
    if (!policy->messages.demo && !qa_network_q2_client_command(runtime, *out, "new", 0, error)) {
        qa_network_detach(runtime, *out, "Q2 initial signon failed", NULL); return false;
    }
    return true;
}
bool qa_network_q2_server_event(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_server_event *event, uint8_t seat, bool reliable, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, true, error);
    return session && event && q2_queue_event(session, event, seat, reliable, error);
}
bool qa_network_q2_server_codec(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_codec **out, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    q2_session *session = qa_network_q2_peer(peer) ? peer->state : NULL;
    if (!session || !session->server || !out)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 outbound codec has no actual server connection");
    *out = &session->codec; return true;
}
bool qa_network_q2_server_bytes(qa_network_runtime *runtime, qa_net_client_id id,
    qa_bytes bytes, uint8_t seat, bool reliable, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, true, error);
    return session && q2_queue_bytes(session, bytes, seat, reliable, error);
}
bool qa_network_q2_server_command(qa_network_runtime *runtime, qa_net_client_id id,
    uint8_t seat, const char *text, qa_error *error)
{
    qa_q2_server_event event = {.kind = QA_Q2_SVC_COMMAND, .data.print = {.text = text}};
    return qa_network_q2_server_event(runtime, id, &event, seat, true, error);
}
bool qa_network_q2_state_read(qa_network_runtime *runtime, qa_net_client_id id,
    qa_network_q2_state *out, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!out || !qa_network_q2_peer(peer)) return q2_fail(error, QA_ERROR_ARGUMENT, "Missing Q2 state owner");
    q2_session *session = peer->state;
    *out = (qa_network_q2_state){.server = session->server, .active = session->active,
        .retiring = session->retiring, .receive_pending = pending(session)};
    if (session->server) { out->server_count = session->state.server.policy.server_count; out->acknowledged_frame = session->state.server.replay[0].last_frame; }
    else { q2_client *client = &session->state.client; out->server_count = client->server_data.servercount;
        out->acknowledged_frame = client->last_frame; out->loading_generation = client->loading_generation;
        out->pending_commands = client->command_count; out->pending_records = client->batch.count - client->batch.cursor;
        out->preparing = client->selecting_server_data || client->preparing_game_state; }
    return !session->server && session->state.client.policy.messages.demo ? true :
        qa_q2_channel_get_status(session->channel, &out->channel);
}
