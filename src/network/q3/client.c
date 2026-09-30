/* Donor: network/q3/client.ts, snapshot-history.ts, client-server-command.ts. */
#include "client_private.h"
#include <limits.h>
#include <stdio.h>

static bool fail(qa_error *e, qa_status code, const char *s) { qa_error_set(e, code, 0, "%s", s); return false; }
static int32_t atoi32(const char *text) {
    char *end;
    long long n = strtoll(text, &end, 10);
    if (end == text) return 0;
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : (int32_t)n;
}
static bool current(qa_q3_client_peer *p, qa_error *e) {
    if (p->hooks.generation(p->hooks.context) != p->generation) return fail(e, QA_ERROR_FORMAT, "Q3 client callback belongs to a retired session");
    return true;
}
static const char *lookup(void *user, int32_t sequence) { return ((qa_q3_client_peer *)user)->server_commands[(uint32_t)sequence & 63]; }
static const qa_q3_snapshot *history(void *user, int32_t sequence) { return &((qa_q3_client_peer *)user)->history[(uint32_t)sequence & 31].value; }
static bool system_info(qa_q3_client_peer *p, qa_error *e) {
    const char *text = qa_q3_configstring(&p->gamestate, 1);
    char server[64];
    if (!text || !qa_q3_info_value(text, "sv_serverid", server, sizeof(server), e)) return false;
    p->server_id = atoi32(server);
    return p->hooks.system_info(p->hooks.context, text, e) && current(p, e);
}
static bool create_peer(qa_q3_identity identity, qa_q3_product product, const qa_net_address *remote,
                               int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
                               bool demo, qa_q3_client_peer **out, qa_error *e) {
    if ((!remote && !demo) || !hooks || !out || !hooks->generation || !hooks->gamestate || !hooks->system_info
        || !hooks->clear_active || !hooks->download_size || !hooks->snapshot || !hooks->download || !hooks->command || !hooks->map_restart
        || !hooks->disconnect || !hooks->level_shot || !hooks->local_server_running || (!demo && !hooks->send)
        || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return fail(e, QA_ERROR_ARGUMENT, "Q3 client peer requires complete session bindings");
    qa_q3_client_peer *p = calloc(1, sizeof(*p));
    if (!p) return fail(e, QA_ERROR_MEMORY, "Allocating Q3 client peer");
    if (!demo && !qa_q3_channel_create(QA_Q3_CLIENT, qport, &p->channel, e)) { free(p); return false; }
    p->identity = identity; p->product = product; if (remote) p->remote = *remote; p->demo = demo; p->challenge = challenge; p->hooks = *hooks;
    p->generation = hooks->generation(hooks->context); p->demo_waiting = true;
    qa_q3_gamestate_init(&p->gamestate);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) qa_q3_slot_clear(&p->history[i], product);
    *out = p; return true;
}
bool qa_q3_client_peer_create(qa_q3_identity identity, qa_q3_product product, const qa_net_address *remote,
                               int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
                               qa_q3_client_peer **out, qa_error *e) {
    return create_peer(identity, product, remote, challenge, qport, hooks, false, out, e);
}
bool qa_q3_client_peer_create_demo(qa_q3_identity identity, qa_q3_product product,
                                    const qa_q3_client_hooks *hooks, qa_q3_client_peer **out, qa_error *e) {
    return create_peer(identity, product, NULL, 0, 0, hooks, true, out, e);
}
void qa_q3_client_peer_destroy(qa_q3_client_peer *p) {
    if (!p) return;
    qa_q3_channel_destroy(p->channel);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) free(p->history[i].entities);
    free(p);
}
const qa_q3_identity *qa_q3_client_peer_identity(const qa_q3_client_peer *p) { return &p->identity; }
const qa_q3_gamestate *qa_q3_client_peer_gamestate(const qa_q3_client_peer *p) { return &p->gamestate; }
const qa_q3_snapshot *qa_q3_client_peer_snapshot(const qa_q3_client_peer *p) { return p->has_snapshot ? &p->history[(uint32_t)p->latest_snapshot & 31].value : NULL; }
const qa_q3_snapshot *qa_q3_client_peer_snapshot_at(const qa_q3_client_peer *p, int32_t number) {
    if (!p || !p->has_snapshot || number < 0 || number > p->latest_snapshot ||
        (int64_t)p->latest_snapshot - number >= QA_Q3_PACKET_BACKUP) return NULL;
    const qa_q3_snapshot *value = &p->history[(uint32_t)number & (QA_Q3_PACKET_BACKUP - 1)].value;
    return value->valid && value->message_number == number ? value : NULL;
}
bool qa_q3_client_peer_command(qa_q3_client_peer *p, const char *text, qa_error *e) {
    if (!p || p->disconnected) return fail(e, QA_ERROR_ARGUMENT, "Q3 client is disconnected");
    return current(p, e) && qa_q3_reliable_add(&p->reliable, QA_Q3_CLIENT, text, e);
}
bool qa_q3_client_peer_usercmd(qa_q3_client_peer *p, const qa_q3_usercmd *command, qa_error *e) {
    if (!p || !command || p->command_number == UINT64_MAX) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 generated user command");
    if (!current(p, e)) return false;
    ++p->command_number; p->commands[p->command_number & 63] = *command; return true;
}
uint64_t qa_q3_client_peer_usercmd_number(const qa_q3_client_peer *p) { return p->command_number; }
const qa_q3_usercmd *qa_q3_client_peer_usercmd_at(const qa_q3_client_peer *p, uint64_t number) {
    if (number > p->command_number || p->command_number - number >= 64) return NULL;
    return &p->commands[number & 63];
}
int32_t qa_q3_client_peer_server_command_sequence(const qa_q3_client_peer *p) { return p->server_command_sequence; }
static bool consume(void *user, const qa_q3_server_event *event, qa_error *e) {
    qa_q3_client_peer *p = user;
    if (!current(p, e)) return false;
    switch (event->kind) {
    case QA_Q3_EVENT_ACK:
        if (!p->demo && (event->value.acknowledge < 0 || event->value.acknowledge > p->reliable.sequence))
            return fail(e, QA_ERROR_FORMAT, "Invalid Q3 server reliable acknowledgement");
        p->reliable.acknowledged = event->value.acknowledge;
        return true;
    case QA_Q3_EVENT_GAMESTATE_START:
        if (!p->hooks.clear_active(p->hooks.context, e) || !current(p, e)) return false;
        for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) qa_q3_slot_clear(&p->history[i], p->product);
        p->has_snapshot = false; p->latest_snapshot = 0; p->parse_entities_number = 0;
        p->command_number = 0; memset(p->commands, 0, sizeof(p->commands)); memset(p->packets, 0, sizeof(p->packets));
        p->big_configstring[0] = 0;
        return true;
    case QA_Q3_EVENT_GAMESTATE:
        p->server_command_sequence = event->value.gamestate->command_sequence;
        if (!system_info(p, e)) return false;
        return p->hooks.gamestate(p->hooks.context, event->value.gamestate, e) && current(p, e);
    case QA_Q3_EVENT_COMMAND:
        p->server_command_sequence = event->value.command->sequence;
        memcpy(p->server_commands[(uint32_t)p->server_command_sequence & 63], event->value.command->text, QA_Q3_COMMAND_CHARS);
        return true;
    case QA_Q3_EVENT_DOWNLOAD:
        return p->hooks.download(p->hooks.context, event->value.download, e) && current(p, e);
    case QA_Q3_EVENT_SNAPSHOT: {
        const qa_q3_snapshot *snapshot = event->value.snapshot.value;
        if (snapshot->delta_number <= 0) p->demo_waiting = false;
        if (event->value.snapshot.validity != QA_Q3_SNAPSHOT_VALID) return true;
        int64_t skipped = p->has_snapshot ? (int64_t)p->latest_snapshot + 1 : 1;
        if ((int64_t)snapshot->message_number - skipped >= QA_Q3_PACKET_BACKUP) skipped = (int64_t)snapshot->message_number - QA_Q3_PACKET_BACKUP + 1;
        while (skipped < snapshot->message_number) p->history[(uint32_t)skipped++ & 31].value.valid = false;
        qa_q3_snapshot_slot *slot = &p->history[(uint32_t)snapshot->message_number & 31];
        if (!qa_q3_slot_store(slot, snapshot, e)) return false;
        p->has_snapshot = true; p->latest_snapshot = snapshot->message_number;
        int32_t ping = 999;
        for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
            const sent_packet *packet = &p->packets[((p->channel ? qa_q3_channel_outgoing(p->channel) : 1U) - 1 - i) & 31];
            if (snapshot->player.commandTime >= packet->server_time) {
                int64_t elapsed = (int64_t)p->receive_time - packet->real_time;
                ping = elapsed > INT32_MAX ? INT32_MAX : elapsed < INT32_MIN ? INT32_MIN : (int32_t)elapsed;
                break;
            }
        }
        slot->value.player.ping = ping;
        return p->hooks.snapshot(p->hooks.context, &slot->value, ping, e) && current(p, e);
    }
    }
    return fail(e, QA_ERROR_FORMAT, "Unknown Q3 client event");
}
static bool download_size(void *user, int32_t wire_size, int32_t *effective, qa_error *error) {
    qa_q3_client_peer *p = user;
    return p->hooks.download_size(p->hooks.context, wire_size, effective, error) && current(p, error);
}
bool qa_q3_client_peer_message(qa_q3_client_peer *p, int32_t sequence, qa_bytes plaintext, int32_t now, qa_error *e) {
    if (!p || p->disconnected || sequence < 0) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 client message");
    if (!current(p, e)) return false;
    if (!p->demo && (sequence < 1 || sequence <= p->server_message_sequence))
        return fail(e, QA_ERROR_FORMAT, "Q3 server message sequence must advance");
    p->server_message_sequence = sequence; p->receive_time = now;
    qa_q3_server_decode decoder = {
        .product = p->product, .message_number = sequence, .reliable_sequence = p->reliable.sequence,
        .server_command_sequence = p->server_command_sequence, .parse_entities_number = p->parse_entities_number,
        .gamestate = &p->gamestate, .history = history, .history_context = p,
        .entity_scratch = p->scratch, .entity_scratch_capacity = QA_Q3_ENTITY_NONE,
        .download_size = download_size, .download_context = p
    };
    bool ok = qa_q3_decode_server(plaintext, &decoder, consume, p, e);
    p->parse_entities_number = decoder.parse_entities_number;
    return ok;
}
bool qa_q3_client_peer_receive(qa_q3_client_peer *p, qa_bytes datagram, int32_t now, qa_q3_receive_kind *kind, qa_error *e) {
    if (!p || !kind || p->disconnected || p->demo) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 client datagram");
    if (!current(p, e)) return false;
    qa_q3_packet packet;
    if (!qa_q3_channel_receive(p->channel, datagram, &packet, e)) return false;
    *kind = packet.kind;
    if (packet.kind != QA_Q3_PACKET_MESSAGE) return true;
    qa_q3_reader reader; qa_q3_reader_init(&reader, packet.payload, false, e);
    int32_t acknowledge = (int32_t)qa_q3_read_bits(&reader, 32);
    if (reader.raw.failed) return false;
    uint8_t plaintext[QA_Q3_MESSAGE_BYTES]; memcpy(plaintext, packet.payload.data, packet.payload.size);
    qa_q3_xor_server(plaintext, packet.payload.size, p->challenge, packet.sequence, qa_q3_reliable_lookup(&p->reliable, acknowledge));
    return qa_q3_client_peer_message(p, (int32_t)packet.sequence, (qa_bytes){plaintext, packet.payload.size}, now, e);
}
static bool execute(qa_q3_client_peer *p, int32_t sequence, const char *text, qa_error *e) {
    qa_q3_tokens tokens;
    if (!qa_q3_tokenize(text, &tokens, e)) return false;
    const char *name = qa_q3_token(&tokens, 0);
    if (!strcmp(name, "disconnect")) {
        p->disconnected = true;
        return p->hooks.disconnect(p->hooks.context, tokens.count >= 2 ? qa_q3_token(&tokens, 1) : "Server disconnected", e);
    }
    if (!strcmp(name, "bcs0")) {
        int n = snprintf(p->big_configstring, sizeof(p->big_configstring), "cs %s \"%s", qa_q3_token(&tokens, 1), qa_q3_token(&tokens, 2));
        if (n < 0 || (size_t)n >= sizeof(p->big_configstring)) return fail(e, QA_ERROR_FORMAT, "Q3 big configstring overflow");
        return true;
    }
    if (!strcmp(name, "bcs1") || !strcmp(name, "bcs2")) {
        size_t used = strlen(p->big_configstring), added = strlen(qa_q3_token(&tokens, 2));
        bool last = !strcmp(name, "bcs2");
        if (!used || added + (last ? 1 : 0) >= sizeof(p->big_configstring) - used) return fail(e, QA_ERROR_FORMAT, "Q3 big configstring overflow or missing start");
        memcpy(p->big_configstring + used, qa_q3_token(&tokens, 2), added + 1);
        if (!last) return true;
        p->big_configstring[used + added] = '"'; p->big_configstring[used + added + 1] = 0;
        if (!qa_q3_tokenize(p->big_configstring, &tokens, e)) return false;
        name = qa_q3_token(&tokens, 0);
    }
    if (!strcmp(name, "cs")) {
        int32_t index = atoi32(qa_q3_token(&tokens, 1));
        if (index < 0 || index >= QA_Q3_CONFIGSTRINGS) return fail(e, QA_ERROR_FORMAT, "Q3 server command has invalid configstring index");
        char value[8192]; size_t used = 0;
        for (size_t i = 2; i < tokens.count; ++i) {
            size_t size = strlen(qa_q3_token(&tokens, i));
            if (size + (i > 2 ? 1 : 0) >= sizeof(value) - used) return fail(e, QA_ERROR_FORMAT, "Q3 configstring command too long");
            if (i > 2) value[used++] = ' ';
            memcpy(value + used, qa_q3_token(&tokens, i), size); used += size;
        }
        value[used] = 0;
        const char *old = qa_q3_configstring(&p->gamestate, (unsigned)index);
        bool changed = !old || strcmp(old, value) != 0;
        if (changed && !qa_q3_configstring_set(&p->gamestate, (unsigned)index, value, e)) return false;
        if (changed && index == 1 && !system_info(p, e)) return false;
    } else if (!strcmp(name, "map_restart")) {
        memset(p->commands, 0, sizeof(p->commands));
        if (!p->hooks.map_restart(p->hooks.context, e) || !current(p, e)) return false;
    } else if (!strcmp(name, "clientLevelShot")) {
        if (!p->hooks.local_server_running(p->hooks.context)) return true;
        if (!p->hooks.level_shot(p->hooks.context, e) || !current(p, e)) return false;
    }
    return p->hooks.command(p->hooks.context, sequence, &tokens, e) && current(p, e);
}
bool qa_q3_client_peer_execute(qa_q3_client_peer *p, int32_t sequence, bool demo, qa_error *e) {
    if (!p) return fail(e, QA_ERROR_ARGUMENT, "Missing Q3 client peer");
    if (!current(p, e)) return false;
    if ((int64_t)sequence <= (int64_t)p->server_command_sequence - 64) {
        if (demo || p->demo) return true;
        return fail(e, QA_ERROR_FORMAT, "Q3 server command was cycled out");
    }
    if (sequence > p->server_command_sequence) return fail(e, QA_ERROR_FORMAT, "Requested Q3 server command has not arrived");
    p->last_executed_server_command = sequence;
    return execute(p, sequence, p->server_commands[(uint32_t)sequence & 63], e);
}
bool qa_q3_client_peer_send(qa_q3_client_peer *p, const qa_q3_client_send *options, qa_error *e) {
    if (!p || !options || p->disconnected || p->demo) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 client send");
    if (!current(p, e)) return false;
    if (qa_q3_channel_pending(p->channel)) return fail(e, QA_ERROR_ARGUMENT, "Q3 client has pending fragments from an interrupted send");
    qa_q3_client_message message = {0};
    message.header = (qa_q3_client_header){p->server_id, p->server_message_sequence, p->server_command_sequence};
    int64_t pending = (int64_t)p->reliable.sequence - p->reliable.acknowledged;
    if (pending < 0 || pending > QA_Q3_RELIABLE + 1) return fail(e, QA_ERROR_FORMAT, "Invalid Q3 client reliable range");
    for (int64_t n = (int64_t)p->reliable.acknowledged + 1; n <= p->reliable.sequence; ++n) {
        qa_q3_command *command = &message.commands[message.command_count++];
        command->sequence = (int32_t)n; memcpy(command->text, qa_q3_reliable_lookup(&p->reliable, (int32_t)n), sizeof(command->text));
    }
    uint32_t sequence = qa_q3_channel_outgoing(p->channel);
    unsigned dup = options->packet_dup > 5 ? 5 : options->packet_dup;
    const sent_packet *old = &p->packets[(sequence - 1 - dup) & 31];
    uint64_t count = p->command_number >= old->command_number ? p->command_number - old->command_number : 0;
    if (count > QA_Q3_USERCMDS) count = QA_Q3_USERCMDS;
    message.usercmd_count = (size_t)count; message.movement = count != 0;
    const qa_q3_snapshot *latest = qa_q3_client_peer_snapshot(p);
    message.no_delta = options->no_delta || !latest || p->demo_waiting || latest->message_number != p->server_message_sequence;
    for (size_t i = 0; i < message.usercmd_count; ++i) message.usercmds[i] = p->commands[(p->command_number - count + i + 1) & 63];
    uint8_t data[QA_Q3_MESSAGE_BYTES]; qa_q3_writer writer;
    qa_q3_writer_init(&writer, data, sizeof(data), false, e);
    if (!qa_q3_encode_client(&writer, &message, p->gamestate.checksum_feed, lookup(p, p->server_command_sequence))) return false;
    size_t size = qa_q3_writer_size(&writer);
    if (!qa_q3_xor_client(data, size, p->challenge, lookup, p, e)) return false;
    p->packets[sequence & 31] = (sent_packet){p->command_number,
        count ? message.usercmds[count - 1].serverTime : 0, options->real_time};
    p->last_packet_sent_time = options->real_time;
    if (!qa_q3_channel_begin(p->channel, (qa_bytes){data, size}, e)) return false;
    do {
        bool present; qa_bytes packet;
        if (!qa_q3_channel_next(p->channel, &present, &packet, e)) return false;
        if (present && !p->hooks.send(p->hooks.context, &p->remote, packet, e)) return false;
        if (!current(p, e)) return false;
    } while (qa_q3_channel_pending(p->channel));
    return true;
}
bool qa_q3_client_peer_ready(const qa_q3_client_peer *p, const qa_q3_client_readiness *o) {
    if (!p || !o || p->disconnected || p->demo || o->cinematic) return false;
    int64_t elapsed = (int64_t)o->real_time - p->last_packet_sent_time;
    if (o->downloading && elapsed < 50) return false;
    if (!o->active && !o->primed && !o->downloading && elapsed < 1000) return false;
    if (o->local || o->lan) return true;
    unsigned maximum = o->maximum_packets < 15 ? 15 : o->maximum_packets > 125 ? 125 : o->maximum_packets;
    const sent_packet *previous = &p->packets[(qa_q3_channel_outgoing(p->channel) - 1) & 31];
    return (int64_t)o->real_time - previous->real_time >= 1000 / maximum;
}
bool qa_q3_client_peer_disconnect(qa_q3_client_peer *p, const qa_q3_client_send *o, qa_error *e) {
    if (p && p->demo) return true;
    if (!qa_q3_client_peer_command(p, "disconnect", e)) return false;
    for (unsigned i = 0; i < 3; ++i) if (!qa_q3_client_peer_send(p, o, e)) return false;
    p->disconnected = true; return true;
}
