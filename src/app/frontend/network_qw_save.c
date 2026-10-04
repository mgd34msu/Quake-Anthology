#include "network_qw_private.h"
#include "qa/application_network.h"
#include "qa/network_save.h"
#include "qa/launch_identity.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool blob_fields(qa_source_save_io *io, qa_buffer *bytes, size_t maximum)
{
    if (!qa_source_save_count(io, &bytes->size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && bytes->size) {
        bytes->data = malloc(bytes->size);
        if (!bytes->data) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring immutable QuakeWorld source bytes");
    }
    return !bytes->size || (bytes->data && qa_source_save_bytes(io, bytes->data, bytes->size));
}
static bool address_fields(qa_source_save_io *io, qa_net_address *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_NET_IPX || !qa_source_save_u16(io, &v->port)) return false;
    v->kind = (qa_net_address_kind)kind;
    switch (v->kind) {
    case QA_NET_IPV4: return qa_source_save_bytes(io, v->host.ipv4, 4);
    case QA_NET_IPV6: return qa_source_save_bytes(io, v->host.ipv6.bytes, 16) && qa_source_save_u32(io, &v->host.ipv6.scope);
    case QA_NET_LOOPBACK: return qa_source_save_bytes(io, v->host.loopback, sizeof(v->host.loopback)) && memchr(v->host.loopback, 0, sizeof(v->host.loopback));
    case QA_NET_IPX: return qa_source_save_u32(io, &v->host.ipx.network) && qa_source_save_bytes(io, v->host.ipx.node, 6);
    }
    return false;
}
static bool short_fields(qa_source_save_io *io, int16_t *value)
{
    uint16_t bits = (uint16_t)*value;
    if (!qa_source_save_u16(io, &bits)) return false;
    *value = bits > INT16_MAX ? (int16_t)((int32_t)bits - 65536) : (int16_t)bits; return true;
}
static bool command_fields(qa_source_save_io *io, qa_qw_command *command)
{
    for (size_t i = 0; i < 3; ++i) if (!qa_source_save_f32(io, command->angles + i) || !isfinite(command->angles[i])) return false;
    return short_fields(io, &command->forward) && short_fields(io, &command->side) && short_fields(io, &command->up) &&
        qa_source_save_u8(io, &command->msec) && qa_source_save_u8(io, &command->buttons) && qa_source_save_u8(io, &command->impulse);
}
static bool entity_fields(qa_source_save_io *io, qa_qw_source_entity *entity)
{
    if (!qa_source_save_u32(io, &entity->number)) return false;
    double *scalars[] = {&entity->model, &entity->frame, &entity->colormap, &entity->skin, &entity->effects};
    for (size_t i = 0; i < 5; ++i)
        if (!qa_source_save_f64(io, scalars[i]) || !isfinite(*scalars[i]) || trunc(*scalars[i]) != *scalars[i]) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!qa_source_save_f32(io, entity->origin + i) || !isfinite(entity->origin[i]) ||
            !qa_source_save_f32(io, entity->angles + i) || !isfinite(entity->angles[i])) return false;
    return qa_source_save_bool(io, &entity->solid);
}
static bool client_fields(qa_source_save_io *io, qw_frontend_peer *peer, frontend_qw_host *host)
{
    bool bound = peer->host != NULL;
    if ((io->direction == QA_SOURCE_SAVE_WRITE && bound && peer->host != host) ||
        !qa_source_save_bool(io, &bound) || !qa_source_save_bool(io, &peer->occupied) ||
        !qa_source_save_bool(io, &peer->retiring) || !qa_source_save_bool(io, &peer->spectator) || !qa_source_save_bool(io, &peer->begun) ||
        !qa_source_save_u64(io, &peer->client.owner) || !qa_source_save_u64(io, &peer->client.generation) || !qa_source_save_u32(io, &peer->client.slot) ||
        !qa_source_save_u64(io, &peer->seat.owner) || !qa_source_save_u32(io, &peer->seat.index) ||
        !qa_source_save_u64(io, &peer->input_sequence) || !qa_source_save_u64(io, &peer->connected_ns) ||
        !qa_source_save_u64(io, &peer->command_time_ns) || !qa_source_save_u64(io, &peer->last_received_ns) ||
        !qa_source_save_u16(io, &peer->qport) || !qa_source_save_u32(io, &peer->rate) || !qa_source_save_u16(io, &peer->stat_mask) ||
        !command_fields(io, &peer->command) || !qa_source_save_f32(io, &peer->frags) || !isfinite(peer->frags) ||
        !qa_source_save_owned_text(io, &peer->userinfo) || !qa_source_save_i32(io, &peer->message_level) ||
        !qa_source_save_bytes(io, peer->reason, sizeof(peer->reason)) || !memchr(peer->reason, 0, sizeof(peer->reason))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) peer->host = bound ? host : NULL;
    for (size_t i = 0; i < 16; ++i)
        if (!qa_source_save_f64(io, peer->stats + i) || !isfinite(peer->stats[i]) || trunc(peer->stats[i]) != peer->stats[i]) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!qa_source_save_bool(io, &peer->pings[i].present) || !qa_source_save_u32(io, &peer->pings[i].sequence) ||
            !qa_source_save_u64(io, &peer->pings[i].sent_ns) || !qa_source_save_f64(io, &peer->pings[i].ping_ms) ||
            !isfinite(peer->pings[i].ping_ms) || peer->pings[i].ping_ms < -1 ||
            (peer->pings[i].present && (peer->pings[i].sequence & 63u) != i)) return false;
    return true;
}
static bool fields(qa_source_save_io *io, frontend_qw_host *host)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t magic = UINT32_C(0x48574151);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x48574151) || !frontend_save_provider(io, host->frontend->application, &host->owner) || !host->owner ||
        !qa_source_save_u64(io, &host->generation) || !qa_source_save_u64(io, &host->event_cursor) || !qa_source_save_u64(io, &host->reliable_cursor) ||
        !qa_source_save_u64(io, &host->event_generation) || !qa_source_save_u64(io, &host->reliable_generation) ||
        !qa_source_save_u64(io, &host->published_time_ns) || !qa_source_save_bool(io, &host->previous_pause) ||
        !qa_source_save_u32(io, &host->random) || !qa_source_save_u32(io, &host->checksum) ||
        !qa_source_save_u32(io, &host->player_model) || !qa_source_save_u32(io, &host->nail_model) || !qa_source_save_u32(io, &host->supernail_model) ||
        !qa_source_save_u32(io, &host->active_limit) || !qa_source_save_i32(io, &host->server_count) ||
        !qa_source_save_bytes(io, host->composition.bytes, sizeof(host->composition.bytes)) ||
        !qa_source_save_count(io, &host->model_count, 255) || !qa_source_save_count(io, &host->sound_count, 255)) return false;
    for (size_t i = 0; i < 255; ++i)
        if (!qa_source_save_owned_text(io, host->models + i) || !qa_source_save_owned_text(io, host->sounds + i)) return false;
    for (size_t i = 0; i < 64; ++i) if (!qa_source_save_owned_text(io, host->styles + i)) return false;
    if (!qa_source_save_count(io, &host->baseline_count, 511)) return false;
    for (size_t i = 0; i < host->baseline_count; ++i) if (!entity_fields(io, host->baselines + i)) return false;
    if (!qa_source_save_count(io, &host->signon_count, 65536)) return false;
    if (reading && (io->offset > io->input.size || host->signon_count > (io->input.size - io->offset) / 8))
        return frontend_fail(io->error, QA_ERROR_FORMAT, "Truncated QuakeWorld signon inventory");
    if (reading && host->signon_count) {
        host->signon = calloc(host->signon_count, sizeof(*host->signon));
        host->signon_views = calloc(host->signon_count, sizeof(*host->signon_views));
        if (!host->signon || !host->signon_views) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring QuakeWorld source signon owner");
    }
    for (size_t i = 0; i < host->signon_count; ++i) {
        if (!blob_fields(io, host->signon + i, QW_SIGNON)) return false;
        if (reading) host->signon_views[i] = (qa_bytes){host->signon[i].data, host->signon[i].size};
    }
    for (size_t i = 0; i < QW_CLIENTS; ++i) if (!client_fields(io, host->peers + i, host)) return false;
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        if (!qa_source_save_u32(io, host->drop_recipients + i)) return false;
        for (size_t j = 0; j < QW_CLIENTS; ++j) {
            qa_net_client_id *client = &host->drop_clients[i][j];
            if (!qa_source_save_u64(io, &client->owner) || !qa_source_save_u64(io, &client->generation) || !qa_source_save_u32(io, &client->slot) ||
                ((host->drop_recipients[i] & (UINT32_C(1) << j)) ?
                    client->owner != QA_NETWORK_COMMAND_OWNER || !client->generation || client->slot >= 64 :
                    client->owner || client->generation || client->slot)) return false;
        }
    }
    if (!qa_source_save_count(io, &host->pending_count, QW_PENDING)) return false;
    for (size_t i = 0; i < QW_PENDING; ++i) {
        qw_pending_control *pending = host->pending + i;
        if (!address_fields(io, &pending->address) || !qa_source_save_u64(io, &pending->time_ns) ||
            !qa_source_save_count(io, &pending->size, QW_MESSAGE) || !qa_source_save_bytes(io, pending->bytes, pending->size)) return false;
        if ((!blob_fields(io, &pending->reply, 65535) ||
            (pending->reply.size && (i >= host->pending_count || pending->size < 4 ||
                qa_load_u32le(pending->bytes) != UINT32_MAX)))) return false;
    }
    if (!qa_source_save_count(io, &host->action_count, QW_ACTIONS)) return false;
    qw_source_action *action = host->first_action;
    for (size_t i = 0; i < host->action_count; ++i) {
        if (reading) {
            action = calloc(1, sizeof(*action));
            if (!action) return frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring QuakeWorld source action owner");
            if (host->last_action) host->last_action->next = action; else host->first_action = action;
            host->last_action = action;
        }
        if (!action || !qa_source_save_u64(io, &action->client.owner) || !qa_source_save_u64(io, &action->client.generation) ||
            !qa_source_save_u32(io, &action->client.slot) || !qa_source_save_u64(io, &action->seat.owner) || !qa_source_save_u32(io, &action->seat.index) ||
            !qa_source_save_actor(io, &action->actor) || !qa_source_save_u64(io, &action->epoch) ||
            !qa_source_save_u64(io, &action->received_ns) || !qa_source_save_owned_text(io, &action->text) || !action->text) return false;
        if (!reading) action = action->next;
    }
    return reading || !action;
}

bool frontend_qw_source_hooks(frontend_qw_host *host, const qa_net_client *client,
    qa_network_qw_server_policy *policy, qa_network_qw_server_hooks *hooks,
    qa_qw_download_admission *downloads, qa_error *error)
{
    if (!host || !client || !policy || !hooks || !downloads || client->attachment != QA_NET_REMOTE ||
        client->protocol.kind != QA_NET_QW28 || client->protocol.flags || client->protocol.revision || client->seat_count != 1 ||
        client->seats[0].remote_index || !qa_sha256_equal(&client->composition, &host->composition))
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source changes its admitted protocol or composition");
    qw_frontend_peer *peer = NULL; size_t index = 0;
    for (size_t i = 0; i < QW_CLIENTS; ++i) if (host->peers[i].occupied && qa_net_client_id_equal(host->peers[i].client, client->id)) { peer = host->peers + i; index = i; }
    if (!peer || peer->host != host || client->seats[0].seat.owner != peer->seat.owner || client->seats[0].seat.index != peer->seat.index)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source has no declared physical peer and seat");
    size_t cursor = 0; qa_application_network_player row; bool found = false;
    while (qa_application_network_player_next(host->frontend->application, &cursor, &row)) {
        if (!qa_net_client_id_equal(row.client, client->id) || row.seat.owner != peer->seat.owner || row.seat.index != peer->seat.index) continue;
        if (found || row.client_slot != index || row.application_seat != peer->seat.index || (!peer->retiring && row.retiring))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld physical peer changes its genuine canonical roster row");
        if (!peer->retiring) {
            qa_application_network_qw_client source; const char *info;
            if (!qa_application_network_qw_client_read(host->frontend->application, row.actor, &source, error) ||
                source.source_slot != index + 1 || source.spectator != peer->spectator || source.begun != peer->begun ||
                !qa_application_network_qw_userinfo_read(host->frontend->application, row.actor, &info, error) || strcmp(info, peer->userinfo))
                return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored canonical/source role, Begin or raw userinfo differs");
        }
        found = true;
    }
    if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source lacks its genuine canonical player");
    *policy = (qa_network_qw_server_policy){peer->qport, peer->rate, QW_MESSAGE, 5};
    *hooks = frontend_qw_peer_hooks(peer);
    *downloads = (qa_qw_download_admission){.maximum_bytes = INT32_MAX,
        .content = qa_application_network_qw_content(host->frontend->application, error)};
    return downloads->content != NULL;
}
bool frontend_qw_qualified(const frontend_qw_host *host, bool complete, qa_error *error)
{
    if (!host || !host->frontend || !host->runtime || !host->admin || !frontend_qw_idle(host) || host->action_active || host->action_time_ns ||
        host->action_actor.registry || host->action_actor.generation || host->action_actor.slot ||
        host->generation != qa_application_configuration_generation(host->frontend->application) || !host->owner ||
        !host->active_limit || host->active_limit > 32 || host->server_count < 1 ||
        host->baseline_count < 32 || host->baseline_count > 511 || host->pending_count > QW_PENDING || host->action_count > QW_ACTIONS ||
        host->model_count > 255 || host->sound_count > 255 || (host->signon_count && (!host->signon || !host->signon_views)))
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld continuation lacks its actual source factory inventory");
    qa_application_network_qw_world world;
    if (!qa_application_network_qw_world_read(host->frontend->application, &world, error) || world.source.owner != host->owner ||
        host->published_time_ns > world.source.source_time_ns) return false;
    if (complete) {
        uint64_t generation = qa_application_protocol_events_generation(host->frontend->application);
        size_t count = qa_application_protocol_event_count(host->frontend->application);
        if (host->event_generation > generation || host->reliable_generation > generation ||
            (host->event_generation == generation && host->event_cursor > count) ||
            (host->reliable_generation == generation && host->reliable_cursor > count))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source publication cursors differ from restored actual event batch");
    }
    const char *models[255], *sounds[255]; size_t model_count, sound_count; uint32_t checksum;
    if (!qa_application_network_qw_precache(host->frontend->application, true, models, &model_count, error) ||
        !qa_application_network_qw_precache(host->frontend->application, false, sounds, &sound_count, error) ||
        model_count != host->model_count || sound_count != host->sound_count || !qa_qw_map_checksum2(world.map_bytes, &checksum, error) || checksum != host->checksum)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld continuation changes actual source precache or admitted map bytes");
    for (size_t i = 0; i < 255; ++i) {
        if ((i < model_count && (!host->models[i] || strcmp(host->models[i], models[i]))) || (i >= model_count && host->models[i]) ||
            (i < sound_count && (!host->sounds[i] || strcmp(host->sounds[i], sounds[i]))) || (i >= sound_count && host->sounds[i]))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld immutable factory names differ from source indices");
    }
    uint32_t player = 0, nail = 0, supernail = 0;
    for (size_t i = 0; i < model_count; ++i) {
        if (!strcmp(models[i], "progs/player.mdl")) player = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/spike.mdl")) nail = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/s_spike.mdl")) supernail = (uint32_t)i + 1;
    }
    if (host->player_model != player || host->nail_model != nail || host->supernail_model != supernail)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld compressed model indices differ from real source precache");
    for (size_t i = 0; i < 64; ++i) if (!host->styles[i]) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld lightstyle cache owner is absent");
    for (size_t i = 0; i < host->baseline_count; ++i) {
        const qa_qw_source_entity *v = host->baselines + i;
        if (!v->number || v->number > 511 || (i && host->baselines[i - 1].number >= v->number) || v->effects != 0 || v->solid)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld immutable baseline identity differs from its physical source factory");
        if (i < 32 && (v->number != i + 1 || v->model != host->player_model || v->colormap != (double)i + 1 || v->frame != 0 || v->skin != 0 ||
            v->origin[0] != 0 || v->origin[1] != 0 || v->origin[2] != 0 || v->angles[0] != 0 || v->angles[1] != 0 || v->angles[2] != 0))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld reserved client baseline changes its true constructor fields");
    }
    size_t occupied = 0;
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        const qw_frontend_peer *peer = host->peers + i;
        if (!peer->occupied) {
            if (peer->host || peer->retiring || peer->begun || peer->userinfo)
                return frontend_fail(error, QA_ERROR_FORMAT, "Absent QuakeWorld peer retains actual source ownership");
            continue;
        }
        ++occupied;
        if (peer->host != host || !peer->client.generation || peer->client.owner != QA_NETWORK_COMMAND_OWNER ||
            peer->seat.owner != QA_NETWORK_COMMAND_OWNER || peer->seat.index != 256u + i || !peer->userinfo ||
            peer->rate < 500 || peer->rate > 10000 || peer->input_sequence > INT32_MAX)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld retained peer has invalid full source admission");
        if (complete) {
            const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
            qa_network_qw_server_state state; qa_network_qw_server_policy policy; qa_network_qw_server_hooks hooks; qa_qw_download_admission downloads;
            if (!frontend_qw_source_hooks((frontend_qw_host *)host, client, &policy, &hooks, &downloads, error) ||
                !qa_network_qw_server_state_read(host->runtime, peer->client, &state, error) || state.input_sequence != peer->input_sequence ||
                state.qport != peer->qport || state.active != peer->begun || state.retiring != peer->retiring) return false;
            if (peer->begun && !peer->retiring) {
                qa_actor_id actor; qa_application_network_qw_client source; bool present; uint64_t accepted;
                if (!qa_application_remote_player_actor(host->frontend->application, peer->client, peer->seat, &actor) ||
                    !qa_application_network_qw_client_read(host->frontend->application, actor, &source, error) ||
                    !qa_network_accepted_sequence(host->runtime, peer->client, peer->seat, &present, &accepted, error) ||
                    present != (peer->input_sequence != 0) || (present && (accepted != peer->input_sequence ||
                        !source.command_present || source.command.sequence != accepted || source.command_time_ns != peer->command_time_ns))) return false;
            }
        }
    }
    if (complete) {
        uint32_t cursor = 0; const qa_net_client *client; size_t count = 0;
        while (qa_net_connections_next(qa_network_connections(host->runtime), &cursor, &client)) ++count;
        if (count != occupied) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld runtime has an undeclared physical peer");
    }
    size_t action_count = 0; const qw_source_action *last = NULL;
    for (const qw_source_action *action = host->first_action; action; action = action->next) {
        if (++action_count > QW_ACTIONS || !action->text || !action->client.generation || !action->actor.registry || !action->epoch)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld retained source action lacks its authored admission");
        last = action;
    }
    if (action_count != host->action_count || last != host->last_action) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source action allocator differs");
    return true;
}
bool frontend_qw_checkpoint(frontend_qw_host *host, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !frontend_qw_qualified(host, true, error)) return false;
    qa_source_save_io io = {0}; qa_buffer challenges = {0};
    bool ok = qa_qw_challenges_checkpoint(host->challenges, &challenges, error) &&
        qa_source_save_writer(&io, qa_application_session(host->frontend->application), error) && fields(&io, host) &&
        blob_fields(&io, &challenges, SIZE_MAX) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&challenges); return ok;
}
static uint32_t restored_random(void *context)
{
    frontend_qw_host *host = context;
    host->random = host->random * UINT32_C(1664525) + UINT32_C(1013904223); return host->random;
}
bool frontend_qw_restore(qa_frontend *frontend, qa_network_runtime *runtime, qa_server_admin *admin,
    qa_bytes bytes, frontend_qw_host **out, qa_error *error)
{
    if (!frontend || !runtime || !admin || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld continuation requires its detached actual service owners");
    frontend_qw_host *host = calloc(1, sizeof(*host));
    if (!host) return frontend_fail(error, QA_ERROR_MEMORY, "Restoring QuakeWorld physical factory");
    host->frontend = frontend; host->runtime = runtime; host->admin = admin;
    qa_source_save_io io = {0}; qa_buffer challenges = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) && fields(&io, host) &&
        blob_fields(&io, &challenges, bytes.size) && qa_source_save_finish(&io, NULL) &&
        qa_qw_challenges_restore_checkpoint((qa_bytes){challenges.data, challenges.size}, 1024, restored_random, host, &host->challenges, error) &&
        frontend_qw_qualified(host, false, error);
    qa_source_save_dispose(&io); qa_buffer_free(&challenges);
    if (!ok) { frontend_qw_destroy(host); return false; }
    *out = host; return true;
}
void frontend_qw_rebind(frontend_qw_host *host, qa_frontend *frontend, qa_network_runtime *runtime, qa_server_admin *admin)
{ if (host) { host->frontend = frontend; host->runtime = runtime; host->admin = admin; } }
