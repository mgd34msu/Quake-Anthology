#include "session_internal.h"
#include "frames_internal.h"
#include "channel_internal.h"
#include "../connections_internal.h"
#include <errno.h>
#include <stdio.h>
#include <limits.h>

static const char *token(const qa_command_tokens *tokens, size_t index)
{
    return index < tokens->count ? tokens->values[index] : "";
}
static bool signon_integer(const char *text, int64_t *out, qa_error *error)
{
    const char *digits = text; if (*digits == '-') ++digits;
    if (!*digits) return q2_fail(error, QA_ERROR_FORMAT, "Q2 signon number is absent");
    for (const char *p = digits; *p; ++p)
        if (*p < '0' || *p > '9') return q2_fail(error, QA_ERROR_FORMAT, "Q2 signon number is not decimal");
    errno = 0; char *end; long long value = strtoll(text, &end, 10);
    if (errno == ERANGE || *end || value < -INT64_C(9007199254740991) || value > INT64_C(9007199254740991))
        return q2_fail(error, QA_ERROR_FORMAT, "Q2 signon number exceeds its Source range");
    *out = (int64_t)value; return true;
}
static bool stuff_write(qa_q2_codec *codec, qa_net_writer *writer, int32_t server_count,
    const char *name, int32_t start, bool page)
{
    char text[96];
    if (page) snprintf(text, sizeof(text), "cmd %s %d %d\n", name, server_count, start);
    else snprintf(text, sizeof(text), "%s %d\n", name, server_count);
    qa_q2_server_event event = {.kind = QA_Q2_SVC_COMMAND, .data.print = {.text = text}};
    return qa_q2_server_event_write(codec, writer, &event);
}
static bool new_client(q2_session *session, qa_error *error)
{
    q2_server *server = &session->state.server;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 signon lost its physical connection");
    bool restart = client->phase != QA_NET_CONNECTED;
    uint64_t composition = client->composition;
    if (restart && !qa_net_connections_restart_ready(session->runtime->connections,
        session->id, &composition, error)) return false;
    qa_q2_game_state borrowed = {0};
    if (!server->hooks.game_state(server->hooks.context, session->id, &borrowed, error)) return false;
    q2_game_state state = {0};
    if (!q2_game_state_clone(&borrowed, &state, error)) return false;
    state.view.data.servercount = server->policy.server_count;
    if (session->codec.protocol.kind == QA_NET_Q2KEX_2023) {
        state.view.data.client_count = session->seats;
        client = qa_net_connections_get(session->runtime->connections, session->id);
        for (size_t i = 0; i < session->seats; ++i) {
            qa_network_q2_player physical;
            if (!server->hooks.player(server->hooks.context, session->id, client->seats[i].seat, &physical, error)) {
                q2_game_state_free(&state); return false;
            }
            if (!physical.source_owner || !physical.source_slot || physical.source_slot > INT32_MAX) {
                q2_game_state_free(&state); return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 signon lacks its physical Source edict");
            }
            state.view.data.clientnums[i] = (int32_t)physical.source_slot - 1;
        }
        state.view.data.clientnum = state.view.data.clientnums[0];
    }
    qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
    uint8_t *bytes = malloc(status.capacity);
    if (!bytes) { q2_game_state_free(&state); return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 initial signon"); }
    qa_net_writer writer; qa_net_writer_init(&writer, bytes, status.capacity, error);
    qa_q2_codec codec = session->codec;
    codec.server_clientnum = state.view.data.clientnum;
    codec.has_server_clientnum = true;
    qa_q2_server_event event = {.kind = QA_Q2_SVC_SERVERDATA, .data.serverdata = state.view.data};
    bool ok = qa_q2_server_event_write(&codec, &writer, &event) &&
        stuff_write(&codec, &writer, server->policy.server_count, "configstrings", 0, true) &&
        q2_queue_bytes(session, (qa_bytes){bytes, qa_net_writer_size(&writer)}, 0, true, error);
    free(bytes);
    if (!ok) { q2_game_state_free(&state); return false; }
    if (restart) {
        qa_net_connections_restart_commit(session->runtime->connections, session->id, &composition);
        qa_network_history_clear(&session->runtime->peers[session->id.slot]);
    }
    q2_download_close(server); session->active = false;
    qa_q2_frame_history_clear(server->frames); qa_buffer_free(&server->datagram);
    server->has_source_frame = false; server->wire_frame = 1;
    for (size_t i = 0; i < session->seats; ++i) qa_q2_command_replay_init(&server->replay[i]);
    q2_game_state_free(&server->signon); server->signon = state; server->signon_started = true;
    session->codec = codec;
    if (server->hooks.game_state_accepted)
        server->hooks.game_state_accepted(server->hooks.context, session->id);
    return true;
}
static bool page(q2_session *session, bool configs, int64_t start, qa_error *error)
{
    q2_server *server = &session->state.server;
    if (!server->signon_started) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 signon request has no physical game state");
    if (start < 0) return q2_fail(error, QA_ERROR_FORMAT, "Q2 signon request has a negative record index");
    qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
    if (status.capacity <= 96) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 signon channel cannot hold its continuation");
    size_t limit = status.capacity - 96;
    uint8_t *bytes = malloc(status.capacity * 2);
    if (!bytes) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 signon page");
    qa_net_writer packet; qa_net_writer_init(&packet, bytes, status.capacity, error);
    qa_q2_codec codec = session->codec;
    size_t count = configs ? server->signon.view.config_count : server->signon.view.baselines.count;
    size_t i = 0;
    while (i < count && (configs ? (uint64_t)server->signon.configs[i].index : server->signon.baselines[i].number) < (uint64_t)start) ++i;
    bool ok = true;
    for (; i < count; ++i) {
        qa_q2_server_event event = {.kind = configs ? QA_Q2_SVC_CONFIGSTRING : QA_Q2_SVC_BASELINE};
        if (configs) { event.data.config.index = server->signon.configs[i].index; event.data.config.value = server->signon.configs[i].value; }
        else event.data.baseline = server->signon.baselines[i];
        qa_q2_codec next_codec = codec;
        qa_net_writer writer; qa_net_writer_init(&writer, bytes + status.capacity, status.capacity, error);
        if (!qa_q2_server_event_write(&next_codec, &writer, &event)) { ok = false; break; }
        size_t length = qa_net_writer_size(&writer);
        if (length > limit) { ok = q2_fail(error, QA_ERROR_ARGUMENT, "A Q2 signon record exceeds the channel page"); break; }
        if (length > limit - qa_net_writer_size(&packet)) break;
        if (!qa_net_write_data(&packet, bytes + status.capacity, length)) { ok = false; break; }
        codec = next_codec;
    }
    if (ok && i < count) {
        int32_t next = (int32_t)(configs ? server->signon.configs[i].index : server->signon.baselines[i].number);
        ok = stuff_write(&codec, &packet, server->policy.server_count,
            configs ? "configstrings" : "baselines", next, true);
    } else if (ok) ok = stuff_write(&codec, &packet, server->policy.server_count,
        configs ? "baselines" : "precache", 0, configs);
    if (ok) ok = q2_queue_bytes(session, (qa_bytes){bytes, qa_net_writer_size(&packet)}, 0, true, error);
    free(bytes);
    if (ok) session->codec = codec;
    return ok;
}
static bool begin(q2_session *session, qa_error *error)
{
    if (session->active) return true;
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client || !session->state.server.signon_started) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 begin precedes physical signon");
    q2_server *server = &session->state.server;
    for (size_t i = 0; i < session->seats; ++i)
        if (!server->hooks.begin(server->hooks.context, session->id, client->seats[i].seat, error)) return false;
    if (!qa_network_phase(session->runtime, session->id, QA_NET_PRIMED, error) ||
        !qa_network_phase(session->runtime, session->id, QA_NET_ACTIVE, error)) return false;
    session->active = true; return true;
}
static bool string_command(q2_session *session, uint8_t seat, const char *text, qa_error *error)
{
    qa_buffer expanded = {0}; q2_server *server = &session->state.server;
    if (!server->hooks.expand_command(server->hooks.context, session->id, text, &expanded, error)) {
        qa_buffer_free(&expanded); return false;
    }
    if (!expanded.data && !expanded.size) return true;
    if (!expanded.data || !expanded.size || expanded.data[expanded.size - 1] || memchr(expanded.data, 0, expanded.size - 1)) {
        qa_buffer_free(&expanded); return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source expansion returned an invalid command string");
    }
    text = (const char *)expanded.data;
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, QA_RULESET_Q2_CLASSIC, false, &tokens, error)) { qa_buffer_free(&expanded); return false; }
    const char *name = token(&tokens, 0); bool ok = true;
    if (!strcmp(name, "disconnect")) {
        ok = q2_server_drop_request(session, "client disconnected", error);
        if (ok) {
            bool complete;
            server->drop_notice = false;
            ok = q2_server_drop_progress(session, session->runtime->now_ns, &complete, error);
        }
    } else if (!strcmp(name, "new")) ok = new_client(session, error);
    else if (!strcmp(name, "download")) ok = q2_download_begin(session, token(&tokens, 1), token(&tokens, 2), error);
    else if (!strcmp(name, "nextdl")) ok = q2_download_next(session, error);
    else if (!strcmp(name, "configstrings") || !strcmp(name, "baselines") || !strcmp(name, "begin")) {
        int64_t generation = 0, start = 0;
        if (!server->signon_started) ok = new_client(session, error);
        else if (signon_integer(token(&tokens, 1), &generation, error)) {
            if (generation != server->policy.server_count) ok = new_client(session, error);
            else if (!strcmp(name, "begin")) ok = begin(session, error);
            else ok = signon_integer(token(&tokens, 2), &start, error) && page(session, !strcmp(name, "configstrings"), start, error);
        } else ok = false;
    } else if (session->active && tokens.count) {
        const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
        ok = client && session->state.server.hooks.command(session->state.server.hooks.context,
            session->id, client->seats[seat].seat, text, error);
    }
    qa_command_tokens_free(&tokens); qa_buffer_free(&expanded); return ok;
}
static bool think(void *context, const qa_q2_usercmd *command, qa_error *error)
{
    q2_session *session = context; q2_server *server = &session->state.server;
    qa_network_peer *peer = qa_network_peer_get(session->runtime, session->id, error);
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!peer || !client || client->phase != QA_NET_ACTIVE || server->reading_seat >= session->seats)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 input lost its admitted Source seat");
    size_t index = server->reading_seat; qa_network_seat *seat = &peer->seats[index];
    if (seat->applying || server->source_sequence[index] == UINT64_MAX)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source input counter is exhausted or recursively applying");
    qa_network_q2_player physical = {0};
    if (!server->hooks.player(server->hooks.context, session->id, seat->id, &physical, error)) return false;
    if (!physical.actor.registry || !physical.source_owner || !physical.source_slot)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 input lacks a physical Source player receipt");
    seat->applying = true;
    bool ok = session->runtime->options.hooks.controlled(session->runtime->options.hooks.context,
        session->id, seat->id, physical.actor, physical.movement, (qa_bytes){0}, error);
    uint64_t sequence = server->source_sequence[index];
    if (ok) {
        ++server->source_sequence[index];
        ok = server->hooks.input(server->hooks.context, session->id, seat->id, &physical, command, sequence, error);
    }
    seat->applying = false;
    if (ok) { seat->accepted = sequence; seat->has_accepted = true; }
    return ok;
}
bool q2_server_record(void *context, const qa_q2_client_record *record, qa_error *error)
{
    q2_session *session = context; q2_server *server = &session->state.server;
    if (session->retiring) return true;
    if (record->seat >= session->seats) return q2_fail(error, QA_ERROR_FORMAT, "Q2 command names an unowned Source seat");
    const qa_net_client *client = qa_net_connections_get(session->runtime->connections, session->id);
    if (!client) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 command lost its physical client");
    const qa_q2_client_event *event = &record->event;
    switch (event->kind) {
    case QA_Q2_CLC_NOP: return true;
    case QA_Q2_CLC_COMMAND: return string_command(session, record->seat, event->data.text, error);
    case QA_Q2_CLC_USERINFO:
        return q2_server_userinfo(session, event->data.text, error);
    case QA_Q2_CLC_USERINFO_DELTA: return q2_server_userinfo_delta(session,
        event->data.userinfo_delta.name, event->data.userinfo_delta.value, error);
    case QA_Q2_CLC_SETTING: return q2_server_setting(session, event->data.setting.index, event->data.setting.value, error);
    case QA_Q2_CLC_MOVE: case QA_Q2_CLC_BATCH:
        if (!session->active) return true;
        server->reading_seat = record->seat;
        return qa_q2_command_replay_run(&server->replay[record->seat], event, server->dropped, think, session, error);
    }
    return q2_fail(error, QA_ERROR_FORMAT, "Unknown Q2 Source command");
}
bool qa_network_q2_server_frame(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_wire_frame *frame, const qa_q2_source_motion *motion, uint64_t now, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, true, error);
    if (!session || !frame || !frame->valid || frame->player_count != session->seats)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 frame lacks its full physical players");
    q2_server *server = &session->state.server;
    if (!session->active) return true;
    uint64_t source_frame = motion ? motion->source_frame : (uint32_t)frame->server_frame;
    if (motion && (uint32_t)motion->source_frame != (uint32_t)frame->server_frame)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 wire frame differs from its actual Source counter");
    if (motion) {
        const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
        qa_network_q2_player player;
        if (!client || !session->state.server.hooks.player(session->state.server.hooks.context, id,
                client->seats[0].seat, &player, error)) return false;
        if (!motion->source_owner || player.source_owner != motion->source_owner)
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 motion history belongs to another physical Source");
    }
    if (server->has_source_frame && server->last_source_frame == source_frame) return true;
    bool first = !server->has_source_frame;
    if (!first && source_frame < server->last_source_frame)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source frame moved backward without its actual restart");
    int32_t wire_frame = server->wire_frame;
    if (session->codec.protocol.kind == QA_NET_Q2PRO_36) {
        if (first) {
            int32_t previous = server->wire_frame;
            if (!q2_server_align(session, source_frame, error)) return false;
            wire_frame = server->wire_frame;
            server->wire_frame = previous;
        } else {
            uint64_t missed = (source_frame - 1) / server->settings.frame_divisor -
                server->last_source_frame / server->settings.frame_divisor;
            if (missed > (uint64_t)(INT32_MAX - server->wire_frame))
                return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source interval exhausts its client wire counter");
            wire_frame += (int32_t)missed;
        }
    }
    if (server->settings.frame_divisor > 1 && source_frame % server->settings.frame_divisor) {
        server->has_source_frame = true; server->last_source_frame = source_frame;
        server->wire_frame = wire_frame; return true;
    }
    if (wire_frame == INT32_MAX) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 wire frame counter is exhausted");
    if (qa_q2_channel_fragment_pending(session->channel)) {
        bool sent = q2_send(session, (qa_bytes){0}, now, NULL, error);
        if (sent) {
            server->has_source_frame = true; server->last_source_frame = source_frame;
            server->wire_frame = wire_frame + 1;
        }
        return sent;
    }
    const qa_q2_wire_frame *old = server->replay[0].last_frame > 0 ?
        qa_q2_frame_history_get(server->frames, server->replay[0].last_frame) : NULL;
    qa_q2_wire_frame wire = {0};
    if (!q2_server_projection(session, frame, old, motion, &wire, error)) return false;
    if (session->codec.protocol.kind == QA_NET_Q2PRO_36) wire.server_frame = wire_frame;
    qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
    uint8_t *data = malloc(status.capacity);
    if (!data) { qa_q2_frame_free(&wire); return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 physical frame encoding"); }
    qa_net_writer writer; qa_net_writer_init(&writer, data, status.capacity, error);
    bool ok = qa_q2_frame_write(&session->codec, &writer, &wire, old, server->signon.view.baselines,
        server->policy.max_clients) && qa_net_write_data(&writer, server->datagram.data, server->datagram.size);
    bool included = false;
    if (ok) ok = q2_send(session, (qa_bytes){data, qa_net_writer_size(&writer)}, now, &included, error);
    free(data);
    if (ok && included) {
        qa_buffer_free(&server->datagram);
        qa_q2_frame_history_store_owned(server->frames, &wire);
        server->has_source_frame = true; server->last_source_frame = source_frame;
        server->wire_frame = wire_frame + 1;
    }
    qa_q2_frame_free(&wire);
    return ok;
}
bool q2_server_drop_progress(q2_session *session, uint64_t now, bool *complete, qa_error *error)
{
    q2_server *server = &session->state.server;
    *complete = false;
    if (server->drop_notice && !server->drop_queued) {
        size_t length = strlen(server->drop_reason);
        if (length > SIZE_MAX - 2) return q2_fail(error, QA_ERROR_MEMORY, "Q2 disconnect text extent overflows");
        char *text = malloc(length + 2);
        uint8_t *bytes = malloc(session->channel->capacity);
        if (!text || !bytes) { free(text); free(bytes); return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 server disconnect packet"); }
        memcpy(text, server->drop_reason, length); text[length] = '\n'; text[length + 1] = 0;
        qa_q2_server_event print = {.kind = QA_Q2_SVC_PRINT, .data.print = {.level = 2, .text = text}};
        qa_q2_server_event disconnect = {.kind = QA_Q2_SVC_DISCONNECT};
        qa_net_writer writer; qa_net_writer_init(&writer, bytes, session->channel->capacity, error);
        bool ok = qa_q2_server_event_write(&session->codec, &writer, &print) &&
            qa_q2_server_event_write(&session->codec, &writer, &disconnect) &&
            q2_queue_bytes(session, (qa_bytes){bytes, qa_net_writer_size(&writer)}, 0, true, error);
        free(text); free(bytes);
        if (!ok) return false;
        server->drop_queued = true;
    }
    if (server->drop_notice && !server->drop_sent) {
        if (!q2_send(session, (qa_bytes){0}, now, NULL, error)) return false;
        if (session->channel->queued_size || session->channel->sending_size) return true;
        server->drop_sent = true;
    }
    if (!server->drop_hook_done) {
        if (!server->hooks.drop(server->hooks.context, session->id, server->drop_reason, error)) return false;
        server->drop_hook_done = true;
    }
    *complete = true; return true;
}
bool q2_server_drop_request(q2_session *session, const char *reason, qa_error *error)
{
    q2_server *server = &session->state.server;
    if (session->retiring && !server->drop_reason) return true;
    if (!server->drop_reason) {
        if (!reason) reason = "server disconnected";
        size_t length = strlen(reason);
        if (length == SIZE_MAX) return q2_fail(error, QA_ERROR_MEMORY, "Q2 disconnect reason extent overflows");
        server->drop_reason = malloc(length + 1);
        if (!server->drop_reason) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 disconnect source reason");
        memcpy(server->drop_reason, reason, length + 1);
        server->drop_notice = true;
        session->retiring = true; session->active = false; q2_download_close(server);
    }
    return true;
}
bool qa_network_q2_server_request_drop(qa_network_runtime *runtime, qa_net_client_id id,
    const char *reason, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!qa_network_q2_peer(peer) || !((q2_session *)peer->state)->server)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 disconnect lost its exact hosted channel");
    return q2_server_drop_request(peer->state, reason, error);
}
bool qa_network_q2_server_drop(qa_network_runtime *runtime, qa_net_client_id id,
    const char *reason, uint64_t now, qa_error *error)
{
    if (!qa_network_q2_server_request_drop(runtime, id, reason, error)) return false;
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    q2_session *session = peer->state;
    if (!session->state.server.drop_reason) return true;
    bool complete;
    if (!q2_server_drop_progress(session, now, &complete, error)) return false;
    return complete;
}
bool q2_server_restart(q2_session *session, qa_error *error)
{
    q2_server *server = &session->state.server;
    if (server->policy.server_count == INT32_MAX) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 server generation exhausted");
    qa_q2_server_event changing = {.kind = QA_Q2_SVC_COMMAND, .data.print = {.text = "changing\n"}};
    qa_q2_server_event reconnect = {.kind = QA_Q2_SVC_RECONNECT};
    uint8_t packet[64]; qa_net_writer writer;
    qa_net_writer_init(&writer, packet, sizeof(packet), error);
    qa_q2_codec codec = session->codec;
    if (!qa_q2_server_event_write(&codec, &writer, &changing) ||
        !qa_q2_server_event_write(&codec, &writer, &reconnect) ||
        !q2_queue_bytes(session, (qa_bytes){packet, qa_net_writer_size(&writer)}, 0, true, error)) return false;
    session->codec = codec;
    ++server->policy.server_count; session->active = false; server->signon_started = false;
    q2_download_close(server); q2_game_state_free(&server->signon); qa_q2_frame_history_clear(server->frames);
    qa_buffer_free(&server->datagram);
    server->has_source_frame = false; server->wire_frame = 1;
    for (size_t i = 0; i < session->seats; ++i) qa_q2_command_replay_init(&server->replay[i]);
    return true;
}
void q2_server_clear(q2_server *server)
{
    q2_download_close(server); q2_game_state_free(&server->signon);
    qa_q2_frame_history_destroy(server->frames); qa_buffer_free(&server->datagram);
    free(server->drop_reason);
}
