#include "network_nq_private.h"
#include "view_settings.h"
#include "qa/application_network.h"
#include "qa/collision.h"
#include "qa/launch_identity.h"
#include "qa/network_save.h"
#include "qa/q1_chat_commands.h"
#include "qa/text.h"
#include <float.h>
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
    qa_net_protocol_id protocol;
} nq_batch;
static char *copy_text(const char *, qa_error *);
static bool reliable_emit(void *, qa_bytes, qa_error *);
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
    if (!qa_nq_write(&writer, batch->protocol, options, message, NULL, 0)) return false;
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
static bool batch_source_emit(void *context, qa_bytes bytes, qa_error *error)
{ return batch_bytes(context, bytes, error); }
static bool source_convert_values(frontend_nq_host *host, qa_net_protocol_id protocol,
    const qa_q1_entity *baselines, size_t baseline_count, qa_bytes bytes, qa_nq_options options,
    qa_q1_emit_fn emit, void *context, qa_error *error)
{
    if (protocol.kind == QA_NET_NQ15) return emit(context, bytes, error);
    uint8_t scratch[NQ_MESSAGE]; qa_net_writer writer; qa_net_reader reader;
    qa_net_writer_init(&writer, scratch, sizeof(scratch), error); qa_net_reader_init(&reader, bytes, error);
    return qa_nq_transcode_original(&reader, &writer, protocol, options, baselines,
        baseline_count, (float)((double)host->published_source_time_ns / 1e9), emit, context);
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
static bool capture_baselines(frontend_nq_host *host, qa_actor_id player,
    const qa_application_network_q1_world *world, qa_q1_entity **saved, size_t *saved_count, qa_error *error)
{
    qa_q1_entity *values = NULL; size_t count = 0, capacity = 0;
    uint32_t cursor = 0; bool present; qa_actor_id actor; qa_q1_entity entity;
    for (uint32_t slot = 1; slot <= world->max_clients; ++slot) {
        if (!qa_application_network_q1_client_baseline(host->frontend->application, player, slot, &entity, error)) goto fail;
        if (count == capacity) {
            size_t grown = capacity ? capacity * 2 : 64;
            void *next = realloc(values, grown * sizeof(*values));
            if (!next) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining NetQuake peer baselines"); goto fail; }
            values = next; capacity = grown;
        }
        values[count++] = entity;
    }
    for (;;) {
        if (!qa_application_network_q1_entity_next(host->frontend->application, player,
            &cursor, &present, &actor, &entity, error)) goto fail;
        if (!present) break;
        if (entity.number <= world->max_clients) continue;
        qa_bounds bounds; bool modeled;
        if (!qa_application_network_q1_bounds(host->frontend->application, player, actor, &bounds, &modeled, error)) goto fail;
        if (!modeled) continue;
        if (!qa_application_network_q1_baseline(host->frontend->application, player, &entity, &entity, error)) goto fail;
        if (count == capacity) {
            size_t grown = capacity ? capacity * 2 : 64;
            void *next = realloc(values, grown * sizeof(*values));
            if (!next) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining NetQuake peer baselines"); goto fail; }
            values = next; capacity = grown;
        }
        values[count++] = entity;
    }
    free(*saved); *saved = values; *saved_count = count; return true;
fail:
    free(values); return false;
}
static bool signon_payload(frontend_nq_host *host, qa_actor_id actor, uint32_t slot,
    qa_net_protocol_id protocol, const qa_q1_entity *baselines, size_t baseline_count, uint8_t stage,
    qa_q1_emit_fn emit, void *output, qa_error *error)
{
    qa_application_network_q1_world world;
    if (!qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
    nq_batch batch = {.emit = emit, .context = output, .host = host, .protocol = protocol}; qa_nq_options options = {.standard_quake = world.standard_quake};
    qa_nq_message message;
    if (stage == 1) {
        const char *models[255], *sounds[255]; size_t model_count, sound_count;
        if (!qa_application_network_q1_precache(host->frontend->application, host->owner, true, models, &model_count, error) ||
            !qa_application_network_q1_precache(host->frontend->application, host->owner, false, sounds, &sound_count, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_SERVERINFO, .data.serverinfo = {.protocol = protocol,
            .max_clients = (uint8_t)world.max_clients, .game_type = world.deathmatch ? 1 : 0, .level = world.level,
            .models = models, .sounds = sounds, .model_count = model_count, .sound_count = sound_count}};
        if (!batch_message(&batch, &message, options, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_CDTRACK, .data.cd = {world.cd_track, world.cd_track}};
        if (!batch_message(&batch, &message, options, error)) return false;
        message = (qa_nq_message){.op = QA_NQ_SETVIEW, .data.value = slot};
        if (!batch_message(&batch, &message, options, error)) return false;
    } else if (stage == 2) {
        size_t count;
        if (!qa_application_network_q1_signon_count(host->frontend->application, host->owner, &count, error)) return false;
        for (size_t i = 0; i < count; ++i) {
            qa_application_protocol_event event;
            uint8_t bytes[QA_APPLICATION_PROTOCOL_SCRATCH_BYTES]; qa_net_writer writer;
            qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
            if (!qa_application_network_q1_signon_at(host->frontend->application, host->owner, i, &event, error) ||
                !qa_application_protocol_event_encode(&event, &writer) ||
                !source_convert_values(host, protocol, baselines, baseline_count, event.payload, options, batch_source_emit, &batch, error)) return false;
        }
        for (size_t i = 0; i < baseline_count; ++i) {
            message = (qa_nq_message){.op = QA_NQ_BASELINE, .data.entity = baselines[i]};
            if (!batch_message(&batch, &message, options, error)) return false;
        }
    } else if (stage == 3) {
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
static bool source_signon(void *context, qa_net_client_id id, uint8_t stage,
    qa_q1_emit_fn emit, void *output, qa_error *error)
{
    nq_frontend_peer *peer=context;frontend_nq_host *host=peer->host;
    if(!qa_net_client_id_equal(id,peer->client))return frontend_fail(error,QA_ERROR_ARGUMENT,"NetQuake signon uses another peer identity");
    qa_actor_id actor;qa_application_network_q1_world world;
    if(!peer_actor(peer,&actor,error)||!qa_application_network_q1_world_read(host->frontend->application,host->owner,&world,error))return false;
    if(stage==1&&!capture_baselines(host,actor,&world,&peer->baselines,&peer->baseline_count,error))return false;
    if(stage==3&&(!qa_application_remote_player_begin(host->frontend->application,peer->client,peer->seat,error)||
        !peer_actor(peer,&actor,error)))return false;
    return signon_payload(host,actor,peer->source_slot,host->frontend->options.network_protocol,
        peer->baselines,peer->baseline_count,stage,emit,output,error);
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
static int32_t color_number(const char *text)
{
    return (int32_t)((uint32_t)strtol(text, NULL, 10) & 15u);
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
    return qa_nq_write(&writer, host->frontend->options.network_protocol,
        (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        reliable_emit(sender,
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
    return qa_nq_write(&writer, host->frontend->options.network_protocol,
        (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        reliable_emit(sender,
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
static bool source_command(void *context, qa_net_client_id id, const char *text, qa_error *error)
{
    nq_frontend_peer *peer = context; frontend_nq_host *host = peer->host; qa_actor_id actor;
    if (!qa_net_client_id_equal(id, peer->client) || !peer_actor(peer, &actor, error)) return false;
    const char *cursor = text; char command[32], first[1024], second[1024]; bool present;
    if (!qa_q1_token(&cursor, true, command, sizeof(command), &present, error)) return false;
    if (!present) return true;
    if (!strcmp(command, "ping")) return source_ping(peer, error);
    if (!strcmp(command, "status")) return source_status(peer, error);
    qa_q1_chat_mode chat = qa_q1_chat_command_read(QA_RULESET_NETQUAKE, command, false);
    if (chat != QA_Q1_CHAT_UNKNOWN)
        return qa_application_actor_command(host->frontend->application, actor, text, error);
    if (!strcmp(command, "kill"))
        return qa_application_network_q1_kill(peer->host->frontend->application, actor, error);
    if (!strcmp(command, "pause")) {
        qa_buffer pause_text = {0}; bool changed;
        if (!qa_application_network_q1_pause(peer->host->frontend->application, actor, &pause_text, &changed, error)) return false;
        uint8_t bytes[NQ_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        qa_nq_message message = {.op = QA_NQ_PRINT, .data.text = (const char *)pause_text.data};
        bool ok = qa_nq_write(&writer, host->frontend->options.network_protocol,
            (qa_nq_options){.standard_quake = true}, &message, NULL, 0);
        qa_buffer_free(&pause_text);
        for (size_t i = 0; ok && i < NQ_CLIENTS; ++i) {
            nq_frontend_peer *target = peer->host->peers + i;
            if (!target->occupied || target->retiring || (!changed && target != peer)) continue;
            ok = reliable_emit(target,
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
    return qa_nq_write(&writer, host->frontend->options.network_protocol, (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
        reliable_emit(peer, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
bool frontend_nq_source_hooks(frontend_nq_host *host, const qa_net_client *client,
    qa_network_nq_server_policy *policy, qa_network_nq_server_hooks *hooks, qa_error *error)
{
    nq_frontend_peer *peer = NULL;
    if (!host || !client || !policy || !hooks)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing retained NetQuake source binding");
    for (size_t i = 0; i < NQ_CLIENTS; ++i)
        if (host->peers[i].occupied && qa_net_client_id_equal(host->peers[i].client, client->id)) peer = host->peers + i;
    if (!peer || peer->host != host ||
        (client->attachment != QA_NET_REMOTE && client->attachment != QA_NET_LOCAL_SEAT) ||
        client->protocol.kind != host->frontend->options.network_protocol.kind ||
        client->protocol.flags != host->frontend->options.network_protocol.flags ||
        client->protocol.revision != host->frontend->options.network_protocol.revision || client->seat_count != 1 || client->seats[0].remote_index ||
        client->seats[0].seat.owner != peer->seat.owner || client->seats[0].seat.index != peer->seat.index ||
        client->composition != host->composition)
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
    uint64_t seconds = host->frontend->wall_time_ns >= entered ?
        (host->frontend->wall_time_ns - entered) / UINT64_C(1000000000) : 0;
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
    const qa_cvar_view *value = NULL;
    if (*previous) {
        value = qa_cvars_find(cvars, previous);
        if (!value || strcmp(value->name, previous)) { *present = false; return true; }
    }
    while ((value = qa_cvars_next(cvars, value))) {
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
    qa_net_seat_id local_seat = {0}; uint32_t application_seat = 0;
    bool local_player = frontend_network_local_seat(host->frontend, address, &local_seat, &application_seat);
    if (!host_source(host, error) ||
        !qa_application_network_q1_world_read(host->frontend->application, host->owner, &world, error)) return false;
    uint32_t slot = 1;
    if (local_player) {
        qa_actor_owner owner; qa_net_protocol_id source_protocol;
        if (!qa_application_player_actor(host->frontend->application, application_seat, &actor) ||
            !qa_application_network_q1_source(host->frontend->application, actor, &owner, &slot, &source_protocol, error)) return false;
    } else {
        qa_application_network_q1_status_player players[255]; size_t count;
        if (!qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error)) return false;
        bool occupied[256] = {0};
        for (size_t i = 0; i < count; ++i) occupied[players[i].source_slot] = true;
        while (slot <= world.max_clients && occupied[slot]) ++slot;
    }
    nq_frontend_peer *peer = NULL;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) if (!host->peers[i].occupied) { peer = host->peers + i; break; }
    if (!peer || (!local_player && slot > world.max_clients)) {
        *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_REJECT, .reason = "Server is full.\n"}; return true;
    }
    if (host->next_admission_order == UINT64_MAX)
        return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source admission order exhausted");
    qa_application_network_q1_host source;
    if (!qa_application_network_q1_host_source(host->frontend->application, &source, error) ||
        source.owner != host->owner)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake connect lost its actual source admission");
    *peer = (nq_frontend_peer){.host = host, .source_slot = slot, .entered_ns = now,
        .seat = local_player ? local_seat : (qa_net_seat_id){QA_NETWORK_COMMAND_OWNER, 128u + slot}};
    peer->protocol_cursor = qa_application_events_local_first(host->frontend->application);
    qa_event_receipts_reset(&peer->event_receipts, peer->protocol_cursor);
    qa_net_seat_binding seat = {peer->seat, 0};
    qa_net_connect request = {.attachment = local_player ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE, .endpoint = *address,
        .protocol = host->frontend->options.network_protocol, .seats = &seat, .seat_count = 1, .composition = host->composition};
    qa_network_nq_server_policy policy = {.message_bytes = NQ_MESSAGE, .fragment_bytes = 1024,
        .queued_bytes = 16u * NQ_MESSAGE};
    qa_network_nq_server_hooks hooks = {.context = peer, .signon = source_signon, .begin = source_begin,
        .command = source_command, .input = source_input, .drop = source_drop};
    if (!qa_network_attach_nq_server(host->runtime, &request, &policy, &hooks, now, &peer->client, error)) return false;
    peer->occupied = true; peer->admission_order = host->next_admission_order++;
    qa_application_remote_player_request player = {.client = peer->client, .seat = peer->seat,
        .application_seat = peer->seat.index, .source_slot = slot, .name = "unconnected",
        .team = "", .skin = "", .userinfo = source.source_board_events ? "\\language\\english" : "",
        .defer_source_begin = true};
    bool attached = local_player ? qa_application_network_local_bind(host->frontend->application,
        qa_net_connections_get(qa_network_connections(host->runtime), peer->client), peer->seat, application_seat, error) :
        qa_application_remote_player_attach(host->frontend->application, &player, &actor, error);
    if (!attached ||
        !peer_actor(peer, &actor, error) || !qa_network_nq_server_start(host->runtime, peer->client, error)) {
        qa_error first = error ? *error : (qa_error){0};
        if (!qa_network_detach(host->runtime, peer->client, "NetQuake source reservation failed", error)) return false;
        if (error) *error = first;
        return false;
    }
    *out = (qa_q1_connect_result){QA_Q1_CONNECT_ACCEPT, local->port, NULL}; return true;
}
static bool source_observer_create(qa_frontend *frontend,qa_network_runtime *runtime,
    frontend_nq_host **out,qa_error *error)
{
    frontend_nq_host *host = calloc(1, sizeof(*host));
    if (!host) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating NetQuake frontend source owner");
    host->frontend = frontend; host->runtime = runtime;
    host->next_admission_order = 1;
    host->generation = qa_application_configuration_generation(frontend->application);
    host->previous_pause = qa_application_q1_paused(frontend->application);
    qa_application_network_q1_world world;
    if (!host_source(host, error) || !qa_application_network_q1_world_read(frontend->application, host->owner, &world, error)) {
        free(host); return false;
    }
    *out = host; return true;
}
bool frontend_nq_create(qa_frontend *frontend, qa_network_runtime *runtime,
    const uint64_t *composition, frontend_nq_host **out, qa_error *error)
{
    if (!frontend || !runtime || !composition || !out || *out || !frontend->application ||
        frontend->options.network_protocol.kind > QA_NET_RMQ999 ||
        !qa_q1_profile_valid(frontend->options.network_protocol, error) || frontend->options.network_connect)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake host requires its actual frontend and sole runtime");
    if(!source_observer_create(frontend,runtime,out,error))return false;
    (*out)->composition=*composition;return true;
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
        qa_actor_id actor;
        if (!host_source(host, error)) return false;
        host->composition = generation;
        host->generation = generation; host->submillisecond_ns = 0;
        for (size_t i = 0; i < 256; ++i) { free(host->board[i].name); host->board[i] = (nq_status_cache){0}; }
        for (size_t i = 0; i < 64; ++i) { free(host->styles[i]); host->styles[i] = NULL; }
        for (size_t i = 0; i < NQ_CLIENTS; ++i) {
            nq_frontend_peer *peer = host->peers + i;
            if (!peer->occupied || peer->retiring) continue;
            peer->command_present = false; peer->impulse = 0;
            free(peer->baselines); peer->baselines = NULL; peer->baseline_count = 0;
            if (!peer_actor(peer, &actor, error) || !qa_network_restart(host->runtime, peer->client, &host->composition, error)) return false;
            peer->protocol_cursor = qa_application_events_local_first(host->frontend->application);
            qa_event_receipts_reset(&peer->event_receipts, peer->protocol_cursor);
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
                if (error) *error = local;
                return false;
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
        qa_player_state selected;
        if (!peer_actor(peer, &actor, error)) { ok = false; break; }
        if (!qa_application_control_read(host->frontend->application, actor, &selected)) {
            ok = frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake source peer has no actual selected control state"); break;
        }
        qa_network_nq_source_command command = {.client = peer->client, .seat = peer->seat, .actor = actor,
            .epoch = qa_network_epoch(host->runtime, peer->client), .sequence = peer->tick_sequence + 1,
            .source_owner = host->owner, .source_slot = peer->source_slot,
            .movement = selected.state.kind, .command = peer->latest};
        command.command.impulse = peer->impulse;
        ok = qa_network_accept_nq_source_command(host->runtime, &command, error);
        if (ok) { ++peer->tick_sequence; peer->impulse = 0; }
    }
    --host->busy; return ok;
}
bool frontend_nq_idle(const frontend_nq_host *host)
{ return !host || !host->busy; }
static bool reliable_emit(void *context, qa_bytes bytes, qa_error *error)
{
    nq_frontend_peer *peer = context;
    if (peer->retiring) return true;
    qa_network_nq_server_state state; qa_network_nq_server_policy policy;
    if (!qa_network_nq_server_state_read(peer->host->runtime, peer->client, &state, error) ||
        !qa_network_nq_server_policy_read(peer->host->runtime, peer->client, &policy, error)) return false;
    if (bytes.size <= policy.message_bytes && bytes.size > policy.queued_bytes - state.queued_bytes)
        return source_drop(peer, peer->client, "Reliable message overflow", error);
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
    const qa_application_network_q1_world *world, qa_net_protocol_id protocol,
    qa_q1_emit_fn emit, void *context, qa_error *error)
{
    qa_application_network_q1_host source;
    if (!qa_application_network_q1_host_source(host->frontend->application, &source, error) ||
        source.owner != host->owner || source.protocol.kind != QA_NET_NQ15 ||
        source.client_slots != world->max_clients)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake status lost its actual source producer");
    qa_application_network_q1_status_player players[255]; size_t count;
    if (!qa_application_network_q1_status(host->frontend->application, host->owner, players, &count, error)) return false;
    nq_status_cache next[256] = {0}; char *styles[64] = {0};
    bool owned[256] = {0}, style_owned[64] = {0}, ok = true;
    char *published[255] = {0}; bool published_owned[255] = {0};
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
    for (uint32_t slot = 1; ok && source.source_board_events && slot <= world->max_clients; ++slot) {
        const char *name = next[slot].name ? next[slot].name : "";
        published[slot - 1] = host->published_names[slot - 1];
        if (!published[slot - 1] || strcmp(published[slot - 1], name)) {
            published[slot - 1] = copy_text(name, error);
            published_owned[slot - 1] = published[slot - 1] != NULL;
        }
        ok = published[slot - 1] != NULL;
    }
    nq_batch batch = {.emit = emit, .context = context, .host = host, .protocol = protocol}; qa_nq_options options = {.standard_quake = world->standard_quake};
    for (uint32_t slot = 1; ok && !source.source_board_events && slot <= world->max_clients; ++slot) {
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
    for (unsigned i = 0; ok && !source.source_board_events && i < 64; ++i)
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
        for (uint32_t slot = 1; source.source_board_events && slot <= world->max_clients; ++slot) {
            if (host->published_names[slot - 1] != published[slot - 1]) free(host->published_names[slot - 1]);
            host->published_names[slot - 1] = published[slot - 1]; published_owned[slot - 1] = false;
        }
    }
    for (size_t i = 0; i < 256; ++i) if (owned[i]) free(next[i].name);
    for (size_t i = 0; i < 64; ++i) if (style_owned[i]) free(styles[i]);
    for (size_t i = 0; i < 255; ++i) if (published_owned[i]) free(published[i]);
    return ok;
}
static const qa_q1_entity *baseline(const qa_q1_entity *values, size_t count, uint32_t number)
{
    size_t first = 0, end = count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        if (values[middle].number < number) first = middle + 1; else end = middle;
    }
    return first < count && values[first].number == number ? values + first : NULL;
}
typedef struct nq_source_datagram {
    uint8_t bytes[NQ_DATAGRAM];
    size_t size;
    bool full;
} nq_source_datagram;
static bool source_datagram_emit(void *context, qa_bytes bytes, qa_error *error)
{
    nq_source_datagram *source = context; (void)error;
    if (!source->full) {
        if (bytes.size > sizeof(source->bytes) - source->size) source->full = true;
        else { memcpy(source->bytes + source->size, bytes.data, bytes.size); source->size += bytes.size; }
    }
    return true;
}
static bool source_event_payload(frontend_nq_host *host, qa_actor_id actor, qa_net_protocol_id protocol,
    const qa_q1_entity *baselines,size_t baseline_count,uint64_t *saved_generation,uint64_t *saved_cursor,
    qa_net_writer *datagram,const qa_application_network_q1_world *world,qa_q1_emit_fn emit,void *context,qa_error *error)
{
    nq_batch reliable = {.emit = emit, .context = context, .host = host, .protocol = protocol};
    qa_application *app = host->frontend->application;
    uint64_t generation = qa_application_protocol_events_generation(app);
    uint64_t first = qa_application_events_local_first(app);
    uint64_t next = qa_application_events_next(app);
    if (*saved_generation != generation) {
        *saved_generation = generation;
    }
    if (*saved_cursor < first) *saved_cursor = first;
    uint8_t source[NQ_DATAGRAM]; size_t source_size = 0; bool source_full = false;
    qa_nq_options options = {.standard_quake = world->standard_quake};
    for (uint64_t i = *saved_cursor; i < next; ++i) {
        qa_application_protocol_event event;
        *saved_cursor = i + 1;
        for (size_t projection = 0; qa_application_protocol_event_at(app, i, projection, &event); ++projection) {
            if (event.provider != host->owner || event.signon) continue;
            if (event.dialect != QA_RULESET_NETQUAKE || event.multicast || event.destination < 0 || event.destination > 2)
                return frontend_fail(error, QA_ERROR_UNSUPPORTED, "NetQuake source event lacks its complete native destination contract");
            if (event.recipient.registry && !qa_actor_id_equal(event.recipient, actor)) continue;
            uint8_t bytes[QA_APPLICATION_PROTOCOL_SCRATCH_BYTES]; qa_net_writer writer;
            qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
            if (!qa_application_protocol_event_encode(&event, &writer)) return false;
            if (event.reliable) {
                if (!source_convert_values(host, protocol, baselines, baseline_count, event.payload, options, batch_source_emit, &reliable, error)) return false;
            } else if (!source_full && protocol.kind == QA_NET_NQ15) {
                if (event.payload.size > sizeof(source) - source_size) source_full = true;
                else {
                    if (event.payload.size) memcpy(source + source_size, event.payload.data, event.payload.size);
                    source_size += event.payload.size;
                }
            } else if (!source_full) {
                nq_source_datagram converted = {0};
                if (!source_convert_values(host, protocol, baselines, baseline_count, event.payload, options, source_datagram_emit, &converted, error)) return false;
                if (converted.full || converted.size > sizeof(source) - source_size) source_full = true;
                else {
                    memcpy(source + source_size, converted.bytes, converted.size);
                    source_size += converted.size;
                }
            }
        }
    }
    if (source_size <= datagram->capacity - qa_net_writer_size(datagram) &&
        !qa_net_write_data(datagram, source, source_size)) return false;
    return batch_flush(&reliable, error) &&
        ((generation == qa_application_protocol_events_generation(app)) ||
         frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake protocol publication replaced its retained source events"));
}
typedef struct nq_event_submission {
    nq_frontend_peer *peer;
    uint64_t first;
} nq_event_submission;
static bool event_reliable_emit(void *context, qa_bytes bytes, qa_error *error)
{
    nq_event_submission *submission = context;
    nq_frontend_peer *peer = submission->peer;
    qa_network_nq_server_state state;
    if (!qa_network_nq_server_state_read(peer->host->runtime, peer->client, &state, error)) return false;
    (void)qa_event_receipts_retired(&peer->event_receipts, state.reliable_acknowledged);
    if (qa_event_receipts_full(&peer->event_receipts))
        return source_drop(peer, peer->client, "Reliable event overflow", error);
    if (!reliable_emit(peer, bytes, error)) return false;
    if (peer->retiring) return true;
    if (!qa_network_nq_server_state_read(peer->host->runtime, peer->client, &state, error)) return false;
    return qa_event_receipts_submit(&peer->event_receipts, submission->first,
        peer->protocol_cursor, state.reliable_queued);
}
static bool source_events(nq_frontend_peer *peer,qa_actor_id actor,qa_net_writer *datagram,
    const qa_application_network_q1_world *world,qa_error *error)
{
    uint64_t first = qa_application_events_local_first(peer->host->frontend->application);
    nq_event_submission submission = {.peer = peer,
        .first = peer->protocol_cursor < first ? first : peer->protocol_cursor};
    return source_event_payload(peer->host,actor,peer->host->frontend->options.network_protocol,
        peer->baselines,peer->baseline_count,&peer->protocol_generation,&peer->protocol_cursor,
        datagram,world,event_reliable_emit,&submission,error);
}
static bool frame_payload(frontend_nq_host *host,qa_actor_id actor,qa_net_protocol_id protocol,
    const qa_q1_entity *baselines,size_t baseline_count,const qa_application_network_q1_world *world,
    qa_net_writer *writer,qa_error *error)
{
    qa_nq_options options = {.standard_quake = world->standard_quake};
    qa_nq_message message = {.op = QA_NQ_TIME, .data.seconds = world->seconds};
    if (!qa_nq_write(writer, protocol, options, &message, NULL, 0)) return false;
    qa_application_network_q1_feedback feedback;
    if (!qa_application_network_q1_consume_feedback(host->frontend->application, actor, &feedback, error)) return false;
    if (feedback.damage) {
        if (protocol.kind == QA_NET_NQ15) {
            if (!qa_nq_write_damage(writer, feedback.armor, feedback.blood, feedback.origin)) return false;
        } else {
            message = (qa_nq_message){.op = QA_NQ_DAMAGE,
                .data.damage = {.armor = feedback.armor, .blood = feedback.blood}};
            for (size_t i = 0; i < 3; ++i) message.data.damage.origin[i] = (float)feedback.origin[i];
            if (!qa_nq_write(writer, protocol, options, &message, NULL, 0)) return false;
        }
        /* Local demo recording owns this real feedback consumption before
         * presentation. Project that same receipt for its admitted seat. */
        qa_vec3 from;
        if (!frontend_view_q1_damage_origin(feedback.origin,&from,error) ||
            !frontend_view_q1_local_damage(host->frontend,actor,feedback.armor,feedback.blood,from,error)) return false;
    }
    if (feedback.set_angle) {
        message.op = QA_NQ_SETANGLE; memcpy(message.data.angles, feedback.angles, sizeof(feedback.angles));
        if (!qa_nq_write(writer, protocol, options, &message, NULL, 0)) return false;
    }
    message.op = QA_NQ_CLIENTDATA;
    if (!qa_application_network_q1_clientdata(host->frontend->application, actor, &message.data.clientdata, error) ||
        !qa_nq_write(writer, protocol, options, &message, NULL, 0)) return false;
    qa_collision_geometry *geometry = qa_world_geometry(qa_application_world(host->frontend->application));
    size_t extent = qa_collision_q1_pvs_bytes(geometry); uint8_t *pvs = extent ? malloc(extent) : NULL;
    if (!pvs) return frontend_fail(error, extent ? QA_ERROR_MEMORY : QA_ERROR_FORMAT, "NetQuake frame requires its actual source PVS row");
    qa_vec3 eye; bool ok = qa_application_network_q1_eye(host->frontend->application, actor, &eye, error) &&
        qa_collision_q1_fat_pvs(geometry,
            qa_world_trace_scratch(qa_application_world(host->frontend->application),geometry),
            eye, pvs, extent, error);
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
            ok = qa_world_q1_visible(qa_application_world(host->frontend->application), entity_actor,
                &bounds, (qa_bytes){pvs, extent}, &visible, error);
            if (!ok) break;
            if (!visible) continue;
        }
        qa_q1_entity empty; qa_q1_entity_init(&empty); empty.number = entity.number;
        const qa_q1_entity *saved = baseline(baselines, baseline_count, entity.number);
        uint8_t encoded[64]; qa_net_writer item; qa_net_writer_init(&item, encoded, sizeof(encoded), error);
        ok = qa_nq_write_entity(&item, protocol, &entity, saved ? saved : &empty, world->seconds);
        if (!ok) break;
        size_t size = qa_net_writer_size(&item);
        if (size > writer->capacity - qa_net_writer_size(writer)) break;
        ok = qa_net_write_data(writer, encoded, size);
    }
    free(pvs);
    return ok;
}
static bool publish_peer(nq_frontend_peer *peer,const qa_application_network_q1_world *world,qa_error *error)
{
    frontend_nq_host *host=peer->host;qa_actor_id actor;
    if(!peer_actor(peer,&actor,error))return false;
    qa_network_nq_server_state state;
    if(!qa_network_nq_server_state_read(host->runtime,peer->client,&state,error))return false;
    uint8_t bytes[NQ_DATAGRAM];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
    if(state.stage!=4) {
        if (!source_events(peer,actor,&writer,world,error)) return false;
        return qa_event_receipts_submit(&peer->event_receipts, peer->event_receipts.through,
            peer->protocol_cursor, 0);
    }
    if (!(frame_payload(host,actor,host->frontend->options.network_protocol,peer->baselines,peer->baseline_count,world,&writer,error)&&
        source_events(peer,actor,&writer,world,error)&&
        (peer->retiring || qa_network_nq_server_frame(host->runtime,peer->client,(qa_bytes){bytes,qa_net_writer_size(&writer)},error)))) return false;
    return qa_event_receipts_submit(&peer->event_receipts, peer->event_receipts.through,
        peer->protocol_cursor, 0);
}
uint64_t frontend_nq_events_retired(frontend_nq_host *host)
{
    if (!host) return UINT64_MAX;
    uint64_t retired = qa_application_events_next(host->frontend->application);
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        nq_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        if (!peer->event_receipts.through)
            qa_event_receipts_reset(&peer->event_receipts, qa_application_events_local_first(host->frontend->application));
        qa_network_nq_server_state state = {0};
        (void)qa_network_nq_server_state_read(host->runtime, peer->client, &state, NULL);
        uint64_t first = qa_event_receipts_retired(&peer->event_receipts, state.reliable_acknowledged);
        if (first < retired) retired = first;
    }
    return retired;
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
        clock.frame.provider != host->owner || clock.frame.kind != QA_RULESET_NETQUAKE ||
        clock.frame.phase != QA_FRAME_EXIT)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "NetQuake ping publication lacks its completed source clock");
    host->published_source_time_ns = clock.frame.time_ns;
    ++host->busy; bool ok = true;
    bool paused = qa_application_q1_paused(host->frontend->application);
    if (paused != host->previous_pause) {
        uint8_t bytes[2]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        qa_nq_message message = {.op = QA_NQ_PAUSE, .data.value = paused};
        ok = qa_nq_write(&writer, host->frontend->options.network_protocol,
            (qa_nq_options){.standard_quake = true}, &message, NULL, 0) &&
            broadcast_emit(host, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
        if (ok) host->previous_pause = paused;
    }
    if (ok) ok = publish_status(host, &world, host->frontend->options.network_protocol, broadcast_emit, host, error);
    for (size_t i = 0; ok && i < NQ_CLIENTS; ++i)
        if (host->peers[i].occupied && !host->peers[i].retiring) ok = publish_peer(host->peers + i, &world, error);
    --host->busy; return ok;
}

/* A recording observes the same real Source host and player without admitting
 * a transport peer, acquiring a client slot or repeating ClientBegin. */
typedef struct nq_demo_source {
    frontend_nq_host *source;
    qa_actor_id actor;
    uint32_t seat,slot;
    qa_net_protocol_id protocol;
    qa_q1_entity *baselines;
    size_t baseline_count;
    uint64_t protocol_cursor,protocol_generation,last_frame;
    frontend_demo_sink sink;
    float angles[3];
    bool published;
} nq_demo_source;
static bool nq_demo_current(const void *context)
{
    const nq_demo_source *record=context;qa_actor_id actor;
    qa_actor_owner owner;uint32_t slot;qa_net_protocol_id protocol;
    return record&&record->source&&record->source->frontend->application&&
        record->source->generation==qa_application_configuration_generation(record->source->frontend->application)&&
        qa_application_player_actor(record->source->frontend->application,record->seat,&actor)&&
        qa_actor_id_equal(actor,record->actor)&&
        qa_application_network_q1_source(record->source->frontend->application,actor,&owner,&slot,&protocol,NULL)&&
        owner==record->source->owner&&slot==record->slot&&(protocol.kind==record->protocol.kind&&protocol.flags==record->protocol.flags&&protocol.revision==record->protocol.revision);
}
static bool nq_demo_angles(nq_demo_source *record,qa_error *error)
{
    qa_player_state view;
    if(!qa_application_control_read(record->source->frontend->application,record->actor,&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording lost its actual player view angles");
    record->angles[0]=view.view_angles.x;record->angles[1]=view.view_angles.y;record->angles[2]=view.view_angles.z;return true;
}
typedef struct nq_demo_output {nq_demo_source *source;const frontend_demo_sink *sink;} nq_demo_output;
static bool nq_demo_emit(void *context,qa_bytes bytes,qa_error *error)
{
    nq_demo_output *output=context;
    frontend_demo_packet packet={.format=FRONTEND_DEMO_NQ,.value.nq={.message=bytes}};
    memcpy(packet.value.nq.angles,output->source->angles,sizeof(output->source->angles));
    return output->sink->append(output->sink->owner,&packet,error);
}
static bool nq_demo_live_emit(void *context,qa_bytes bytes,qa_error *error)
{
    qa_error write_error={0};(void)error;
    (void)nq_demo_emit(context,bytes,&write_error);return true;
}
static bool nq_demo_seed_status(void *context,qa_bytes bytes,qa_error *error)
{
    /* The same rows were emitted by signon stage three. Capture that actual
     * status as the observer baseline without duplicating it in the file. */
    (void)context;(void)bytes;(void)error;return true;
}
static bool nq_demo_seed(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    nq_demo_source *record=context;frontend_nq_host *host=record->source;
    qa_application_network_q1_world world;
    if(!nq_demo_current(record)||!nq_demo_angles(record,error)||
        !qa_application_network_q1_world_read(host->frontend->application,host->owner,&world,error)||
        !capture_baselines(host,record->actor,&world,&record->baselines,&record->baseline_count,error))return false;
    nq_demo_output output={record,sink};
    for(uint8_t stage=1;stage<=3;++stage) {
        if(!signon_payload(host,record->actor,record->slot,record->protocol,record->baselines,
            record->baseline_count,stage,nq_demo_emit,&output,error))return false;
        uint8_t bytes[2];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
        qa_nq_message marker={.op=QA_NQ_SIGNON,.data.value=stage};
        if(!qa_nq_write(&writer,record->protocol,(qa_nq_options){.standard_quake=world.standard_quake},&marker,NULL,0)||
            !nq_demo_emit(&output,(qa_bytes){bytes,qa_net_writer_size(&writer)},error))return false;
    }
    if(!publish_status(host,&world,record->protocol,nq_demo_seed_status,NULL,error))return false;
    record->protocol_generation=qa_application_protocol_events_generation(host->frontend->application);
    record->protocol_cursor=qa_application_events_next(host->frontend->application);
    return true;
}
static bool nq_demo_attach(void *context,const frontend_demo_sink *sink,bool *attached,qa_error *error)
{
    nq_demo_source *record=context;*attached=false;
    if(!nq_demo_current(record)||record->sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording has no unattached Source");
    record->sink=*sink;*attached=true;return true;
}
static bool nq_demo_detach(void *context,const frontend_demo_sink *sink,qa_error *error)
{
    nq_demo_source *record=context;
    if(record->sink.owner!=sink->owner||record->sink.append!=sink->append)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording lost its retained sink");
    record->sink=(frontend_demo_sink){0};return true;
}
static bool nq_demo_publish(void *context,qa_error *error)
{
    nq_demo_source *record=context;frontend_nq_host *host=record->source;
    qa_application *app=host->frontend->application;qa_clock_state clock;
    if(!nq_demo_current(record)||!record->sink.append||!qa_network_callbacks_idle(host->runtime)||
        !qa_session_clock(qa_application_session(app),host->owner,&clock)||
        clock.frame.provider!=host->owner||clock.frame.kind!=QA_RULESET_NETQUAKE||clock.frame.phase!=QA_FRAME_EXIT)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording requires its completed Source frame");
    if(record->published&&record->last_frame==clock.frame.number&&
        record->protocol_generation==qa_application_protocol_events_generation(app)&&
        record->protocol_cursor==qa_application_events_next(app)&&
        host->previous_pause==qa_application_q1_paused(app))return true;
    host->published_source_time_ns=clock.frame.time_ns;
    qa_application_network_q1_world world;
    if(!nq_demo_angles(record,error)||!qa_application_network_q1_world_read(app,host->owner,&world,error))return false;
    /* File faults remain owned by the common service; they do not abort the
     * actual Source frame or consume another player's feedback. */
    qa_error write_error={0};nq_demo_output output={record,&record->sink};
    bool paused=qa_application_q1_paused(app);
    if(paused!=host->previous_pause) {
        uint8_t bytes[2];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
        qa_nq_message message={.op=QA_NQ_PAUSE,.data.value=paused};
        if(!qa_nq_write(&writer,record->protocol,(qa_nq_options){.standard_quake=world.standard_quake},&message,NULL,0))return false;
        (void)nq_demo_emit(&output,(qa_bytes){bytes,qa_net_writer_size(&writer)},&write_error);
        host->previous_pause=paused;
    }
    /* Continue deriving Source output even if writing failed; each emitter
     * absorbs only the recording fault, preserving Source producer errors. */
    if(!publish_status(host,&world,record->protocol,nq_demo_live_emit,&output,error))return false;
    uint8_t bytes[64000];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
    if(!frame_payload(host,record->actor,record->protocol,record->baselines,record->baseline_count,&world,&writer,error)||
        !source_event_payload(host,record->actor,record->protocol,record->baselines,record->baseline_count,
            &record->protocol_generation,&record->protocol_cursor,&writer,&world,nq_demo_live_emit,&output,error))return false;
    (void)nq_demo_emit(&output,(qa_bytes){bytes,qa_net_writer_size(&writer)},&write_error);
    record->last_frame=clock.frame.number;record->published=true;return true;
}
static bool nq_demo_release(void **owner,qa_error *error)
{
    nq_demo_source *record=owner?*owner:NULL;if(!record)return true;
    if(record->sink.append)return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording still owns its Source sink");
    free(record->baselines);frontend_nq_destroy(record->source);free(record);*owner=NULL;return true;
}
bool frontend_nq_demo_record(qa_frontend *f,qa_network_runtime *runtime,qa_actor_id actor,qa_fs_root *root,
    frontend_demo_record_source *out,qa_error *error)
{
    if(!f||!f->application||!runtime||!root||!out)return frontend_fail(error,QA_ERROR_ARGUMENT,"Local NQ recording requires its actual Source owners");
    nq_demo_source *record=calloc(1,sizeof(*record));
    if(!record)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining local NQ recording Source");
    frontend_nq_host *host=NULL;
    if(!source_observer_create(f,runtime,&host,error)){free(record);return false;}
    record->source=host;record->actor=actor;
    if(!qa_application_player_seat(f->application,actor,&record->seat)||
        !qa_application_network_q1_source(f->application,actor,&host->owner,&record->slot,&record->protocol,error)||
        !host_source(host,error)||record->protocol.kind!=QA_NET_NQ15||record->protocol.flags||record->protocol.revision) {
        free(host);free(record);return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Local recording requires its genuine classic NetQuake Source player");
    }
    *out=(frontend_demo_record_source){.owner=record,.format=FRONTEND_DEMO_NQ,.protocol=record->protocol,.root=root,.forced_track=-1,
        .current=nq_demo_current,.seed=nq_demo_seed,.attach=nq_demo_attach,.detach=nq_demo_detach,
        .publish=nq_demo_publish,.release=nq_demo_release};return true;
}

static bool state_valid(const frontend_nq_host *host, bool complete_clock, qa_error *error)
{
    if (!host || !host->frontend || !host->frontend->application || !host->runtime || host->busy ||
        !host->frontend->options.network_host || host->frontend->options.network_connect ||
        host->frontend->options.network_protocol.kind > QA_NET_RMQ999 ||
        !qa_q1_profile_valid(host->frontend->options.network_protocol, error) || !host->owner ||
        host->generation != qa_application_configuration_generation(host->frontend->application) ||
        host->submillisecond_ns >= UINT64_C(1000000) || host->pending_count > NQ_PENDING || !host->next_admission_order)
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host lacks its actual source generation and idle policy");
    qa_application_network_q1_host source;
    if (!qa_application_network_q1_host_source(host->frontend->application, &source, error) ||
        source.owner != host->owner || source.protocol.kind != QA_NET_NQ15 ||
        source.protocol.flags || source.protocol.revision)
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host changes its primary classic source owner");
    uint32_t client_slots, entity_slots; const char *models[255]; size_t model_count; uint32_t player_model = 0;
    if (!qa_application_network_q1_extents(host->frontend->application, host->owner, &client_slots, &entity_slots, error) ||
        !qa_application_network_q1_precache(host->frontend->application, host->owner, true, models, &model_count, error)) return false;
    for (size_t i = 0; i < model_count; ++i) if (!strcmp(models[i], "progs/player.mdl")) player_model = (uint32_t)i + 1;
    if (host->composition != host->generation)
        return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host changes its launch generation");
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        const nq_frontend_peer *p = host->peers + i;
        if ((p->host && p->host != host) || (p->client.owner && p->client.owner != QA_NETWORK_COMMAND_OWNER) ||
            (p->seat.owner && p->seat.owner != QA_NETWORK_COMMAND_OWNER) || (p->baseline_count && !p->baselines) ||
            p->baseline_count > UINT16_MAX || !memchr(p->reason, 0, sizeof(p->reason)) ||
            p->ping_count > NQ_PINGS ||
            (!p->occupied && (p->retiring || p->command_present || p->baseline_count || p->ping_count || p->admission_order ||
             p->protocol_generation || p->protocol_cursor)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake physical peer has invalid owned storage");
        double maximum_ping = ((double)UINT64_MAX / 1e9 + (double)FLT_MAX) * 1000;
        for (size_t j = 0; j < NQ_PINGS; ++j)
            if (!isfinite(p->pings[j]) || p->pings[j] < 0 || p->pings[j] > maximum_ping ||
                (j >= p->ping_count && p->pings[j] != 0))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake ping history differs from its received timestamp domain");
        if (!p->occupied) continue;
        uint64_t protocol_generation = qa_application_protocol_events_generation(host->frontend->application);
        if (p->protocol_generation > protocol_generation)
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake generation differs from its real protocol event owner");
        if (p->host != host || p->client.owner != QA_NETWORK_COMMAND_OWNER || !p->client.generation || p->client.slot >= NQ_CLIENTS ||
            p->seat.owner != QA_NETWORK_COMMAND_OWNER || !p->source_slot || p->source_slot > client_slots ||
            p->baseline_count < client_slots || !player_model ||
            !p->admission_order || p->admission_order >= host->next_admission_order ||
            p->ping_count != (p->input_sequence < NQ_PINGS ? p->input_sequence : NQ_PINGS) ||
            (p->command_present && !p->input_sequence) ||
            (!p->command_present && p->impulse) || (complete_clock && p->entered_ns > host->frontend->wall_time_ns))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake peer changes its actual native seat or input identity");
        for (size_t j = 0; j < i; ++j) if (host->peers[j].occupied &&
            (qa_net_client_id_equal(p->client, host->peers[j].client) || p->source_slot == host->peers[j].source_slot ||
             p->admission_order == host->peers[j].admission_order))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake peers alias one source client or connection");
        for (size_t j = 0; j < p->baseline_count; ++j) {
            const qa_q1_entity *e = p->baselines + j;
            uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
            qa_nq_message message = {.op = QA_NQ_BASELINE, .data.entity = *e};
            if (!e->number || e->number > UINT16_MAX || e->number >= entity_slots || e->model > model_count ||
                (j && e->number <= p->baselines[j - 1].number) || e->effects || e->step ||
                (j < client_slots && (e->number != j + 1 || e->model != player_model || e->colormap != e->number)) ||
                (j >= client_slots && (!e->model || e->colormap)) ||
                !qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15},
                    (qa_nq_options){.standard_quake = true}, &message, NULL, 0))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake baseline lacks its original source wire admission");
        }
    }
    for (size_t i = 0; i < NQ_PENDING; ++i) {
        const nq_pending_control *p = host->pending + i;
        if (p->size > sizeof(p->bytes) || (i < host->pending_count && !p->size) ||
            (p->size && (p->size < 5 || p->bytes[0] != 128 || p->bytes[1] != 0 ||
                p->bytes[4] < QA_NQ_CONNECT_REQUEST || p->bytes[4] > QA_NQ_RULE_INFO_REQUEST ||
                (p->address.kind != QA_NET_LOOPBACK && !p->address.port) ||
                (p->address.kind != QA_NET_IPV4 && p->address.kind != QA_NET_IPV6 && p->address.kind != QA_NET_LOOPBACK))) ||
            (complete_clock && p->received_ns > host->frontend->wall_time_ns))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake query queue differs from its actual receive producer");
    }
    for (size_t i = 0; i < 256; ++i) {
        const nq_status_cache *v = host->board + i;
        if (host->published_names[i] && strlen(host->published_names[i]) > NQ_MESSAGE - 3)
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake published name exceeds its real source service extent");
        if (!isfinite(v->source_frags) || (v->present && (!i || !v->name || (v->colors & 15) > 13 || (v->colors >> 4) > 13)) ||
            (!v->present && (v->name || v->frags || v->source_frags != 0 || v->colors)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake source status cache has invalid native ownership");
        int32_t frags = qa_source_float_to_i32(v->source_frags);
        if (v->frags != frags) return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake cache score differs from its actual source value");
    }
    return true;
}
bool frontend_nq_qualified(const frontend_nq_host *host, bool complete_clock, qa_error *error)
{
    if (!state_valid(host, complete_clock, error) || !qa_network_callbacks_idle(host->runtime)) return false;
    size_t expected = 0;
    for (size_t i = 0; i < NQ_CLIENTS; ++i) {
        const nq_frontend_peer *p = host->peers + i; if (!p->occupied) continue;
        ++expected;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), p->client);
        const qa_q1_peer *native = qa_network_nq_server_view(host->runtime, p->client);
        qa_network_nq_server_state state; qa_network_nq_server_policy policy, declared; qa_network_nq_server_hooks hooks;
        if (!client || !native || native->kind != QA_Q1_PEER_NETQUAKE || !native->channel.nq || !native->transport ||
            !qa_net_address_equal(&native->remote, &client->endpoint, true) ||
            qa_net_transport_address(native->transport) != qa_network_local_address(host->runtime) || client->connected_ns != p->entered_ns ||
            !frontend_nq_source_hooks((frontend_nq_host *)host, client, &declared, &hooks, error) ||
            !qa_network_nq_server_policy_read(host->runtime, p->client, &policy, error) ||
            policy.message_bytes != declared.message_bytes || policy.fragment_bytes != declared.fragment_bytes ||
            policy.queued_bytes != declared.queued_bytes || !qa_network_nq_server_state_read(host->runtime, p->client, &state, error) ||
            !state.started || !state.stage || state.input_sequence != p->input_sequence || state.retiring != p->retiring)
            return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake host/native channel/source policy inventories differ");
        if (complete_clock) {
            bool present; uint64_t sequence;
            if (!qa_network_accepted_sequence(host->runtime, p->client, p->seat, &present, &sequence, error) ||
                (present && (!sequence || sequence != p->tick_sequence || state.stage != 4)) ||
                (!present && (sequence || (p->tick_sequence && qa_network_epoch(host->runtime, p->client) == 1))))
                return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake source tick differs from its actual accepted command owner");
        }
        size_t cursor = 0; qa_application_network_player row;
        while (qa_application_network_player_next(host->frontend->application, &cursor, &row))
            if (qa_net_client_id_equal(row.client, p->client) && row.seat.owner == p->seat.owner && row.seat.index == p->seat.index &&
                !p->retiring && client->attachment == QA_NET_REMOTE && row.source_begin_pending != (state.stage < 3))
                return frontend_fail(error, QA_ERROR_FORMAT, "Retained NetQuake source begin differs from native signon stage");
    }
    uint32_t cursor = 0; const qa_net_client *client; size_t count = 0;
    while (qa_net_connections_next(qa_network_connections(host->runtime), &cursor, &client)) ++count;
    if (count != expected) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake runtime has an undeclared frontend connection");
    size_t roster = 0, rows = 0; qa_application_network_player row;
    while (qa_application_network_player_next(host->frontend->application, &roster, &row)) {
        bool found = false;
        for (size_t i = 0; i < NQ_CLIENTS; ++i) if (host->peers[i].occupied && qa_net_client_id_equal(row.client, host->peers[i].client) &&
            row.seat.owner == host->peers[i].seat.owner && row.seat.index == host->peers[i].seat.index) found = true;
        if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "NetQuake application roster has an undeclared frontend peer");
        ++rows;
    }
    return rows == expected || frontend_fail(error, QA_ERROR_FORMAT, "NetQuake frontend and application roster counts differ");
}
