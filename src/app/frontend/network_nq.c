#include "network_nq_private.h"
#include "qa/application_network.h"
#include "qa/collision.h"
#include "qa/launch_identity.h"
#include "qa/network_save.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct nq_batch {
    uint8_t bytes[NQ_MESSAGE];
    size_t size;
    qa_q1_emit_fn emit;
    void *context;
    frontend_nq_host *host;
} nq_batch;
static char *copy_text(const char *, qa_error *);
static bool peer_actor(nq_frontend_peer *peer, qa_actor_id *out, qa_error *error)
{
    if (!peer->occupied || peer->retiring || !qa_application_remote_player_actor(peer->host->frontend->application,
        peer->client, peer->seat, out)) return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake source peer has no canonical remote player");
    qa_actor_owner owner; uint32_t slot; qa_net_protocol_id protocol;
    return qa_application_network_q1_source(peer->host->frontend->application, *out, &owner, &slot, &protocol, error) &&
        ((owner == peer->host->owner && slot == peer->source_slot && protocol.kind == QA_NET_NQ15) ||
         frontend_fail(error, QA_ERROR_FORMAT, "NetQuake peer changes its admitted source slot or provider"));
}
static bool host_source(frontend_nq_host *host, qa_error *error)
{
    qa_application_network_q1_host source;
    if (!qa_application_network_q1_host_source(host->frontend->application, &source, error)) return false;
    if (source.protocol.kind != QA_NET_NQ15 || source.protocol.flags || source.protocol.revision ||
        (host->owner && source.owner != host->owner))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "NetQuake hosting requires its complete original classic source owner");
    host->owner = source.owner; return true;
}
static bool batch_flush(nq_batch *batch, qa_error *error)
{
    if (!batch->size) return true;
    if (!batch->emit(batch->context, (qa_bytes){batch->bytes, batch->size}, error)) return false;
    batch->size = 0; return true;
}
static bool batch_bytes(nq_batch *batch, qa_bytes bytes, qa_error *error)
{
    if (bytes.size > sizeof(batch->bytes) || (bytes.size && !bytes.data))
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source service exceeds native reliable message extent");
    if (bytes.size > sizeof(batch->bytes) - batch->size && !batch_flush(batch, error)) return false;
    if (bytes.size) memcpy(batch->bytes + batch->size, bytes.data, bytes.size);
    batch->size += bytes.size; return true;
}
static bool batch_message(nq_batch *batch, const qa_nq_message *message, qa_nq_options options, qa_error *error)
{
    uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15}, options, message, NULL, 0)) return false;
    char *name = NULL;
    if (batch->host && message->op == QA_NQ_NAME) {
        name = copy_text(message->data.indexed_text.text, error);
        if (!name) return false;
    }
    bool ok = batch_bytes(batch, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    if (ok && name) {
        uint8_t index = message->data.indexed_text.index;
        free(batch->host->published_names[index]);
        batch->host->published_names[index] = name;
    } else free(name);
    return ok;
}
static int32_t wire_frags(int32_t value)
{
    uint32_t bits = (uint32_t)value & UINT32_C(65535);
    return bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536;
}
static bool status_messages(nq_batch *batch, qa_application_network_q1_status_player *players,
    size_t count, qa_nq_options options, qa_error *error)
{
    for (size_t i = 0; i < count; ++i) {
        qa_nq_message message = {.op = QA_NQ_NAME,
            .data.indexed_text = {(uint8_t)(players[i].source_slot - 1), players[i].name}};
        if (!batch_message(batch, &message, options, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_FRAGS,
            .data.indexed = {(uint8_t)(players[i].source_slot - 1), wire_frags(players[i].frags)}};
        if (!batch_message(batch, &message, options, error)) return false;
        message.op = QA_NQ_COLORS; message.data.indexed.value = players[i].colors;
        if (!batch_message(batch, &message, options, error)) return false;
    }
    return true;
}
static bool capture_baselines(nq_frontend_peer *peer, qa_actor_id player,
    const qa_application_network_q1_world *world, qa_error *error)
{
    qa_q1_entity *values = NULL; size_t count = 0, capacity = 0;
    uint32_t cursor = 0; bool present; qa_actor_id actor; qa_q1_entity entity;
    for (uint32_t slot = 1; slot <= world->max_clients; ++slot) {
        if (!qa_application_network_q1_client_baseline(peer->host->frontend->application, player, slot, &entity, error)) goto fail;
        if (count == capacity) {
            size_t grown = capacity ? capacity * 2 : 64;
            void *next = realloc(values, grown * sizeof(*values));
            if (!next) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining NetQuake peer baselines"); goto fail; }
            values = next; capacity = grown;
        }
        values[count++] = entity;
    }
    for (;;) {
        if (!qa_application_network_q1_entity_next(peer->host->frontend->application, player,
            &cursor, &present, &actor, &entity, error)) goto fail;
        if (!present) break;
        if (entity.number <= world->max_clients) continue;
        qa_bounds bounds; bool modeled;
        if (!qa_application_network_q1_bounds(peer->host->frontend->application, player, actor, &bounds, &modeled, error)) goto fail;
        if (!modeled) continue;
        if (!qa_application_network_q1_baseline(peer->host->frontend->application, player, &entity, &entity, error)) goto fail;
        if (count == capacity) {
            size_t grown = capacity ? capacity * 2 : 64;
            void *next = realloc(values, grown * sizeof(*values));
            if (!next) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining NetQuake peer baselines"); goto fail; }
            values = next; capacity = grown;
        }
        values[count++] = entity;
    }
    free(peer->baselines); peer->baselines = values; peer->baseline_count = count; return true;
fail:
    free(values); return false;
}
static bool source_signon(void *context, qa_net_client_id id, uint8_t stage,
    qa_q1_emit_fn emit, void *output, qa_error *error)
{
    nq_frontend_peer *peer = context; frontend_nq_host *host = peer->host;
    if (!qa_net_client_id_equal(id, peer->client)) return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake signon uses another peer identity");
    qa_actor_id actor; qa_application_network_q1_world world;
    if (!peer_actor(peer, &actor, error) || !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
    nq_batch batch = {.emit = emit, .context = output, .host = host}; qa_nq_options options = {.standard_quake = world.standard_quake};
    qa_nq_message message;
    if (stage == 1) {
        const char *models[255], *sounds[255]; size_t model_count, sound_count;
        if (!capture_baselines(peer, actor, &world, error) ||
            !qa_application_network_q1_precache(host->frontend->application, host->owner, true, models, &model_count, error) ||
            !qa_application_network_q1_precache(host->frontend->application, host->owner, false, sounds, &sound_count, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_SERVERINFO, .data.serverinfo = {.protocol = world.protocol,
            .max_clients = (uint8_t)world.max_clients, .game_type = world.deathmatch ? 1 : 0, .level = world.level,
            .models = models, .sounds = sounds, .model_count = model_count, .sound_count = sound_count}};
        if (!batch_message(&batch, &message, options, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_SETVIEW, .data.value = peer->source_slot};
        if (!batch_message(&batch, &message, options, error)) return false;
    } else if (stage == 2) {
        size_t count;
        if (!qa_application_network_q1_signon_count(host->frontend->application, host->owner, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            qa_application_protocol_event event;
            if (!qa_application_network_q1_signon_at(host->frontend->application, host->owner, i, &event, error) ||
                !batch_bytes(&batch, event.payload, error)) return false;
        }
        for (size_t i = 0; i < peer->baseline_count; ++i) {
            message = (qa_nq_message){.op = QA_NQ_BASELINE, .data.entity = peer->baselines[i]};
            if (!batch_message(&batch, &message, options, error)) return false;
        }
    } else if (stage == 3) {
        if (!qa_application_remote_player_begin(host->frontend->application, peer->client, peer->seat, error) ||
            !peer_actor(peer, &actor, error) || !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_PAUSE, .data.value = qa_application_q1_paused(host->frontend->application)};
        if (!batch_message(&batch, &message, options, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_TIME, .data.seconds = world.seconds};
        if (!batch_message(&batch, &message, options, error)) return false;
        for (unsigned i = 0; i < 64; ++i) {
            message = (qa_nq_message){.op = QA_NQ_LIGHTSTYLE, .data.indexed_text = {(uint8_t)i, world.lightstyles[i]}};
            if (!batch_message(&batch, &message, options, error)) return false;
        }
        qa_application_network_q1_status_player players[255]; size_t count;
        if (!qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error) ||
            !status_messages(&batch, players, count, options, error)) return false;
        const int32_t stats[] = {world.total_secrets, world.total_monsters, world.found_secrets, world.killed_monsters};
        for (unsigned i = 0; i < 4; ++i) {
            message = (qa_nq_message){.op = QA_NQ_STAT, .data.indexed = {(uint8_t)(11 + i), stats[i]}};
            if (!batch_message(&batch, &message, options, error)) return false;
        }
        qa_q1_entity state;
        if (!qa_application_network_q1_entity(host->frontend->application, actor, actor, &state, error)) return false;
        message.op = QA_NQ_SETANGLE; memcpy(message.data.angles, state.angles, sizeof(state.angles));
        if (!batch_message(&batch, &message, options, error)) return false;
        message.op = QA_NQ_CLIENTDATA;
        if (!qa_application_network_q1_clientdata(host->frontend->application, actor, &message.data.clientdata, error) ||
            !batch_message(&batch, &message, options, error)) return false;
    } else return frontend_fail(error, QA_ERROR_ARGUMENT, "Unknown NetQuake source signon stage");
    return batch_flush(&batch, error);
}
static bool source_begin(void *context, qa_net_client_id id, qa_error *error)
{
    nq_frontend_peer *peer = context;
    return qa_net_client_id_equal(id, peer->client) &&
        qa_application_remote_player_begin(peer->host->frontend->application, id, peer->seat, error);
}
static bool source_input(void *context, qa_net_client_id id, const qa_q1_command *command,
    uint64_t sequence, qa_error *error)
{
    nq_frontend_peer *peer = context; qa_actor_id actor;
    if (!qa_net_client_id_equal(id, peer->client) || !sequence || sequence <= peer->input_sequence ||
        !peer_actor(peer, &actor, error)) return false;
    if (!command || !isfinite(command->time))
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake ping receipt has an invalid source acknowledgement timestamp");
    double ping = ((double)peer->host->published_source_time_ns / 1e9 - command->time) * 1000;
    if (ping < 0) ping = 0;
    if (peer->ping_count == NQ_PINGS)
        memmove(peer->pings, peer->pings + 1, (NQ_PINGS - 1) * sizeof(*peer->pings));
    else ++peer->ping_count;
    peer->pings[peer->ping_count - 1] = ping;
    peer->latest = *command; peer->input_sequence = sequence; peer->command_present = true;
    if (command->impulse) peer->impulse = command->impulse;
    return true;
}
static bool source_drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    nq_frontend_peer *peer = context;
    if (!qa_net_client_id_equal(id, peer->client)) return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake drop uses another native peer");
    peer->retiring = true; snprintf(peer->reason, sizeof(peer->reason), "%s", reason); return true;
}
static bool number_space(char value)
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' || value == '\f';
}
static int32_t color_number(const char *text)
{
    while (number_space(*text)) ++text;
    const char *limit = text + strlen(text);
    while (limit > text && number_space(limit[-1])) --limit;
    if (limit == text) return 0;
    unsigned base = 0;
    if (limit - text >= 2 && text[0] == '0') {
        if (text[1] == 'x' || text[1] == 'X') base = 16;
        else if (text[1] == 'b' || text[1] == 'B') base = 2;
        else if (text[1] == 'o' || text[1] == 'O') base = 8;
    }
    double number;
    if (base) {
        const char *digit = text + 2; uint64_t value = 0; bool overflow = false;
        if (digit == limit) return 0;
        for (; digit != limit; ++digit) {
            unsigned decoded;
            if (*digit >= '0' && *digit <= '9') decoded = (unsigned)(*digit - '0');
            else if (*digit >= 'a' && *digit <= 'f') decoded = (unsigned)(*digit - 'a') + 10;
            else if (*digit >= 'A' && *digit <= 'F') decoded = (unsigned)(*digit - 'A') + 10;
            else return 0;
            if (decoded >= base) return 0;
            if (!overflow) {
                if (value > (UINT64_MAX - decoded) / base) overflow = true;
                else value = value * base + decoded;
            }
        }
        if (overflow) return 0;
        number = (double)value;
    } else {
        if ((*text == '+' || *text == '-') && limit - text >= 3 && text[1] == '0' &&
            (text[2] == 'x' || text[2] == 'X')) return 0;
        char *end; number = strtod(text, &end);
        if (end != limit || !isfinite(number)) return 0;
    }
    return (int32_t)fmod(trunc(number), 16);
}
static bool source_chat(nq_frontend_peer *sender, qa_actor_id actor, bool team_only,
    const char *cursor, qa_error *error)
{
    char argument[NQ_MESSAGE], body[127]; size_t used = 0; bool first = true, present;
    for (;;) {
        if (!qa_q1_token(&cursor, true, argument, sizeof(argument), &present, error)) return false;
        if (!present) break;
        if (!first && used < sizeof(body) - 1) body[used++] = ' ';
        size_t length = strlen(argument), remaining = sizeof(body) - 1 - used;
        if (length > remaining) length = remaining;
        memcpy(body + used, argument, length); used += length; first = false;
    }
    body[used] = 0;
    qa_actor_id recipients[255]; size_t count; const char *name;
    frontend_nq_host *host = sender->host;
    if (!qa_application_network_q1_chat_recipients(host->frontend->application, actor,
        team_only, &name, recipients, &count, error)) return false;
    char text[NQ_MESSAGE]; size_t name_length = strlen(name);
    if (name_length > sizeof(text) - used - 6)
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source chat exceeds native reliable message extent");
    text[0] = 1; memcpy(text + 1, name, name_length);
    size_t offset = name_length + 1; text[offset++] = ':'; text[offset++] = ' ';
    memcpy(text + offset, body, used); offset += used; text[offset++] = '\n'; text[offset] = 0;
    uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = text};
    if (!qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
        (qa_nq_options){.standard_quake = true}, &message, NULL, 0)) return false;
    qa_net_client_id selected[NQ_CLIENTS]; size_t selected_count = 0;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        qa_actor_id recipient;
        if (!peer_actor(peer, &recipient, error)) return false;
        for (size_t j = 0; j < count; ++j) if (qa_actor_id_equal(recipient, recipients[j])) {
            selected[selected_count++] = peer->client;
            break;
        }
    }
    for (size_t i = 0; i < selected_count; ++i)
        if (!qa_network_nq_server_reliable(host->runtime, selected[i],
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    return true;
}
static size_t ordered_peers(frontend_nq_host *host, nq_frontend_peer *ordered[NQ_CLIENTS])
{
    size_t count = 0;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        size_t at = count;
        while (at && ordered[at - 1]->admission_order > peer->admission_order) {
            ordered[at] = ordered[at - 1]; --at;
        }
        ordered[at] = peer; ++count;
    }
    return count;
}
static bool source_ping(nq_frontend_peer *sender, qa_error *error)
{
    frontend_nq_host *host = sender->host;
    nq_frontend_peer *ordered[NQ_CLIENTS]; size_t count = ordered_peers(host, ordered);
    char text[NQ_MESSAGE] = "Client ping times:\n";
    size_t used = strlen(text);
    for (size_t i = 0; i < count; ++i) {
        nq_frontend_peer *peer = ordered[i]; double sum = 0;
        for (size_t j = 0; j < peer->ping_count; ++j) sum += peer->pings[j];
        double ping = peer->ping_count ? trunc(sum / peer->ping_count) : 0;
        const char *name = host->published_names[peer->source_slot - 1];
        if (!name || !*name) name = "unconnected";
        qa_buffer number = {0};
        if (!qa_unified_checkpoint_number(ping == 0 ? 0 : ping, &number, error)) return false;
        int length = snprintf(text + used, sizeof(text) - used, "%.*s %s\n",
            (int)number.size, (const char *)number.data, name);
        qa_buffer_free(&number);
        if (length < 0 || (size_t)length >= sizeof(text) - used)
            return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake ping response exceeds its actual reliable message extent");
        used += (size_t)length;
    }
    uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = text};
    return qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
        (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        qa_network_nq_server_reliable(host->runtime, sender->client,
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
static bool source_status(nq_frontend_peer *sender, qa_error *error)
{
    frontend_nq_host *host = sender->host; qa_application_network_q1_world world;
    if (!qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
    nq_frontend_peer *ordered[NQ_CLIENTS]; size_t count = ordered_peers(host, ordered);
    char text[NQ_MESSAGE];
    int length = snprintf(text, sizeof(text), "map: %s\nplayers: %zu active (%u max)\n",
        world.map, count, world.max_clients);
    if (length < 0 || (size_t)length >= sizeof(text))
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake status header exceeds its actual reliable message extent");
    size_t used = (size_t)length;
    for (size_t i = 0; i < count; ++i) {
        nq_frontend_peer *peer = ordered[i]; char address[128];
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
        if (!client) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake status peer has no actual connection");
        if (!qa_net_address_format(&client->endpoint, address, sizeof(address), error)) return false;
        const char *name = host->published_names[peer->source_slot - 1];
        if (!name || !*name) name = "unconnected";
        length = snprintf(text + used, sizeof(text) - used, "#%u %s %s\n", peer->source_slot, name, address);
        if (length < 0 || (size_t)length >= sizeof(text) - used)
            return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake status response exceeds its actual reliable message extent");
        used += (size_t)length;
    }
    uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = text};
    return qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
        (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        qa_network_nq_server_reliable(host->runtime, sender->client,
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
static bool source_command(void *context, qa_net_client_id id, const char *text, qa_error *error)
{
    nq_frontend_peer *peer = context; qa_actor_id actor;
    if (!qa_net_client_id_equal(id, peer->client) || !peer_actor(peer, &actor, error)) return false;
    const char *cursor = text; char command[32], first[1024], second[1024]; bool present;
    if (!qa_q1_token(&cursor, true, command, sizeof(command), &present, error)) return false;
    if (!present) return true;
    if (!strcmp(command, "ping")) return source_ping(peer, error);
    if (!strcmp(command, "status")) return source_status(peer, error);
    if (!strcmp(command, "say") || !strcmp(command, "say_team"))
        return source_chat(peer, actor, !strcmp(command, "say_team"), cursor, error);
    if (!strcmp(command, "kill"))
        return qa_application_network_q1_kill(peer->host->frontend->application, actor, error);
    if (!strcmp(command, "pause")) {
        qa_buffer text = {0}; bool changed;
        if (!qa_application_network_q1_pause(peer->host->frontend->application, actor, &text, &changed, error)) return false;
        uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = (const char *)text.data};
        bool ok = qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
            (qa_nq_options){.standard_quake = true}, &message, NULL, 0);
        qa_buffer_free(&text);
        for (size_t i = 0; ok && i < NQ_CLIENTS; ++i) {
            nq_frontend_peer *target = peer->host->peers + i;
            if (!target->occupied || target->retiring || (!changed && target != peer)) continue;
            ok = qa_network_nq_server_reliable(peer->host->runtime, target->client,
                (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
        }
        return ok;
    }
    if (!strcmp(command, "name")) {
        if (!qa_q1_token(&cursor, true, first, sizeof(first), &present, error)) return false;
        return qa_application_network_q1_name(peer->host->frontend->application, actor, present ? first : "unconnected", error);
    }
    if (!strcmp(command, "color")) {
        if (!qa_q1_token(&cursor, true, first, sizeof(first), &present, error)) return false;
        int32_t top = present ? color_number(first) : 0;
        if (!qa_q1_token(&cursor, true, second, sizeof(second), &present, error)) return false;
        int32_t bottom = present ? color_number(second) : top;
        return qa_application_network_q1_colors(peer->host->frontend->application, actor, top, bottom, error);
    }
    static const char *const allowed[] = {"god", "notarget", "fly", "noclip",
        "kick", "give", "ban"};
    for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); ++i)
        if (!strcmp(command, allowed[i])) return qa_application_actor_command(peer->host->frontend->application, actor, text, error);
    uint8_t bytes[96]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = "Unknown client command\n"};
    return qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15}, (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        qa_network_nq_server_reliable(peer->host->runtime, id, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
bool frontend_nq_source_hooks(frontend_nq_host *host, const qa_net_client *client,
    qa_network_nq_server_policy *policy, qa_network_nq_server_hooks *hooks, qa_error *error)
{
    nq_frontend_peer *peer = NULL;
    if (!host || !client || !policy || !hooks)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing retained NetQuake source binding");
    for (size_t i = 0; i < NQ_CLIENTS; ++i)
        if (host->peers[i].occupied && qa_net_client_id_equal(host->peers[i].client, client->id)) peer = host->peers + i;
    if (!peer || peer->host != host || client->attachment != QA_NET_REMOTE || client->protocol.kind != QA_NET_NQ15 ||
        client->protocol.flags || client->protocol.revision || client->seat_count != 1 || client->seats[0].remote_index ||
        client->seats[0].seat.owner != peer->seat.owner || client->seats[0].seat.index != peer->seat.index ||
        !qa_sha256_equal(&client->composition, &host->composition))
        return frontend_fail(error, QA_ERROR_FORMAT, "Restored NetQuake source is not its declared native connection seat");
    size_t cursor = 0; qa_application_network_player row; bool found = false;
    while (qa_application_network_player_next(host->frontend->application, &cursor, &row)) {
        if (!qa_net_client_id_equal(row.client, client->id) || row.seat.owner != peer->seat.owner || row.seat.index != peer->seat.index) continue;
        if (found || row.application_seat != peer->seat.index ||
            row.client_slot != peer->source_slot - 1 || (!peer->retiring && (row.retiring || row.deferred)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake peer differs from its actual application roster");
        if (!peer->retiring) {
            qa_actor_id actor;
            if (!peer_actor(peer, &actor, error) || !qa_actor_id_equal(actor, row.actor)) return false;
        }
        found = true;
    }
    if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake source has no actual application player");
    *policy = (qa_network_nq_server_policy){NQ_MESSAGE, NQ_DATAGRAM, 16u * NQ_MESSAGE};
    *hooks = (qa_network_nq_server_hooks){.context = peer, .signon = source_signon, .begin = source_begin,
        .command = source_command, .input = source_input, .drop = source_drop};
    return true;
}
static bool server_info(void *context, qa_nq_control *out, qa_error *error)
{
    frontend_nq_host *host = context; qa_application_network_q1_world world;
    qa_application_network_q1_status_player players[255]; size_t count;
    if (!host_source(host, error) || !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error) ||
        !qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error) ||
        !qa_net_address_format(qa_network_local_address(host->runtime), host->reply_address, sizeof(host->reply_address), error)) return false;
    qa_cvars *cvars = qa_application_network_q1_cvars(host->frontend->application, host->owner, error);
    if (!cvars) return false;
    const qa_cvar_view *name = qa_cvars_find(cvars, "hostname");
    *out = (qa_nq_control){.kind = QA_NQ_SERVER_INFO, .data.server = {.address = host->reply_address,
        .name = name ? name->value : world.level, .map = world.map, .players = (uint8_t)count,
        .max_players = (uint8_t)world.max_clients, .version = 3}};
    return true;
}
static bool player_info(void *context, uint8_t ordinal, bool *present, qa_nq_control *out, qa_error *error)
{
    frontend_nq_host *host = context;
    qa_application_network_q1_status_player players[255]; size_t count;
    if (!host_source(host, error) || !qa_application_network_q1_status(host->frontend->application,
        host->owner, players, &count, error)) return false;
    *present = ordinal < count; if (!*present) return true;
    qa_application_network_q1_status_player *player = players + ordinal;
    const qa_net_address *address = qa_network_local_address(host->runtime); uint64_t entered = 0;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->source_slot != player->source_slot) continue;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
        if (!client) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake query peer has no real connection");
        address = &client->endpoint; entered = peer->entered_ns; break;
    }
    if (!qa_net_address_format(address, host->reply_address, sizeof(host->reply_address), error)) return false;
    uint64_t seconds = host->frontend->time_ns >= entered ? (host->frontend->time_ns - entered) / UINT64_C(1000000000) : 0;
    *out = (qa_nq_control){.kind = QA_NQ_PLAYER_INFO, .data.player_info = {
        .player = (uint8_t)(player->source_slot - 1), .name = player->name, .colors = player->colors,
        .frags = player->frags, .seconds = seconds > INT32_MAX ? INT32_MAX : (int32_t)seconds, .address = host->reply_address}};
    return true;
}
static bool next_rule(void *context, const char *previous, bool *present, qa_qw_rule *out, qa_error *error)
{
    frontend_nq_host *host = context;
    if (!host_source(host, error)) return false;
    qa_cvars *cvars = qa_application_network_q1_cvars(host->frontend->application, host->owner, error);
    if (!cvars) return false;
    size_t start = 0;
    if (*previous) {
        bool found = false;
        for (size_t i = 0; i < qa_cvars_count(cvars); ++i)
            if (!strcmp(qa_cvars_at(cvars, i)->name, previous)) { start = i + 1; found = true; break; }
        if (!found) { *present = false; return true; }
    }
    for (size_t i = start; i < qa_cvars_count(cvars); ++i) {
        const qa_cvar_view *value = qa_cvars_at(cvars, i);
        if (!(value->flags & QA_CVAR_SERVERINFO)) continue;
        *out = (qa_qw_rule){value->name, value->value}; *present = true; return true;
    }
    *present = false; return true;
}
static bool connect_source(void *context, const qa_net_address *address, uint64_t now,
    qa_q1_connect_result *out, qa_error *error)
{
    frontend_nq_host *host = context;
    const qa_net_address *local = qa_network_local_address(host->runtime);
    if (!local || !local->port) return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake host has no actual bound UDP port");
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
        if (client && qa_net_address_equal(&client->endpoint, address, true)) {
            *out = (qa_q1_connect_result){QA_Q1_CONNECT_ACCEPT, local->port, NULL}; return true;
        }
    }
    qa_actor_id actor; qa_application_network_q1_world world;
    qa_application_network_q1_status_player players[255]; size_t count;
    if (!host_source(host, error) || !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error) ||
        !qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error)) return false;
    bool occupied[256] = {0};
    for (size_t i = 0; i < count; ++i) occupied[players[i].source_slot] = true;
    uint32_t slot = 1;
    while (slot <= world.max_clients && occupied[slot]) ++slot;
    nq_frontend_peer *peer = NULL;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) if (!host->peers[i].occupied) { peer = host->peers + i; break; }
    if (!peer || slot > world.max_clients) {
        *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_REJECT, .reason = "Server is full.\n"}; return true;
    }
    if (host->next_admission_order == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source admission order exhausted");
    *peer = (nq_frontend_peer){.host = host, .source_slot = slot, .entered_ns = now,
        .seat = {QA_NETWORK_COMMAND_OWNER, 128u + slot}};
    qa_net_seat_binding seat = {peer->seat, 0};
    qa_net_connect request = {.attachment = QA_NET_REMOTE, .endpoint = *address,
        .protocol = {.kind = QA_NET_NQ15}, .seats = &seat, .seat_count = 1, .composition = host->composition};
    qa_network_nq_server_policy policy = {.message_bytes = NQ_MESSAGE, .fragment_bytes = 1024,
        .queued_bytes = 16u * NQ_MESSAGE};
    qa_network_nq_server_hooks hooks = {.context = peer, .signon = source_signon, .begin = source_begin,
        .command = source_command, .input = source_input, .drop = source_drop};
    if (!qa_network_attach_nq_server(host->runtime, &request, &policy, &hooks, now, &peer->client, error)) return false;
    peer->occupied = true; peer->admission_order = host->next_admission_order++;
    qa_application_remote_player_request player = {.client = peer->client, .seat = peer->seat,
        .application_seat = peer->seat.index, .source_slot = slot, .name = "unconnected",
        .team = "", .skin = "", .userinfo = "", .defer_source_begin = true};
    if (!qa_application_remote_player_attach(host->frontend->application, &player, &actor, error) ||
        !peer_actor(peer, &actor, error) || !qa_network_nq_server_start(host->runtime, peer->client, error)) {
        qa_error first = error ? *error : (qa_error){0};
        if (!qa_network_detach(host->runtime, peer->client, "NetQuake source reservation failed", error)) return false;
        if (error) *error = first;
        return false;
    }
    *out = (qa_q1_connect_result){QA_Q1_CONNECT_ACCEPT, local->port, NULL}; return true;
}
bool frontend_nq_create(qa_frontend *frontend, qa_network_runtime *runtime,
    const qa_sha256_digest *composition, frontend_nq_host **out, qa_error *error)
{
    if (!frontend || !runtime || !composition || !out || *out || !frontend->application ||
        frontend->options.network_protocol.kind != QA_NET_NQ15 || frontend->options.network_protocol.flags ||
        frontend->options.network_protocol.revision || frontend->options.network_connect)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake host requires its actual frontend and sole runtime");
    frontend_nq_host *host = calloc(1, sizeof(*host));
    if (!host) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating NetQuake frontend source owner");
    host->frontend = frontend; host->runtime = runtime; host->composition = *composition;
    host->next_admission_order = 1;
    host->generation = qa_application_configuration_generation(frontend->application);
    host->previous_pause = qa_application_q1_paused(frontend->application);
    qa_application_network_q1_world world;
    if (!host_source(host, error) || !qa_application_network_q1_world_read(frontend->application, host->owner, &world, error)) {
        free(host); return false;
    }
    *out = host; return true;
}
void frontend_nq_destroy(frontend_nq_host *host)
{
    if (!host) return;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) free(host->peers[i].baselines);
    for (size_t i = 0; i < 256; ++i) {
        free(host->board[i].name); free(host->published_names[i]);
    }
    for (size_t i = 0; i < 64; ++i) free(host->styles[i]);
    free(host);
}
void frontend_nq_disconnected(frontend_nq_host *host, qa_net_client_id id)
{
    if (!host) return;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || !qa_net_client_id_equal(peer->client, id)) continue;
        free(peer->baselines); *peer = (nq_frontend_peer){0};
    }
}
bool frontend_nq_receive(frontend_nq_host *host, const qa_net_datagram *packet,
    bool *recognized, qa_error *error)
{
    if (!host || !packet || !recognized) return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing NetQuake control owner");
    *recognized = packet->payload.size >= 5 && packet->payload.data[0] == 128 && packet->payload.data[1] == 0 &&
        packet->payload.data[4] >= QA_NQ_CONNECT_REQUEST && packet->payload.data[4] <= QA_NQ_RULE_INFO_REQUEST;
    if (!*recognized) return true;
    if (packet->payload.size > sizeof(host->pending[0].bytes) || host->pending_count == NQ_PENDING) return true;
    nq_pending_control *pending = host->pending + host->pending_count++;
    pending->address = packet->from; pending->received_ns = packet->received_ns; pending->size = packet->payload.size;
    memcpy(pending->bytes, packet->payload.data, pending->size); return true;
}
bool frontend_nq_prepare(frontend_nq_host *host, qa_error *error)
{
    if (!host) return true;
    if (host->busy || !qa_network_callbacks_idle(host->runtime))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake source pump requires returned native callbacks");
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (peer->occupied && peer->retiring && !qa_network_detach(host->runtime, peer->client, peer->reason, error)) return false;
    }
    uint64_t generation = qa_application_configuration_generation(host->frontend->application);
    if (generation != host->generation) {
        qa_actor_id actor; qa_buffer identity = {0};
        if (!host_source(host, error) || !qa_launch_identity_encode(qa_application_launch(host->frontend->application),
            qa_session_actors(qa_application_session(host->frontend->application)), &identity, error)) return false;
        qa_sha256((qa_bytes){identity.data, identity.size}, &host->composition); qa_buffer_free(&identity);
        host->generation = generation; host->submillisecond_ns = 0;
        for (size_t i = 0; i < 256; ++i) { free(host->board[i].name); host->board[i] = (nq_status_cache){0}; }
        for (size_t i = 0; i < 64; ++i) { free(host->styles[i]); host->styles[i] = NULL; }
        for (size_t i = 0; i < NQ_CLIENTS; ++i) {
            nq_frontend_peer *peer = host->peers + i;
            if (!peer->occupied || peer->retiring) continue;
            peer->command_present = false; peer->impulse = 0;
            free(peer->baselines); peer->baselines = NULL; peer->baseline_count = 0;
            if (!peer_actor(peer, &actor, error) || !qa_network_restart(host->runtime, peer->client, &host->composition, error)) return false;
        }
    }
    return true;
}
bool frontend_nq_pump(frontend_nq_host *host, qa_error *error)
{
    if (!host) return true;
    if (!frontend_nq_prepare(host, error)) return false;
    size_t count = host->pending_count; host->pending_count = 0;
    qa_nq_connection_host callbacks = {.context = host, .server_info = server_info,
        .player_info = player_info, .next_rule = next_rule, .connect = connect_source};
    for (size_t i = 0; i < count; ++i) {
        nq_pending_control *pending = host->pending + i; uint8_t response[NQ_MESSAGE];
        qa_error local = {0}; qa_net_writer writer; qa_net_writer_init(&writer, response, sizeof(response), &local); bool present;
        if (!qa_nq_control_answer((qa_bytes){pending->bytes, pending->size}, &pending->address,
            pending->received_ns, &callbacks, &present, &writer, &local)) {
            if (local.code == QA_ERROR_MEMORY || qa_application_get_state(host->frontend->application) == QA_APPLICATION_FAULTED) {
                if (error) *error = local; return false;
            }
            frontend_print(host->frontend, local.message); continue;
        }
        if (present && !qa_network_send_address(host->runtime, &pending->address,
            (qa_bytes){response, qa_net_writer_size(&writer)}, error)) return false;
    }
    return true;
}
bool frontend_nq_tick(frontend_nq_host *host, uint64_t elapsed, bool retiring_map, qa_error *error)
{
    if (!host || retiring_map || !elapsed || qa_application_q1_paused(host->frontend->application)) return true;
    if (host->busy || !qa_network_callbacks_idle(host->runtime) || elapsed > UINT64_MAX - host->submillisecond_ns)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake source tick exceeds its idle clock boundary");
    uint64_t total = elapsed + host->submillisecond_ns, milliseconds = total / UINT64_C(1000000);
    if (milliseconds > UINT32_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source tick exceeds native movement duration");
    host->submillisecond_ns = total % UINT64_C(1000000);
    if (!milliseconds) return true;
    ++host->busy; bool ok = true;
    for (size_t i = 0; ok && i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring || !peer->command_present) continue;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
        if (!client || client->phase != QA_NET_ACTIVE) continue;
        qa_actor_id actor;
        if (peer->tick_sequence == UINT64_MAX) { ok = frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source tick sequence exhausted"); break; }
        if (!peer_actor(peer, &actor, error)) { ok = false; break; }
        qa_network_command command = {.client = peer->client, .seat = peer->seat, .actor = actor,
            .epoch = qa_network_epoch(host->runtime, peer->client), .movement = {.kind = QA_MOVEMENT_NETQUAKE,
            .sequence = peer->tick_sequence + 1, .milliseconds = (uint32_t)milliseconds,
            .acknowledged_server_seconds = peer->latest.time,
            .angles = {peer->latest.angles[0], peer->latest.angles[1], peer->latest.angles[2]},
            .forward_move = peer->latest.forward, .side_move = peer->latest.side, .up_move = peer->latest.up,
            .buttons = peer->latest.buttons, .impulse = peer->impulse}};
        ok = qa_network_accept(host->runtime, &command, error);
        if (ok) { ++peer->tick_sequence; peer->impulse = 0; }
    }
    --host->busy; return ok;
}
bool frontend_nq_idle(const frontend_nq_host *host)
{ return !host || !host->busy; }
static bool reliable_emit(void *context, qa_bytes bytes, qa_error *error)
{
    nq_frontend_peer *peer = context;
    return qa_network_nq_server_reliable(peer->host->runtime, peer->client, bytes, error);
}
static bool broadcast_emit(void *context, qa_bytes bytes, qa_error *error)
{
    frontend_nq_host *host = context;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        if (!reliable_emit(peer, bytes, error)) return false;
    }
    return true;
}
static char *copy_text(const char *text, qa_error *error)
{
    size_t length = strlen(text); char *copy = malloc(length + 1);
    if (!copy) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining NetQuake source status"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}
static bool publish_status(frontend_nq_host *host,
    const qa_application_network_q1_world *world, qa_error *error)
{
    qa_application_network_q1_status_player players[255]; size_t count;
    if (!qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error)) return false;
    nq_status_cache next[256] = {0}; char *styles[64] = {0};
    bool owned[256] = {0}, style_owned[64] = {0}, ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        uint32_t slot = players[i].source_slot;
        char *name = host->board[slot].name;
        if (!name || strcmp(name, players[i].name)) { name = copy_text(players[i].name, error); owned[slot] = name != NULL; }
        next[slot] = (nq_status_cache){.name = name,
            .frags = players[i].frags, .source_frags = players[i].source_frags,
            .colors = players[i].colors, .present = true};
        ok = next[slot].name != NULL;
    }
    for (size_t i = 0; ok && i < 64; ++i) {
        styles[i] = host->styles[i];
        if (!styles[i] || strcmp(styles[i], world->lightstyles[i])) {
            styles[i] = copy_text(world->lightstyles[i], error); style_owned[i] = styles[i] != NULL;
        }
        ok = styles[i] != NULL;
    }
    nq_batch batch = {.emit = broadcast_emit, .context = host, .host = host}; qa_nq_options options = {.standard_quake = world->standard_quake};
    for (uint32_t slot = 1; ok && slot <= world->max_clients; ++slot) {
        const nq_status_cache *old = host->board + slot, *current = next + slot;
        const char *before = old->name ? old->name : "", *after = current->name ? current->name : "";
        qa_nq_message message;
        if (old->present != current->present || strcmp(before, after)) {
            message = (qa_nq_message){.op = QA_NQ_NAME, .data.indexed_text = {(uint8_t)(slot - 1), after}};
            ok = batch_message(&batch, &message, options, error);
        }
        if (ok && (old->present != current->present || old->source_frags != current->source_frags)) {
            message = (qa_nq_message){.op = QA_NQ_FRAGS, .data.indexed = {(uint8_t)(slot - 1), wire_frags(current->frags)}};
            ok = batch_message(&batch, &message, options, error);
        }
        if (ok && (old->present != current->present || old->colors != current->colors)) {
            message = (qa_nq_message){.op = QA_NQ_COLORS, .data.indexed = {(uint8_t)(slot - 1), current->colors}};
            ok = batch_message(&batch, &message, options, error);
        }
    }
    for (unsigned i = 0; ok && i < 64; ++i)
        if (!host->styles[i] || strcmp(host->styles[i], styles[i])) {
            qa_nq_message message = {.op = QA_NQ_LIGHTSTYLE, .data.indexed_text = {(uint8_t)i, styles[i]}};
            ok = batch_message(&batch, &message, options, error);
        }
    if (ok) ok = batch_flush(&batch, error);
    if (ok) {
        for (size_t i = 0; i < 256; ++i) {
            if (host->board[i].name != next[i].name) free(host->board[i].name);
            host->board[i] = next[i]; owned[i] = false;
        }
        for (size_t i = 0; i < 64; ++i) {
            if (host->styles[i] != styles[i]) free(host->styles[i]);
            host->styles[i] = styles[i]; style_owned[i] = false;
        }
    }
    for (size_t i = 0; i < 256; ++i) if (owned[i]) free(next[i].name);
    for (size_t i = 0; i < 64; ++i) if (style_owned[i]) free(styles[i]);
    return ok;
}
static const qa_q1_entity *baseline(const nq_frontend_peer *peer, uint32_t number)
{
    size_t first = 0, end = peer->baseline_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (peer->baselines[middle].number < number) first = middle + 1; else end = middle;
    }
    return first < peer->baseline_count && peer->baselines[first].number == number ? peer->baselines + first : NULL;
}
static bool source_events(nq_frontend_peer *peer, qa_actor_id actor, qa_net_writer *datagram, qa_error *error)
{
    frontend_nq_host *host = peer->host; nq_batch reliable = {.emit = reliable_emit, .context = peer};
    qa_application *app = host->frontend->application;
    uint64_t generation = qa_application_protocol_events_generation(app);
    size_t count = qa_application_protocol_event_count(app);
    if (peer->protocol_generation != generation) {
        peer->protocol_generation = generation;
        peer->protocol_cursor = 0;
    }
    if (peer->protocol_cursor > count)
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake protocol cursor exceeds its actual retained event generation");
    uint8_t source[NQ_DATAGRAM]; size_t source_size = 0; bool source_full = false;
    for (size_t i = peer->protocol_cursor; i < count; ++i) {
        qa_application_protocol_event event;
        if (!qa_application_protocol_event_at(app, i, &event))
            return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source protocol event disappeared before publication");
        peer->protocol_cursor = i + 1;
        if (event.provider != host->owner || event.signon) continue;
        if (event.dialect != QA_CLOCK_NETQUAKE || event.multicast || event.destination < 0 || event.destination > 2)
            return frontend_fail(error, QA_ERROR_UNSUPPORTED, "NetQuake source event lacks its complete native destination contract");
        if (event.recipient.registry && !qa_actor_id_equal(event.recipient, actor)) continue;
        if (event.reliable) {
            if (!batch_bytes(&reliable, event.payload, error)) return false;
        } else if (!source_full) {
            if (event.payload.size > sizeof(source) - source_size) source_full = true;
            else {
                if (event.payload.size) memcpy(source + source_size, event.payload.data, event.payload.size);
                source_size += event.payload.size;
            }
        }
    }
    if (source_size <= datagram->capacity - qa_net_writer_size(datagram) &&
        !qa_net_write_data(datagram, source, source_size)) return false;
    return batch_flush(&reliable, error) &&
        ((generation == qa_application_protocol_events_generation(app)) ||
         frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake protocol publication replaced its retained source events"));
}
static bool publish_peer(nq_frontend_peer *peer, const qa_application_network_q1_world *world, qa_error *error)
{
    frontend_nq_host *host = peer->host; qa_actor_id actor;
    if (!peer_actor(peer, &actor, error)) return false;
    qa_network_nq_server_state state;
    if (!qa_network_nq_server_state_read(host->runtime, peer->client, &state, error)) return false;
    uint8_t bytes[NQ_DATAGRAM]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (state.stage != 4) return source_events(peer, actor, &writer, error);
    qa_nq_options options = {.standard_quake = world->standard_quake}; qa_net_protocol_id protocol = {.kind = QA_NET_NQ15};
    qa_nq_message message = {.op = QA_NQ_TIME, .data.seconds = world->seconds};
    if (!qa_nq_write(&writer, protocol, options, &message, NULL, 0)) return false;
    qa_application_network_q1_feedback feedback;
    if (!qa_application_network_q1_consume_feedback(host->frontend->application, actor, &feedback, error)) return false;
    if (feedback.damage && !qa_nq_write_damage(&writer, feedback.armor, feedback.blood, feedback.origin)) return false;
    if (feedback.set_angle) {
        message.op = QA_NQ_SETANGLE; memcpy(message.data.angles, feedback.angles, sizeof(feedback.angles));
        if (!qa_nq_write(&writer, protocol, options, &message, NULL, 0)) return false;
    }
    message.op = QA_NQ_CLIENTDATA;
    if (!qa_application_network_q1_clientdata(host->frontend->application, actor, &message.data.clientdata, error) ||
        !qa_nq_write(&writer, protocol, options, &message, NULL, 0)) return false;
    qa_collision_geometry *geometry = qa_world_geometry(qa_application_world(host->frontend->application));
    size_t extent = qa_collision_q1_pvs_bytes(geometry); uint8_t *pvs = extent ? malloc(extent) : NULL;
    if (!pvs) return frontend_fail(error, extent ? QA_ERROR_MEMORY : QA_ERROR_FORMAT, "NetQuake frame requires its actual source PVS row");
    qa_vec3 eye; bool ok = qa_application_network_q1_eye(host->frontend->application, actor, &eye, error) &&
        qa_collision_q1_fat_pvs(geometry, eye, pvs, extent, error);
    uint32_t cursor = 0; bool present;
    while (ok) {
        qa_actor_id entity_actor; qa_q1_entity entity;
        ok = qa_application_network_q1_entity_next(host->frontend->application, actor, &cursor,
            &present, &entity_actor, &entity, error);
        if (!ok || !present) break;
        if (!qa_actor_id_equal(actor, entity_actor)) {
            qa_bounds bounds; bool modeled, visible;
            ok = qa_application_network_q1_bounds(host->frontend->application, actor, entity_actor, &bounds, &modeled, error);
            if (!ok) break;
            if (!modeled) continue;
            ok = qa_collision_q1_bounds_visible(geometry, (qa_bytes){pvs, extent}, bounds, &visible, error);
            if (!ok) break;
            if (!visible) continue;
        }
        qa_q1_entity empty; qa_q1_entity_init(&empty); empty.number = entity.number;
        const qa_q1_entity *saved = baseline(peer, entity.number);
        uint8_t encoded[64]; qa_net_writer item; qa_net_writer_init(&item, encoded, sizeof(encoded), error);
        ok = qa_nq_write_entity(&item, protocol, &entity, saved ? saved : &empty, world->seconds);
        if (!ok) break;
        size_t size = qa_net_writer_size(&item);
        if (size > sizeof(bytes) - qa_net_writer_size(&writer)) break;
        ok = qa_net_write_data(&writer, encoded, size);
    }
    free(pvs);
    return ok && source_events(peer, actor, &writer, error) &&
        qa_network_nq_server_frame(host->runtime, peer->client, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
bool frontend_nq_publish(frontend_nq_host *host, qa_error *error)
{
    if (!host) return true;
    if (host->busy || !qa_network_callbacks_idle(host->runtime))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake source publisher requires returned native callbacks");
    if (!frontend_nq_pump(host, error)) return false;
    qa_application_network_q1_world world;
    if (!host_source(host, error) || !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
    qa_clock_state clock;
    if (!qa_session_clock(qa_application_session(host->frontend->application), host->owner, &clock) ||
        clock.frame.provider != host->owner || clock.frame.kind != QA_CLOCK_NETQUAKE ||
        clock.frame.phase != QA_FRAME_EXIT)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake ping publication lacks its completed source clock");
    host->published_source_time_ns = clock.frame.time_ns;
    ++host->busy; bool ok = true;
    bool paused = qa_application_q1_paused(host->frontend->application);
    if (paused != host->previous_pause) {
        uint8_t bytes[2]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        qa_nq_message message = {.op = QA_NQ_PAUSE, .data.value = paused};
        ok = qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
            (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
            broadcast_emit(host, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
        if (ok) host->previous_pause = paused;
    }
    if (ok) ok = publish_status(host, &world, error);
    for (size_t i = 0; ok && i < NQ_CLIENTS; ++i)
        if (host->peers[i].occupied && !host->peers[i].retiring) ok = publish_peer(host->peers + i, &world, error);
    --host->busy; return ok;
}
