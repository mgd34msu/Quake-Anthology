/* Donor: network/q3/server.ts, snapshot-store.ts and configstrings.ts. */
#include "server_private.h"
#include "qa/network_q3_save.h"
#include <limits.h>
#include <stdio.h>
#include <math.h>

static bool fail(qa_error *e, qa_status code, const char *s) { qa_error_set(e, code, 0, "%s", s); return false; }
static const char *lookup(void *p, int32_t sequence) { return qa_q3_reliable_lookup(&((qa_q3_server_peer *)p)->reliable, sequence); }
static bool current(qa_q3_server_peer *p, qa_q3_server_world previous, qa_error *e) {
    qa_q3_server_world now = p->hooks.world(p->hooks.context);
    if (now.generation != previous.generation || now.server_id != previous.server_id)
        return fail(e, QA_ERROR_FORMAT, "Q3 callback belongs to a retired server world");
    return true;
}
static bool drop(qa_q3_server_peer *p, const char *reason, qa_error *e) {
    p->state.phase = QA_Q3_ZOMBIE; return p->hooks.drop(p->hooks.context, reason, e);
}
bool qa_q3_server_peer_create(qa_q3_identity identity, qa_q3_product product, const qa_net_address *remote,
                               int32_t challenge, uint16_t qport, const qa_q3_server_hooks *hooks,
                               qa_q3_server_peer **out, qa_error *e) {
    if (!remote || !hooks || !out || !hooks->world || !hooks->command || !hooks->enter_world
        || !hooks->think || !hooks->resend_gamestate || !hooks->pure_rejected_snapshot || !hooks->drop || !hooks->send
        || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return fail(e, QA_ERROR_ARGUMENT, "Q3 server peer requires complete session bindings");
    qa_q3_server_peer *p = calloc(1, sizeof(*p));
    if (!p) return fail(e, QA_ERROR_MEMORY, "Allocating Q3 server peer");
    if (!qa_q3_channel_create(QA_Q3_SERVER, qport, &p->channel, e)) { free(p); return false; }
    p->identity = identity; p->product = product; p->remote = *remote; p->challenge = challenge; p->hooks = *hooks;
    p->state.phase = QA_Q3_CONNECTED; p->state.delta_message = -1; p->state.gamestate_message_number = -1;
    qa_q3_gamestate_init(&p->gamestate);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) qa_q3_slot_clear(&p->history[i], product);
    *out = p; return true;
}
void qa_q3_server_peer_destroy(qa_q3_server_peer *p) {
    if (!p) return;
    qa_q3_channel_destroy(p->channel);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) free(p->history[i].entities);
    queued_message *q = p->queue_first;
    while (q) { queued_message *next = q->next; free(q); q = next; }
    free(p);
}
qa_q3_server_state *qa_q3_server_peer_state(qa_q3_server_peer *p) { return &p->state; }
const qa_q3_identity *qa_q3_server_peer_identity(const qa_q3_server_peer *p) { return &p->identity; }
qa_q3_product qa_q3_server_peer_product(const qa_q3_server_peer *p) { return p->product; }
uint16_t qa_q3_server_peer_qport(const qa_q3_server_peer *p) { return qa_q3_channel_qport(p->channel); }
const qa_q3_gamestate *qa_q3_server_peer_gamestate_view(const qa_q3_server_peer *p) { return &p->gamestate; }
bool qa_q3_server_peer_rebind(qa_q3_server_peer *p, const qa_net_address *remote, qa_error *e) {
    if (!p || !remote || !qa_net_address_equal(&p->remote, remote, false)) return fail(e, QA_ERROR_ARGUMENT, "Q3 NAT rebind changes host");
    p->remote = *remote; return true;
}
bool qa_q3_server_peer_command(qa_q3_server_peer *p, const char *text, qa_error *e) {
    if (!p || !text) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 server command");
    if (!qa_q3_reliable_add(&p->reliable, QA_Q3_SERVER, text, e)) {
        p->state.phase = QA_Q3_ZOMBIE;
        return false;
    }
    return true;
}
bool qa_q3_server_peer_receive(qa_q3_server_peer *p, qa_bytes datagram, qa_q3_receive_kind *kind, qa_error *e) {
    if (!p || !kind) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 server receive");
    qa_q3_packet packet;
    if (!qa_q3_channel_receive(p->channel, datagram, &packet, e)) return false;
    *kind = packet.kind;
    if (packet.kind != QA_Q3_PACKET_MESSAGE || p->state.phase == QA_Q3_ZOMBIE) return true;
    uint8_t data[QA_Q3_MESSAGE_BYTES]; memcpy(data, packet.payload.data, packet.payload.size);
    if (!qa_q3_xor_client(data, packet.payload.size, p->challenge, lookup, p, e)) return false;
    qa_q3_client_cursor cursor;
    if (!qa_q3_client_cursor_init(&cursor, (qa_bytes){data, packet.payload.size}, e)) return false;
    qa_q3_server_world world = p->hooks.world(p->hooks.context);
    qa_q3_server_state *state = &p->state;
    state->message_acknowledge = cursor.header.message_acknowledge;
    if (state->message_acknowledge < 0) return fail(e, QA_ERROR_FORMAT, "Negative Q3 client message acknowledgement");
    int32_t ack = cursor.header.reliable_acknowledge;
    if ((int64_t)ack < (int64_t)p->reliable.sequence - 64) {
        p->reliable.acknowledged = p->reliable.sequence;
        return fail(e, QA_ERROR_FORMAT, "Q3 client reliable acknowledgement was overwritten");
    }
    if (ack < 0 || ack > p->reliable.sequence) return fail(e, QA_ERROR_FORMAT, "Q3 client reliable acknowledgement is outside history");
    p->reliable.acknowledged = ack;
    if (cursor.header.server_id != world.server_id && !world.downloading && !strstr(p->last_command, "nextdl")) {
        if (cursor.header.server_id >= world.restarted_server_id && cursor.header.server_id < world.server_id) return true;
        if (state->message_acknowledge > state->gamestate_message_number) return p->hooks.resend_gamestate(p->hooks.context, e);
        return true;
    }
    for (;;) {
        qa_q3_client_part part; qa_q3_command command;
        if (!qa_q3_client_cursor_next(&cursor, &part, &command)) return false;
        if (part == QA_Q3_CLIENT_END) return true;
        if (part == QA_Q3_CLIENT_COMMAND) {
            if (command.sequence <= state->last_client_command) continue;
            if ((int64_t)command.sequence != (int64_t)state->last_client_command + 1) return drop(p, "Lost reliable commands", e);
            bool allowed = world.client_running || state->phase != QA_Q3_ACTIVE || !world.flood_protect || world.time >= state->next_reliable_time;
            state->next_reliable_time = (int64_t)world.time + 1000;
            if (!p->hooks.command(p->hooks.context, &command, allowed, e)) return false;
            if (!current(p, world, e)) return false;
            state->last_client_command = command.sequence;
            memcpy(p->last_command, command.text, sizeof(p->last_command));
            if (state->phase == QA_Q3_ZOMBIE) return true;
            world = p->hooks.world(p->hooks.context);
            continue;
        }
        state->delta_message = part == QA_Q3_CLIENT_MOVE ? state->message_acknowledge : -1;
        qa_q3_usercmd commands[QA_Q3_USERCMDS]; size_t count = 0;
        if (!qa_q3_client_cursor_movement(&cursor, world.checksum_feed,
            qa_q3_reliable_lookup(&p->reliable, ack), commands, &count)) return false;
        qa_q3_snapshot_slot *slot = &p->history[(uint32_t)state->message_acknowledge & 31];
        slot->ack_time = world.time;
        if (world.pure && !state->pure_authentic && !state->got_pure_command) {
            if (state->phase == QA_Q3_ACTIVE) return p->hooks.resend_gamestate(p->hooks.context, e);
            return true;
        }
        if (state->phase == QA_Q3_PRIMED) {
            state->last_usercmd = commands[0]; state->phase = QA_Q3_ACTIVE;
            if (!p->hooks.enter_world(p->hooks.context, &state->last_usercmd, e) || !current(p, world, e)) return false;
        }
        if (world.pure && !state->pure_authentic) return drop(p, "Cannot validate pure client!", e);
        if (state->phase != QA_Q3_ACTIVE) { state->delta_message = -1; return true; }
        for (size_t i = 0; i < count; ++i) {
            if (commands[i].serverTime > commands[count - 1].serverTime || commands[i].serverTime <= state->last_usercmd.serverTime) continue;
            state->last_usercmd = commands[i];
            if (!p->hooks.think(p->hooks.context, &commands[i], e) || !current(p, world, e)) return false;
            if (state->phase == QA_Q3_ZOMBIE) return true;
        }
        /* Original SV_UserMove completes at the last command; EOF validation is
         * available separately through qa_q3_client_cursor_end. */
        return true;
    }
}
static bool send_next(qa_q3_server_peer *p, bool *sent, qa_error *e) {
    qa_bytes packet;
    if (!qa_q3_channel_next(p->channel, sent, &packet, e)) return false;
    return !*sent || p->hooks.send(p->hooks.context, &p->remote, packet, e);
}
bool qa_q3_server_peer_fragment(qa_q3_server_peer *p, bool *sent, qa_error *e) {
    if (!p || !sent) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 fragment send");
    if (!send_next(p, sent, e)) return false;
    if (!qa_q3_channel_pending(p->channel) && p->queue_first) {
        queued_message *q = p->queue_first;
        qa_q3_xor_server(q->data, q->size, p->challenge, qa_q3_channel_outgoing(p->channel), q->key_command);
        if (!qa_q3_channel_begin(p->channel, (qa_bytes){q->data, q->size}, e)) return false;
        p->queue_first = q->next; if (!p->queue_first) p->queue_last = NULL;
        --p->queue_count; free(q);
        bool next_sent;
        if (!send_next(p, &next_sent, e)) return false;
        *sent = *sent || next_sent;
    }
    return true;
}
static bool rate_interval(size_t size, const qa_q3_server_rate *rate, int64_t *interval, qa_error *e) {
    if (!isfinite(rate->maximum_rate)) return fail(e, QA_ERROR_ARGUMENT, "Q3 maximum rate must retain a finite source number");
    double speed = rate->bytes_per_second;
    if (rate->maximum_rate != 0) {
        double maximum = rate->maximum_rate < 1000 ? 1000 : rate->maximum_rate;
        if (speed > maximum) speed = maximum;
    }
    if (speed == 0) return fail(e, QA_ERROR_ARGUMENT, "Q3 rate must be nonzero");
    if (size > 1500) size = 1500;
    *interval = (int64_t)((double)(size + 48) * 1000 / speed); return true;
}
static bool transmit(qa_q3_server_peer *p, qa_q3_writer *writer, uint8_t *data, const qa_q3_server_rate *rate, qa_error *e) {
    if (!qa_q3_server_end(writer)) return false;
    size_t size = qa_q3_writer_size(writer);
    qa_q3_server_world world = p->hooks.world(p->hooks.context);
    bool sent;
    if (qa_q3_channel_pending(p->channel) || p->queue_first) {
        if (p->queue_count >= 64) return fail(e, QA_ERROR_FORMAT, "Q3 server message queue is full");
        queued_message *q = malloc(sizeof(*q));
        if (!q) return fail(e, QA_ERROR_MEMORY, "Allocating queued Q3 server message");
        q->next = NULL; q->size = size; memcpy(q->data, data, size);
        memcpy(q->key_command, p->last_command, sizeof(q->key_command));
        if (p->queue_last) p->queue_last->next = q; else p->queue_first = q;
        p->queue_last = q; ++p->queue_count;
        if (!qa_q3_server_peer_fragment(p, &sent, e)) return false;
    } else {
        qa_q3_xor_server(data, size, p->challenge, qa_q3_channel_outgoing(p->channel), p->last_command);
        if (!qa_q3_channel_begin(p->channel, (qa_bytes){data, size}, e) || !send_next(p, &sent, e)) return false;
    }
    if (rate->local || (rate->force_lan && rate->lan)) { p->state.next_snapshot_time = (int64_t)world.time - 1; return true; }
    int64_t interval;
    if (!rate_interval(size, rate, &interval, e)) return false;
    p->state.rate_delayed = interval >= rate->snapshot_ms;
    if (interval < rate->snapshot_ms) interval = rate->snapshot_ms;
    if (p->state.phase != QA_Q3_ACTIVE && !world.downloading && interval < 1000) interval = 1000;
    p->state.next_snapshot_time = world.time + interval; return true;
}
static bool message_begin(qa_q3_server_peer *p, qa_q3_writer *w) {
    if (!qa_q3_server_begin(w, p->state.last_client_command)) return false;
    for (int64_t n = (int64_t)p->reliable.acknowledged + 1; n <= p->reliable.sequence; ++n)
        if (!qa_q3_server_command(w, (int32_t)n, qa_q3_reliable_lookup(&p->reliable, (int32_t)n))) return false;
    p->state.reliable_sent = p->reliable.sequence; return true;
}
static const qa_q3_snapshot *delta_snapshot(const qa_q3_server_peer *p, uint32_t sequence) {
    int32_t requested = p->state.delta_message;
    if (requested > 0 && p->state.phase == QA_Q3_ACTIVE && (int64_t)sequence - requested < 29 && sequence > (uint32_t)requested) {
        const qa_q3_snapshot *candidate = &p->history[(uint32_t)requested & 31].value;
        if (candidate->valid && candidate->message_number == requested) return candidate;
    }
    return NULL;
}
static bool drain(qa_q3_server_peer *p, qa_error *e) {
    while (qa_q3_channel_pending(p->channel) || p->queue_first) {
        bool sent;
        if (!qa_q3_server_peer_fragment(p, &sent, e)) return false;
        if (!sent) return fail(e, QA_ERROR_FORMAT, "Q3 retained fragment queue did not advance");
    }
    return true;
}
bool qa_q3_server_peer_disconnect(qa_q3_server_peer *p, const qa_q3_server_rate *rate,
                                  uint8_t flags, const char *reason, qa_error *e) {
    if (!p || !rate || !reason || p->state.phase == QA_Q3_FREE)
        return fail(e, QA_ERROR_ARGUMENT, "Q3 disconnect requires its retained peer and delivery policy");
    if (p->state.phase == QA_Q3_ZOMBIE) return true;
    char command[QA_Q3_COMMAND_CHARS];
    const char prefix[] = "disconnect \"";
    memcpy(command, prefix, sizeof(prefix) - 1); size_t length = sizeof(prefix) - 1;
    for (const char *text = reason; *text; ++text) {
        if (*text == '"' || *text == '\n' || *text == '\r') continue;
        if (length == sizeof(command) - 1) break;
        command[length++] = *text;
    }
    if (length < sizeof(command) - 1) command[length++] = '"';
    command[length] = 0;
    bool ok = qa_q3_server_peer_command(p, command, e) && drain(p, e);
    if (ok) {
        uint32_t sequence = qa_q3_channel_outgoing(p->channel);
        if (sequence > INT32_MAX) {
            p->state.phase = QA_Q3_ZOMBIE;
            return fail(e, QA_ERROR_FORMAT, "Q3 disconnect message sequence exhausted");
        }
        qa_q3_snapshot retained = p->history[sequence & 31].value;
        qa_q3_server_world world = p->hooks.world(p->hooks.context);
        const qa_q3_snapshot *old = delta_snapshot(p, sequence);
        retained.message_number = (int32_t)sequence; retained.server_time = world.time;
        retained.delta_number = old ? p->state.delta_message : -1;
        retained.flags = (uint8_t)(flags | (p->state.rate_delayed ? 1U : 0U) |
            (p->state.phase == QA_Q3_ACTIVE ? 0U : 2U));
        retained.server_command_number = p->reliable.sequence;
        uint8_t data[QA_Q3_MESSAGE_BYTES]; qa_q3_writer writer;
        qa_q3_writer_init(&writer, data, sizeof(data), false, e);
        ok = message_begin(p, &writer) && qa_q3_server_snapshot(&writer, old, &retained, &p->gamestate);
        if (ok) {
            qa_q3_snapshot_slot *slot = &p->history[sequence & 31];
            slot->sent_time = world.time; slot->ack_time = -1;
            slot->message_size = qa_q3_writer_size(&writer);
            ok = transmit(p, &writer, data, rate, e) && drain(p, e);
        }
    }
    p->state.phase = QA_Q3_ZOMBIE;
    return ok;
}
bool qa_q3_server_peer_seed_baselines(qa_q3_server_peer *p, const qa_q3_gamestate *state, qa_error *e) {
    if (!p || !state || p->state.phase != QA_Q3_CONNECTED || p->state.gamestate_message_number != -1)
        return fail(e, QA_ERROR_ARGUMENT, "Q3 initial baselines require their actual connected source admission");
    for (int32_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (state->baseline_present[i] && (i >= QA_Q3_ENTITY_NONE || state->baselines[i].number != i))
            return fail(e, QA_ERROR_FORMAT, "Q3 initial baseline differs from its physical source number");
    if (state != &p->gamestate) {
        memcpy(p->gamestate.baselines, state->baselines, sizeof(p->gamestate.baselines));
        memcpy(p->gamestate.baseline_present, state->baseline_present, sizeof(p->gamestate.baseline_present));
    }
    return true;
}
bool qa_q3_server_peer_gamestate(qa_q3_server_peer *p, const qa_q3_gamestate *state, const qa_q3_server_rate *rate, qa_error *e) {
    if (!p || !state || !rate) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 gamestate send");
    qa_q3_server_world world = p->hooks.world(p->hooks.context);
    uint32_t sequence = qa_q3_channel_outgoing(p->channel) + p->queue_count + (qa_q3_channel_pending(p->channel) ? 1U : 0U);
    if (sequence > (uint32_t)INT32_MAX) return fail(e, QA_ERROR_FORMAT, "Q3 gamestate sequence exhausted");
    p->gamestate = *state; p->gamestate.command_sequence = p->reliable.sequence; p->gamestate.checksum_feed = world.checksum_feed;
    uint8_t data[QA_Q3_MESSAGE_BYTES]; qa_q3_writer writer;
    qa_q3_writer_init(&writer, data, sizeof(data), false, e);
    if (!message_begin(p, &writer) || !qa_q3_server_gamestate(&writer, &p->gamestate)) return false;
    p->state.phase = QA_Q3_PRIMED; p->state.pure_authentic = false; p->state.got_pure_command = false;
    p->state.gamestate_message_number = (int32_t)sequence; p->state.delta_message = -1;
    return transmit(p, &writer, data, rate, e);
}
bool qa_q3_server_peer_snapshot_ready(const qa_q3_server_peer *p)
{ return p && !qa_q3_channel_pending(p->channel) && !p->queue_first; }
bool qa_q3_server_peer_snapshot_write(qa_q3_server_peer *p, const qa_q3_snapshot *snapshot,
    const qa_q3_server_rate *rate, qa_q3_server_download_write_fn write_downloads, void *context, qa_error *e) {
    if (!p || !snapshot || !rate || snapshot->player.product != p->product)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 snapshot send");
    if (qa_q3_channel_pending(p->channel) || p->queue_first) {
        bool sent; int64_t interval;
        if (!rate_interval(qa_q3_channel_pending(p->channel) ? qa_q3_channel_remaining(p->channel) : p->queue_first->size, rate, &interval, e)) return false;
        p->state.next_snapshot_time = p->hooks.world(p->hooks.context).time + interval;
        return qa_q3_server_peer_fragment(p, &sent, e);
    }
    uint32_t sequence = qa_q3_channel_outgoing(p->channel);
    qa_q3_server_world world = p->hooks.world(p->hooks.context);
    qa_q3_snapshot current_snapshot = *snapshot;
    current_snapshot.valid = true; current_snapshot.message_number = (int32_t)sequence;
    current_snapshot.server_time = world.time; current_snapshot.server_command_number = p->reliable.sequence;
    current_snapshot.parse_entities_number = p->entity_number;
    current_snapshot.flags = (uint8_t)(current_snapshot.flags | (p->state.rate_delayed ? 1U : 0U)
        | (p->state.phase == QA_Q3_ACTIVE ? 0U : 2U));
    const qa_q3_snapshot *old = delta_snapshot(p, sequence);
    int32_t requested = p->state.delta_message;
    current_snapshot.delta_number = old ? requested : -1;
    uint8_t data[QA_Q3_MESSAGE_BYTES]; qa_q3_writer writer;
    qa_q3_writer_init(&writer, data, sizeof(data), false, e);
    if (!message_begin(p, &writer) || !qa_q3_server_snapshot(&writer, old, &current_snapshot, &p->gamestate)) return false;
    if (write_downloads && !write_downloads(context, &writer, e)) return false;
    qa_q3_snapshot_slot *slot = &p->history[sequence & 31];
    if (current_snapshot.entity_count > UINT64_MAX - p->entity_number)
        return fail(e, QA_ERROR_FORMAT, "Q3 snapshot entity sequence exhausted");
    if (!qa_q3_slot_store(slot, &current_snapshot, e)) return false;
    p->entity_number += current_snapshot.entity_count;
    slot->sent_time = world.time; slot->ack_time = -1; slot->message_size = qa_q3_writer_size(&writer);
    return transmit(p, &writer, data, rate, e);
}
typedef struct snapshot_downloads { const qa_q3_download *messages; size_t count; } snapshot_downloads;
static bool write_download_array(void *context, qa_q3_writer *writer, qa_error *error)
{
    const snapshot_downloads *downloads = context; (void)error;
    for (size_t i = 0; i < downloads->count; ++i)
        if (!qa_q3_server_download(writer, downloads->messages + i)) return false;
    return true;
}
bool qa_q3_server_peer_snapshot_downloads(qa_q3_server_peer *p, const qa_q3_snapshot *snapshot,
    const qa_q3_server_rate *rate, const qa_q3_download *downloads, size_t count, qa_error *error)
{
    if (count && !downloads) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 snapshot download records");
    snapshot_downloads source = {downloads, count};
    return qa_q3_server_peer_snapshot_write(p, snapshot, rate, write_download_array, &source, error);
}
bool qa_q3_server_peer_snapshot(qa_q3_server_peer *p, const qa_q3_snapshot *snapshot,
                                const qa_q3_server_rate *rate, const qa_q3_download *download, qa_error *e) {
    return qa_q3_server_peer_snapshot_downloads(p, snapshot, rate, download, download ? 1 : 0, e);
}
bool qa_q3_server_peer_pure(qa_q3_server_peer *p, const qa_q3_pure_server *server, const qa_q3_tokens *args,
                            qa_q3_pure_result *result, qa_error *e) {
    if (!p) return fail(e, QA_ERROR_ARGUMENT, "Missing Q3 pure verification peer");
    if (!qa_q3_verify_pure(server, args, result, e)) return false;
    if (*result == QA_Q3_PURE_DISABLED || *result == QA_Q3_PURE_OUTDATED) return true;
    p->state.got_pure_command = true; p->state.pure_authentic = *result == QA_Q3_PURE_AUTHENTIC;
    if (*result == QA_Q3_PURE_REJECTED) {
        p->state.next_snapshot_time = -1; p->state.phase = QA_Q3_ACTIVE;
        if (!p->hooks.pure_rejected_snapshot(p->hooks.context, e)) return false;
        return drop(p, "Unpure client detected. Invalid .PK3 files referenced!", e);
    }
    return true;
}
bool qa_q3_server_peer_reset_pure(qa_q3_server_peer *p, qa_error *error)
{
    if (!p) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 pure admission owner");
    p->state.pure_authentic = false; p->state.got_pure_command = false; return true;
}
bool qa_q3_server_peer_configstring(qa_q3_server_peer *p, unsigned index, const char *text, qa_error *e) {
    if (!p || !text || index >= QA_Q3_CONFIGSTRINGS || strlen(text) >= 8192 || strpbrk(text, "\"\r\n"))
        return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 configstring update");
    const char *old = qa_q3_configstring(&p->gamestate, index);
    if (old && !strcmp(old, text)) return true;
    if (!qa_q3_configstring_set(&p->gamestate, index, text, e)) return false;
    if (p->state.phase < QA_Q3_PRIMED) return true;
    size_t length = strlen(text), offset = 0;
    do {
        size_t n = length - offset;
        if (n > 999) n = 999;
        const char *name = length <= 999 ? "cs" : !offset ? "bcs0" : offset + n == length ? "bcs2" : "bcs1";
        char command[1024];
        snprintf(command, sizeof(command), "%s %u \"%.*s\"", name, index, (int)n, text + offset);
        if (!qa_q3_server_peer_command(p, command, e)) return false;
        offset += n;
    } while (offset < length);
    return true;
}
