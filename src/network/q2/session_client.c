#include "session_internal.h"
#include "messages_internal.h"
#include "channel_internal.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>

static bool current(q2_session *session, qa_error *error)
{
    return !session->retiring && session->state.client.hooks.current(
        session->state.client.hooks.context, session->id, error);
}
static bool sent_continue(q2_session *session, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (!client->sent_pending) return true;
    while (client->sent_cursor < session->seats) {
        size_t seat = client->sent_cursor;
        const qa_net_client *connection = qa_net_connections_get(session->runtime->connections, session->id);
        if (!connection || !current(session, error) ||
            !client->hooks.sent(client->hooks.context, session->id, connection->seats[seat].seat,
                client->sent_sequence, client->sent.number, &client->sent.commands[seat], client->sent_ns, error) ||
            !current(session, error)) return false;
        client->oldest[seat] = client->previous[seat];
        client->previous[seat] = client->sent.commands[seat];
        ++client->sent_cursor;
    }
    client->sent = (q2_command_group){0}; client->sent_sequence = 0;
    client->sent_ns = 0; client->sent_cursor = 0; client->sent_pending = false;
    return true;
}
static bool cancel(q2_session *session, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (client->loading_generation == UINT64_MAX) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 loading generation exhausted");
    if (!current(session, error) || !client->hooks.cancel_loading(client->hooks.context, session->id, error) ||
        !current(session, error)) return false;
    ++client->loading_generation; client->selecting_server_data = false; client->preparing_game_state = false; client->preparation_held = false;
    q2_game_state_free(&client->preparing); return true;
}
static bool retire_client(q2_session *session, const char *reason, bool notice, bool notify,
    bool records, uint64_t now, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (!client->drop_reason && !sent_continue(session, error)) return false;
    if (!client->drop_reason) {
        if (!reason) reason = "connection timed out";
        size_t length = strlen(reason);
        if (length == SIZE_MAX) return q2_fail(error, QA_ERROR_MEMORY, "Q2 CLIENT retirement reason extent overflows");
        size_t size = length + 1;
        client->drop_reason = malloc(size);
        if (!client->drop_reason) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 CLIENT timeout reason");
        memcpy(client->drop_reason, reason, size);
        client->drop_notice = notice; client->drop_notify = notify; client->drop_records_needed = records;
        session->retiring = true; session->active = false;
    }
    bool classic = session->codec.protocol.kind == QA_NET_Q2_34;
    if (client->policy.messages.demo) client->drop_notice = false;
    if (client->drop_notice && classic && !client->drop_sent) {
        uint8_t bytes[32];
        qa_q2_client_event disconnect = {.kind = QA_Q2_CLC_COMMAND, .data.text = "disconnect"};
        qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        if (!qa_q2_client_event_write(&session->codec, &writer, &disconnect, 0)) return false;
        while (client->drop_transmissions < 3) {
            if (!q2_send(session, (qa_bytes){bytes, qa_net_writer_size(&writer)}, now, NULL, error)) return false;
            ++client->drop_transmissions;
        }
        client->drop_sent = true;
    }
    if (client->drop_notice && !classic && !client->drop_queued) {
        uint8_t *bytes = malloc(session->channel->capacity);
        if (!bytes) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 CLIENT disconnect command");
        qa_q2_client_event disconnect = {.kind = QA_Q2_CLC_COMMAND, .data.text = "disconnect"};
        qa_net_writer writer; qa_net_writer_init(&writer, bytes, session->channel->capacity, error);
        bool ok = qa_q2_client_event_write(&session->codec, &writer, &disconnect, 0) &&
            qa_q2_channel_queue(session->channel, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
        free(bytes); if (!ok) return false;
        client->drop_queued = true;
    }
    if (client->drop_notice && !classic && !client->drop_sent) {
        if (!q2_send(session, (qa_bytes){0}, now, NULL, error)) return false;
        if (session->channel->queued_size || session->channel->sending_size) return false;
        client->drop_sent = true;
    }
    if (!client->drop_canceled) {
        if (client->loading_generation == UINT64_MAX)
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 loading generation exhausted during timeout");
        if (!client->hooks.current(client->hooks.context, session->id, error) ||
            !client->hooks.cancel_loading(client->hooks.context, session->id, error) ||
            !client->hooks.current(client->hooks.context, session->id, error)) return false;
        ++client->loading_generation;
        client->selecting_server_data = false; client->preparing_game_state = false; client->preparation_held = false;
        q2_game_state_free(&client->preparing); client->drop_canceled = true;
    }
    if (client->drop_records_needed && !client->drop_records_done) {
        if (!client->hooks.records(client->hooks.context, session->id, client->batch.records,
            client->batch.count, error)) return false;
        client->drop_records_done = true;
    }
    if (!client->drop_hook_done) {
        if (client->drop_notify && !client->hooks.drop(client->hooks.context, session->id, client->drop_reason, error)) return false;
        client->drop_hook_done = true;
    }
    q2_records_free(&client->batch); client->receive_held = false; client->acknowledgement_held = false;
    return true;
}
bool q2_client_drop_progress(q2_session *session, const char *reason, qa_error *error)
{ return retire_client(session, reason, false, true, false, session->runtime->now_ns, error); }
static bool loading(q2_session *session, qa_error *error)
{
    const qa_net_client *connection = qa_net_connections_get(session->runtime->connections, session->id);
    if (!connection) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 loading lost its admitted connection");
    if (connection->phase != QA_NET_CONNECTED) {
        uint64_t composition = connection->composition;
        if (!qa_net_connections_restart(session->runtime->connections, session->id, &composition, error)) return false;
        qa_network_history_clear(&session->runtime->peers[session->id.slot]);
    }
    session->active = false; session->state.client.last_frame = -1; return true;
}
bool qa_network_q2_client_control(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_client_event *event, uint8_t seat, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    if (!session || !event || seat >= session->seats) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 control lacks its admitted seat");
    if (session->state.client.policy.messages.demo) return true;
    qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
    uint8_t *data = malloc(status.capacity);
    if (!data) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 client control");
    qa_net_writer writer; qa_net_writer_init(&writer, data, status.capacity, error);
    bool ok = qa_q2_client_event_write(&session->codec, &writer, event, seat) &&
        qa_q2_channel_queue(session->channel, (qa_bytes){data, qa_net_writer_size(&writer)}, error);
    free(data); return ok;
}
bool qa_network_q2_client_command(qa_network_runtime *runtime, qa_net_client_id id,
    const char *text, uint8_t seat, qa_error *error)
{
    qa_q2_client_event event = {.kind = QA_Q2_CLC_COMMAND, .data.text = text};
    return qa_network_q2_client_control(runtime, id, &event, seat, error);
}
static bool prepare(q2_session *session, bool *waiting, qa_error *error)
{
    q2_client *client = &session->state.client; qa_q2_preparation result = QA_Q2_PREPARATION_WAITING;
    if (!current(session, error)) return false;
    if (client->selecting_server_data) {
        if (!client->hooks.server_data(client->hooks.context, session->id, client->loading_generation,
            &client->server_data, &result, error) || !current(session, error)) return false;
        if (result == QA_Q2_PREPARATION_WAITING) { client->preparation_held = true; *waiting = true; return true; }
        if (result == QA_Q2_PREPARATION_RECEIVING) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 directory selection cannot await game-channel downloads");
        client->selecting_server_data = false;
    } else if (client->preparing_game_state) {
        if (!client->hooks.prepare(client->hooks.context, session->id, client->loading_generation,
            &client->preparing.view, &result, error) || !current(session, error)) return false;
        if (result == QA_Q2_PREPARATION_WAITING) { client->preparation_held = true; *waiting = true; return true; }
        if (result == QA_Q2_PREPARATION_RECEIVING) { client->preparation_held = false; *waiting = false; return true; }
        client->preparing_game_state = false;
        if (result == QA_Q2_PREPARATION_READY) {
            char text[64]; snprintf(text, sizeof(text), "begin %d", client->server_data.servercount);
            if (!qa_network_q2_client_command(session->runtime, session->id, text, 0, error) ||
                !qa_network_phase(session->runtime, session->id, QA_NET_PRIMED, error) ||
                !qa_network_phase(session->runtime, session->id, QA_NET_ACTIVE, error)) return false;
            session->active = true;
        }
        q2_game_state_free(&client->preparing);
    }
    if ((unsigned)result > QA_Q2_PREPARATION_RECEIVING) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source preparation returned an invalid state");
    if (result == QA_Q2_PREPARATION_CANCELED) {
        if (!retire_client(session, "loading canceled", false, true, false, session->runtime->now_ns, error)) return false;
    }
    client->preparation_held = false; *waiting = false; return true;
}
static bool retain_game_state(q2_session *session, qa_error *error)
{
    q2_client *client = &session->state.client; qa_q2_messages *messages = client->messages;
    size_t count = 0;
    for (size_t i = 0; i < messages->config_capacity; ++i) if (messages->configs[i]) ++count;
    qa_q2_config_entry *entries = count ? malloc(count * sizeof(*entries)) : NULL;
    if (count && !entries) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 streamed configuration");
    size_t index = 0;
    for (size_t i = 0; i < messages->config_capacity; ++i)
        if (messages->configs[i]) entries[index++] = (qa_q2_config_entry){(uint16_t)i, messages->configs[i]};
    qa_q2_game_state state = {.data = client->server_data, .configs = entries, .config_count = count,
        .baselines = {messages->baselines, messages->baseline_count}};
    bool ok = q2_game_state_clone(&state, &client->preparing, error); free(entries);
    if (ok) client->preparing_game_state = true;
    return ok;
}
static bool server_command(q2_session *session, uint8_t source_seat, const char *text, bool *waiting, qa_error *error)
{
    q2_client *client = &session->state.client;
    size_t text_length = strlen(text);
    if (client->command_offset > text_length) return q2_fail(error, QA_ERROR_FORMAT, "Q2 retained command cursor exceeds its record");
    const char *at = text + client->command_offset;
    while (*at) {
        const char *end = at; while (*end && *end != '\n' && *end != ';') ++end;
        size_t length = (size_t)(end - at); char *line = malloc(length + 1);
        if (!line) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 signon command");
        memcpy(line, at, length); line[length] = 0;
        qa_command_tokens tokens = {0};
        bool ok = qa_command_tokenize(line, QA_CONSOLE_Q2, false, &tokens, error);
        const char *name = tokens.count ? tokens.values[0] : "";
        if (ok && !strcmp(name, "cmd") && tokens.count >= 2 &&
            (!strcmp(tokens.values[1], "configstrings") || !strcmp(tokens.values[1], "baselines"))) {
            char *command = malloc(length + 1);
            if (!command) ok = q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 config request");
            else {
                size_t used = 0;
                for (size_t i = 1; i < tokens.count; ++i) {
                    size_t size = strlen(tokens.values[i]);
                    if (i > 1) command[used++] = ' ';
                    memcpy(command + used, tokens.values[i], size); used += size;
                }
                command[used] = 0; ok = qa_network_q2_client_command(session->runtime, session->id, command, 0, error);
                free(command);
            }
        } else if (ok && !strcmp(name, "precache")) {
            char saved_count[32];
            snprintf(saved_count, sizeof(saved_count), "%d", client->server_data.servercount);
            const char *number = tokens.count > 1 ? tokens.values[1] :
                client->policy.messages.demo ? saved_count : "", *digits = number;
            if (*digits == '-') ++digits;
            bool valid = *digits != 0;
            for (const char *p = digits; *p; ++p) if (*p < '0' || *p > '9') valid = false;
            errno = 0; char *tail; long long count = strtoll(number, &tail, 10);
            if (!client->has_server_data || !valid || *tail || errno == ERANGE || count < INT32_MIN ||
                count > INT32_MAX || count != client->server_data.servercount)
                ok = q2_fail(error, QA_ERROR_FORMAT, "Q2 precache refers to another physical server generation");
            else ok = retain_game_state(session, error) && prepare(session, waiting, error);
        } else if (ok && !strcmp(name, "changing")) ok = cancel(session, error) && loading(session, error);
        else if (ok && tokens.count) ok = current(session, error) &&
            client->hooks.server_command(client->hooks.context, session->id, source_seat, line, error) && current(session, error);
        qa_command_tokens_free(&tokens); free(line);
        if (!ok || *waiting) { client->command_offset = (size_t)((*end ? end + 1 : end) - text); return ok; }
        at = *end ? end + 1 : end;
    }
    client->command_offset = 0; return true;
}
bool q2_client_receive(q2_session *session, qa_bytes bytes, uint32_t acknowledged, uint64_t now, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (client->receive_held || client->batch.count || client->preparation_held)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 receiver has an unconsumed Source preparation");
    q2_records records = {.received_ns = now};
    if (!qa_q2_messages_read(client->messages, bytes, q2_record_retain, &records, error)) { q2_records_free(&records); return false; }
    session->codec = *qa_q2_messages_codec(client->messages);
    if (client->recording.append) {
        bool full_frame = false;
        for (size_t i = 0; i < records.count; ++i)
            if (records.records[i].event.kind == QA_Q2_SVC_FRAME &&
                records.records[i].event.data.frame->valid && records.records[i].event.data.frame->delta_frame <= 0)
                full_frame = true;
        if (!client->recording.append(client->recording.context, bytes, full_frame, error)) {
            q2_records_free(&records); return false;
        }
    }
    client->batch = records; client->acknowledged = acknowledged;
    client->receive_held = true; client->acknowledgement_held = true; return true;
}
bool qa_network_q2_client_record(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_packet_sink *sink, bool attach, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    q2_session *session = qa_network_q2_peer(peer) ? peer->state : NULL;
    if (!session || session->server || !sink || !sink->append ||
        !qa_network_callbacks_idle(runtime) || session->busy || (attach && session->retiring))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 recording requires its returned payload owner");
    qa_q2_packet_sink *held = &session->state.client.recording;
    if (attach) {
        if (held->append || session->state.client.policy.messages.demo)
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 payload recording is already owned or playing a demo");
        *held = *sink;
    } else {
        if (held->context != sink->context || held->append != sink->append)
            return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 recording detach differs from its actual sink");
        *held = (qa_q2_packet_sink){0};
    }
    return true;
}
bool qa_network_q2_client_demo_receive(qa_network_runtime *runtime, qa_net_client_id id,
    qa_bytes bytes, uint64_t now, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    if (!session || !session->state.client.policy.messages.demo ||
        !qa_network_callbacks_idle(runtime) || session->busy)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 demo requires its returned retained CLIENT receiver");
    return qa_network_received(runtime, id, now, error) && q2_client_receive(session, bytes, 0, now, error);
}
bool qa_network_q2_client_continue(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    if (!session || !qa_network_callbacks_idle(runtime) || session->busy)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 Source delivery requires its idle enclosing runtime");
    q2_client *client = &session->state.client; session->busy = true; runtime->callback = true;
    bool ok = sent_continue(session, error), waiting = false, download_retry = false;
    if (ok && client->acknowledgement_held) {
        client->acknowledgement_held = false;
        ok = client->policy.messages.demo || (current(session, error) && client->hooks.acknowledged(client->hooks.context, id,
            client->acknowledged, client->batch.received_ns, error) && current(session, error));
    }
    if (ok && (client->selecting_server_data || client->preparing_game_state)) ok = prepare(session, &waiting, error);
    while (ok && !waiting && !session->retiring && client->batch.cursor < client->batch.count) {
        const qa_q2_server_record *record = &client->batch.records[client->batch.cursor++];
        const qa_q2_server_event *event = &record->event;
        ok = current(session, error); if (!ok) break;
        switch (event->kind) {
        case QA_Q2_SVC_SERVERDATA:
            ok = cancel(session, error) && loading(session, error);
            if (ok) {
                client->server_data = event->data.serverdata; client->has_server_data = true;
                memset(client->oldest, 0, sizeof(client->oldest)); memset(client->previous, 0, sizeof(client->previous));
                client->command_count = 0; client->command_number = 0; client->selecting_server_data = true;
                ok = prepare(session, &waiting, error);
            }
            break;
        case QA_Q2_SVC_COMMAND:
            ok = server_command(session, record->seat, event->data.print.text, &waiting, error);
            if (ok && waiting) --client->batch.cursor;
            break;
        case QA_Q2_SVC_RECONNECT: ok = cancel(session, error) && loading(session, error) &&
            qa_network_q2_client_command(runtime, id, "new", 0, error); break;
        case QA_Q2_SVC_PRINT:
            ok = client->hooks.print(client->hooks.context, id, event->data.print.text, error) && current(session, error); break;
        case QA_Q2_SVC_FRAME:
            if (!event->data.frame->valid) client->last_frame = -1;
            else {
                client->last_frame = event->data.frame->server_frame;
                ok = client->hooks.frame(client->hooks.context, id, event->data.frame, client->batch.records,
                    client->batch.count, client->batch.received_ns, error) && current(session, error);
            }
            break;
        case QA_Q2_SVC_DOWNLOAD: {
            bool complete = false;
            ok = client->hooks.download(client->hooks.context, id, event, &complete, error);
            if (!ok && (!error || (error->code != QA_ERROR_FORMAT && error->code != QA_ERROR_UNSUPPORTED))) {
                --client->batch.cursor; download_retry = true; waiting = true;
            } else if (ok) ok = current(session, error);
            if (ok && complete && client->preparing_game_state) ok = prepare(session, &waiting, error);
            break;
        }
        case QA_Q2_SVC_DISCONNECT:
            ok = retire_client(session, "server disconnected", false, true, true, runtime->now_ns, error);
            break;
        default: break;
        }
        if (session->retiring) break;
    }
    if (ok && !waiting && !session->retiring && client->receive_held && client->batch.cursor == client->batch.count)
        ok = current(session, error) && client->hooks.records(client->hooks.context, id, client->batch.records,
            client->batch.count, error) && current(session, error);
    if (!waiting && !(client->drop_reason && !client->drop_hook_done) &&
        (session->retiring || !ok || client->batch.cursor == client->batch.count)) {
        q2_records_free(&client->batch); client->receive_held = false;
    }
    if (!ok && !client->sent_pending && !download_retry) session->retiring = true;
    runtime->callback = false; session->busy = false; return ok;
}
static bool queue_commands(q2_session *session, const qa_q2_usercmd *commands, size_t seats, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (!session->active || seats != session->seats || !commands || client->command_count == client->policy.pending_commands)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 command group does not match its active Source seats or queue");
    if (client->command_number == UINT64_MAX)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 logical command counter is exhausted");
    if (!client->commands) {
        if (client->policy.pending_commands > SIZE_MAX / sizeof(*client->commands)) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 command queue extent overflows");
        client->commands = calloc(client->policy.pending_commands, sizeof(*client->commands));
        if (!client->commands) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 Source command queue");
        client->command_capacity = client->policy.pending_commands;
    }
    q2_command_group *group = client->commands + client->command_count++;
    memcpy(group->commands, commands, seats * sizeof(*commands));
    group->number = ++client->command_number;
    return true;
}
bool qa_network_q2_client_usercmds(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_usercmd *commands, size_t seats, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    return session && qa_network_callbacks_idle(runtime) && queue_commands(session, commands, seats, error);
}
bool q2_client_submit(q2_session *session, const qa_network_command *command, qa_error *error)
{
    if (session->seats != 1) return q2_fail(error, QA_ERROR_ARGUMENT, "Split Q2 Source input requires its complete admitted command group");
    qa_q2_usercmd wire;
    return current(session, error) && session->state.client.hooks.command(session->state.client.hooks.context,
        command, &wire, error) && current(session, error) && queue_commands(session, &wire, 1, error);
}
bool q2_client_send(q2_session *session, uint64_t now, qa_error *error)
{
    q2_client *client = &session->state.client;
    if (client->policy.messages.demo) return true;
    if (!sent_continue(session, error)) return false;
    if (client->receive_held || client->preparation_held)
        return !qa_q2_channel_should_update(session->channel, now) ||
            q2_send(session, (qa_bytes){0}, now, NULL, error);
    if (client->command_count) {
        qa_q2_channel_status status; qa_q2_channel_get_status(session->channel, &status);
        uint8_t *bytes = malloc(status.capacity);
        if (!bytes) return q2_fail(error, QA_ERROR_MEMORY, "Allocating Q2 command encoding");
        bool ok = true; size_t consumed = 0;
        while (ok && consumed < client->command_count) {
            uint32_t sequence = qa_q2_channel_outgoing(session->channel);
            qa_q2_usercmd group[QA_NETWORK_MAX_SEATS][3];
            for (size_t i = 0; i < session->seats; ++i) {
                group[i][0] = client->oldest[i]; group[i][1] = client->previous[i]; group[i][2] = client->commands[consumed].commands[i];
            }
            qa_net_writer writer; qa_net_writer_init(&writer, bytes, status.capacity, error);
            bool included = false;
            ok = qa_q2_client_move_write(&session->codec, &writer, sequence, client->last_frame,
                (const qa_q2_usercmd (*)[3])group, session->seats) &&
                q2_send(session, (qa_bytes){bytes, qa_net_writer_size(&writer)}, now, &included, error);
            if (!ok || !included) break;
            client->sent = client->commands[consumed++]; client->sent_sequence = sequence;
            client->sent_ns = now; client->sent_cursor = 0; client->sent_pending = true;
            ok = sent_continue(session, error);
        }
        free(bytes);
        client->command_count -= consumed;
        if (client->command_count && consumed)
            memmove(client->commands, client->commands + consumed, client->command_count * sizeof(*client->commands));
        if (consumed) memset(client->commands + client->command_count, 0, consumed * sizeof(*client->commands));
        return ok;
    }
    return !qa_q2_channel_should_update(session->channel, now) || q2_send(session, (qa_bytes){0}, now, NULL, error);
}
bool qa_network_q2_client_send(qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    if (!session || !qa_network_callbacks_idle(runtime)) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 send requires idle transport ownership");
    runtime->callback = true; bool ok = q2_client_send(session, now, error); runtime->callback = false; return ok;
}
bool qa_network_q2_client_disconnect(qa_network_runtime *runtime, qa_net_client_id id, uint64_t now, qa_error *error)
{
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!qa_network_q2_peer(peer) || ((q2_session *)peer->state)->server || !qa_network_callbacks_idle(runtime))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 disconnect requires its exact returned CLIENT owner");
    q2_session *session = peer->state;
    if (session->busy) return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT disconnect is already entered");
    session->busy = true; runtime->callback = true;
    bool ok = retire_client(session, "client disconnected", true, false, false, now, error);
    runtime->callback = false; session->busy = false; return ok;
}
bool qa_network_q2_client_request_full_frame(qa_network_runtime *runtime, qa_net_client_id id, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error); if (!session) return false;
    session->state.client.last_frame = -1; return true;
}
const qa_q2_messages *qa_network_q2_client_messages(qa_network_runtime *runtime, qa_net_client_id id)
{
    q2_session *session = q2_get(runtime, id, false, NULL); return session ? session->state.client.messages : NULL;
}
bool qa_network_q2_client_serverdata(qa_network_runtime *runtime, qa_net_client_id id,
    const qa_q2_serverdata **out, qa_error *error)
{
    q2_session *session = q2_get(runtime, id, false, error);
    if (!session || !out) return q2_fail(error, QA_ERROR_ARGUMENT, "Missing Q2 server-data receipt owner");
    *out = session->state.client.has_server_data ? &session->state.client.server_data : NULL;
    return true;
}
bool q2_client_restart(q2_session *session, qa_error *error)
{
    if (!sent_continue(session, error) || !cancel(session, error)) return false;
    q2_client *client = &session->state.client; session->active = false; client->last_frame = -1;
    client->command_count = 0; client->command_number = 0; client->command_offset = 0; client->has_server_data = false;
    q2_records_free(&client->batch); client->receive_held = client->acknowledgement_held = false;
    memset(client->oldest, 0, sizeof(client->oldest)); memset(client->previous, 0, sizeof(client->previous));
    qa_q2_messages_reset(client->messages);
    session->codec = *qa_q2_messages_codec(client->messages);
    return qa_network_q2_client_command(session->runtime, session->id, "new", 0, error);
}
void q2_client_clear(q2_client *client)
{
    q2_records_free(&client->batch); q2_game_state_free(&client->preparing);
    qa_q2_messages_destroy(client->messages); free(client->commands); free(client->drop_reason);
}
