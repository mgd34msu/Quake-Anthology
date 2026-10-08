#include "network_unified_private.h"
#include "map_players_private.h"
#include "network_q1_source.h"
#include "guest_q3_private.h"
#include "guest_q3_console.h"
#include "guest_native_q2_private.h"
#include "native_q3_clients.h"
#include "native_q3_wire_state.h"
#include "native_q3_console.h"
#include "guest_q3_components.h"
#include "client_events.h"
#include "qa/application_network.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q3_clients.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static application_provider *primary(const qa_application *app)
{
    return app ? application_world_provider((qa_application *)app, QA_ROLE_ENTITIES, "") : NULL;
}

static bool checkpoint_lane(const qa_application *app)
{
    return app && app->operation == APPLICATION_PERSISTING &&
        (app->capture_content_graph || (app->native_restore_image && app->content_graph)) &&
        !app->publication_started && !app->finalizing;
}
static bool source_ready(const qa_application *app, const application_provider *source, bool checkpoint)
{
    return app && app->state == QA_APPLICATION_RUNNING && !app->destroy_requested &&
        (checkpoint ? checkpoint_lane(app) : app->operation == APPLICATION_IDLE) && !app->frame_preparing && !app->q3_round_active &&
        !app->q3_world_restart && app->session && app->world && app->players &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) && qa_world_idle(app->world) &&
        source && source == app->players->map_provider && source->constructed && source->attached &&
        source->map_bound && !source->close_pending && source->owner && source->launch && source->product;
}

static bool capacity(application_provider *source, uint32_t *out, bool checkpoint, qa_error *error)
{
    if (source->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_bot_max_clients(source->state.q1, out, error);
    if (source->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_bot_max_clients(source->state.q2, out, error);
    if (source->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_source_max_clients(source->state.q3, out, error);
    if (source->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = application_network_q1_qc_observation(source->application,
            source->owner, error);
        if (!engine) return false;
        *out = engine->max_clients;
        return true;
    }
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        const struct application_native_q2 *engine = source->state.native.q2_engine;
        const qa_cvar_view *maximum = qa_cvars_find(engine->cvars, "maxclients");
        if (!engine->initialized || !engine->map_ready || engine->shutting_down || engine->calls ||
            !maximum || maximum->integer < 1 || maximum->integer > 256)
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified Source lost its initialized Q2 client capacity");
        *out = (uint32_t)maximum->integer;
        return true;
    }
    struct application_q3_guest *engine = q3g_engine(source);
    if (!engine || !engine->game || !engine->game->initialized || engine->game->retired ||
        !engine->map_ready || (engine->restore_pending && !(checkpoint &&
            checkpoint_lane(source->application) && source->application->native_restore_image)) || engine->calls || engine->draining_clients ||
        !engine->game->host ||
        !engine->loaded_compatibility || engine->loaded_max_clients < 1 || engine->loaded_max_clients > 64)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Unified Source has no initialized physical client owner");
    *out = (uint32_t)engine->loaded_max_clients;
    return true;
}

static bool bytes_copy(qa_bytes value, qa_buffer *out, qa_error *error)
{
    if (value.size && !value.data)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified intent has no owned source bytes");
    uint8_t *copy = value.size ? malloc(value.size) : NULL;
    if (value.size && !copy)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining unified Source intent");
    if (value.size) memcpy(copy, value.data, value.size);
    *out = (qa_buffer){copy, value.size};
    return true;
}

static void input_free(retained_input *input)
{
    qa_buffer_free(&input->provider);
    qa_buffer_free(&input->weapon);
    *input = (retained_input){0};
}

static bool inputs_player(application_unified_inputs *owner, qa_unified_session_player *out,
    qa_error *error)
{
    qa_application *app = owner->application;
    return qa_network_epoch(owner->runtime, owner->client) == owner->runtime_epoch &&
        app->publication_generation == owner->publication && app->map_revision == owner->map_revision &&
        application_unified_player_read(app, owner->client, owner->seat, out, error) &&
        qa_actor_id_equal(out->actor, owner->actor) && out->source_owner == owner->source &&
        out->source_slot == owner->source_slot;
}

bool application_unified_inputs_create(qa_application *app, qa_network_runtime *runtime,
    qa_net_client_id client, qa_net_seat_id seat, uint32_t epoch,
    application_unified_inputs **out, qa_error *error)
{
    application_unified_source source;
    qa_unified_session_player player;
    if (!out || !runtime || !epoch || !qa_network_callbacks_idle(runtime) ||
        !qa_network_epoch(runtime, client) ||
        !application_unified_source_read(app, &source, error) ||
        !application_unified_player_read(app, client, seat, &player, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input owner requires its admitted physical Source peer");
    application_unified_inputs *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Allocating unified Source input owner");
    *owner = (application_unified_inputs){.application = app, .runtime = runtime, .client = client,
        .seat = seat, .actor = player.actor, .source = player.source_owner, .source_slot = player.source_slot,
        .epoch = epoch, .runtime_epoch = qa_network_epoch(runtime, client),
        .publication = source.publication, .map_revision = source.map_revision,
        .queued = -1, .submitted = -1};
    *out = owner;
    return true;
}

bool application_unified_inputs_queue(application_unified_inputs *owner,
    const qa_unified_input_batch *batch, qa_error *error)
{
    qa_unified_session_player player;
    if (!owner || !batch || owner->advancing || batch->epoch != owner->epoch || batch->count > 64 ||
        !inputs_player(owner, &player, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input batch belongs to a retired Source peer");
    size_t added = 0;
    int64_t sequence = owner->queued;
    retained_input candidates[64] = {0};
    for (size_t i = 0; i < batch->count; ++i) {
        const qa_unified_input *input = batch->commands + i;
        if (input->sequence > INT64_MAX) goto invalid;
        if ((int64_t)input->sequence <= sequence) continue;
        if (input->command.kind != player.movement) goto invalid;
        if (input->has_arsenal && (input->arsenal.provider.size != player.arsenal.size ||
            !input->arsenal.provider.data || memcmp(input->arsenal.provider.data,
                player.arsenal.data, player.arsenal.size))) goto invalid;
        retained_input *candidate = candidates + added;
        candidate->value = *input;
        if (input->has_arsenal) {
            if (!bytes_copy(input->arsenal.provider, &candidate->provider, error) ||
                !bytes_copy(input->arsenal.weapon, &candidate->weapon, error)) goto failed;
            candidate->value.arsenal.provider = (qa_bytes){candidate->provider.data, candidate->provider.size};
            candidate->value.arsenal.weapon = (qa_bytes){candidate->weapon.data, candidate->weapon.size};
        }
        ++added;
        sequence = (int64_t)input->sequence;
    }
    if (added > SIZE_MAX / sizeof(*owner->commands) - owner->count) {
        application_fail(error, QA_ERROR_MEMORY, "Unified input programme exceeds its actual extent");
        goto failed;
    }
    if (added) {
        retained_input *next = realloc(owner->commands, (owner->count + added) * sizeof(*next));
        if (!next) {
            application_fail(error, QA_ERROR_MEMORY, "Growing retained unified Source input programme");
            goto failed;
        }
        owner->commands = next;
        memcpy(next + owner->count, candidates, added * sizeof(*next));
        owner->count += added;
    }
    owner->queued = sequence;
    return true;
invalid:
    application_fail(error, QA_ERROR_ARGUMENT, "Unified command differs from its admitted movement or arsenal");
failed:
    for (size_t i = 0; i < 64; ++i) input_free(candidates + i);
    return false;
}

static bool same_bytes(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

bool application_unified_inputs_flush(application_unified_inputs *owner, qa_error *error)
{
    qa_unified_session_player player;
    if (!owner || owner->advancing || !qa_network_callbacks_idle(owner->runtime) ||
        !inputs_player(owner, &player, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input flush requires its returned Source peer");
    if (owner->cursor == owner->count) return true;
    owner->advancing = true;
    bool okay = true;
    if (player.movement == QA_MOVEMENT_NETQUAKE) {
        qa_unified_input selected = owner->commands[owner->cursor].value;
        for (size_t i = owner->cursor + 1; i < owner->count; ++i) {
            qa_unified_input next = owner->commands[i].value;
            if (next.command.data.nq.impulse == 0)
                next.command.data.nq.impulse = selected.command.data.nq.impulse;
            if (selected.has_arsenal && (!next.has_arsenal ||
                same_bytes(next.arsenal.provider, selected.arsenal.provider))) {
                if (!next.has_arsenal) { next.has_arsenal = true; next.arsenal = selected.arsenal; }
                else {
                    if (!next.arsenal.weapon.size) next.arsenal.weapon = selected.arsenal.weapon;
                    next.arsenal.use_holdable |= selected.arsenal.use_holdable;
                    if (!next.arsenal.has_impulse || !next.arsenal.impulse) {
                        next.arsenal.has_impulse = selected.arsenal.has_impulse || next.arsenal.has_impulse;
                        next.arsenal.impulse = selected.arsenal.impulse;
                    }
                }
            }
            selected = next;
        }
        okay = qa_network_accept_unified_input(owner->runtime, owner->client, owner->seat,
            owner->actor, owner->runtime_epoch, &selected, error);
        if (okay) { owner->submitted = (int64_t)selected.sequence; owner->cursor = owner->count; }
    } else {
        while (okay && owner->cursor < owner->count) {
            const qa_unified_input *input = &owner->commands[owner->cursor].value;
            okay = qa_network_accept_unified_input(owner->runtime, owner->client, owner->seat,
                owner->actor, owner->runtime_epoch, input, error);
            if (okay) { owner->submitted = (int64_t)input->sequence; ++owner->cursor; }
        }
    }
    owner->advancing = false;
    if (okay) {
        for (size_t i = 0; i < owner->count; ++i) input_free(owner->commands + i);
        free(owner->commands); owner->commands = NULL; owner->cursor = owner->count = 0;
    }
    return okay;
}

int64_t application_unified_inputs_submitted(const application_unified_inputs *owner)
{
    return owner ? owner->submitted : -1;
}

bool application_unified_inputs_destroy(application_unified_inputs *owner, qa_error *error)
{
    if (!owner) return true;
    if (owner->advancing)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified Source input is still entered");
    for (size_t i = 0; i < owner->count; ++i) input_free(owner->commands + i);
    free(owner->commands); free(owner);
    return true;
}

static bool source_read(qa_application *app, application_unified_source *out, bool checkpoint, qa_error *error)
{
    application_provider *source = primary(app);
    if (!out || !source_ready(app, source, checkpoint))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified hosting requires its returned primary Source");
    qa_clock_state clock;
    uint32_t maximum;
    if (!qa_session_clock(app->session, source->owner, &clock) ||
        clock.frame.provider != source->owner || clock.frame.kind != source->component.clock.kind ||
        !capacity(source, &maximum, checkpoint, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified hosting lost its actual Source clock or client owner");
    if (!maximum || maximum > 256 || (source->product->family != QA_GAME_Q1 &&
        source->product->family != QA_GAME_Q2 && source->product->family != QA_GAME_Q3))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Unified Source exceeds the admitted protocol extent");
    *out = (application_unified_source){.launch = qa_application_launch(app), .session = app->session,
        .world = app->world, .owner = source->owner, .family = source->product->family,
        .frame = clock.frame, .publication = app->publication_generation, .map_revision = app->map_revision,
        .frame_revision = app->frame_revision, .max_clients = maximum};
    return true;
}
bool application_unified_source_read(qa_application *app, application_unified_source *out, qa_error *error)
{ return source_read(app,out,false,error); }
bool application_unified_source_checkpoint_read(qa_application *app, application_unified_source *out, qa_error *error)
{ return source_read(app,out,true,error); }

static bool source_current(const qa_application *app, const application_unified_source *receipt, bool checkpoint)
{
    application_provider *source = primary(app);
    qa_clock_state clock;
    uint32_t maximum;
    return receipt && source_ready(app, source, checkpoint) && source->owner == receipt->owner &&
        source->product->family == receipt->family && qa_application_launch(app) == receipt->launch &&
        app->session == receipt->session && app->world == receipt->world &&
        app->publication_generation == receipt->publication && app->map_revision == receipt->map_revision &&
        app->frame_revision == receipt->frame_revision &&
        capacity(source, &maximum, checkpoint, NULL) && maximum == receipt->max_clients &&
        qa_session_clock(app->session, source->owner, &clock) &&
        clock.frame.provider == receipt->frame.provider && clock.frame.kind == receipt->frame.kind &&
        clock.frame.phase == receipt->frame.phase && clock.frame.number == receipt->frame.number &&
        clock.frame.start_ns == receipt->frame.start_ns && clock.frame.time_ns == receipt->frame.time_ns &&
        clock.frame.elapsed_ns == receipt->frame.elapsed_ns;
}
bool application_unified_source_current(const qa_application *app, const application_unified_source *receipt)
{ return source_current(app,receipt,false); }
bool application_unified_source_checkpoint_current(const qa_application *app, const application_unified_source *receipt)
{ return source_current(app,receipt,true); }

bool application_unified_source_slot_occupied(qa_application *app, uint32_t slot,
    bool *out, qa_error *error)
{
    application_unified_source receipt;
    if (!out || !application_unified_source_read(app, &receipt, error)) return false;
    if (slot >= receipt.max_clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified admission exceeds its actual Source capacity");
    application_provider *source = primary(app);
    bool occupied = false;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        occupied |= !row->retiring && row->client_slot == slot;
    }
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_actor_id actor;
        occupied |= qa_q1_source_client_actor(source->state.q1, slot, &actor);
    } else if (source->kind == APPLICATION_PROVIDER_Q2) {
        uint32_t cursor = 0;
        const qa_actor_record *actor;
        while (qa_actors_next(qa_session_actors(app->session), &cursor, &actor)) {
            qa_builtin_player_info client;
            occupied |= qa_q2_player_projection(source->state.q2, actor->id, &client) &&
                client.connected && client.slot == slot;
        }
    } else if (source->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        if (!qa_q3_source_binding_read(source->state.q3, slot, &binding, error) ||
            !qa_q3_client_slot_read(source->state.q3, slot, &client, error)) return false;
        occupied |= binding.actor.registry != 0 || client.connected != QA_Q3_CLIENT_DISCONNECTED;
    } else if (source->kind == APPLICATION_PROVIDER_QC) {
        const application_qc_client *client = source->state.qc.engine->clients + slot + 1;
        occupied |= client->connected || client->prepared;
    } else if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        const application_native_q2_client *client = source->state.native.q2_engine->clients + slot + 1;
        occupied |= client->reserved || client->connected || client->actor.registry != 0;
    } else {
        const q3g_client *client = q3g_engine(source)->clients + slot;
        occupied |= client->reserved || client->allocated || client->connected || client->pending_retirement;
    }
    if (!application_unified_source_current(app, &receipt))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified admission changed its actual Source receipt");
    *out = occupied;
    return true;
}

static bool physical_player(application_provider *source, const application_player_record *row,
    uint32_t *entity, qa_error *error)
{
    uint32_t slot;
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        if (!qa_q1_native_client_slot(source->state.q1, row->actor, &slot, error) || slot != row->client_slot) {
            application_fail(error, QA_ERROR_ARGUMENT, "Unified player differs from its physical Q1 client");
            return false;
        }
        *entity = slot + 1;
        return true;
    }
    if (source->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = application_network_q1_qc_observation(source->application,
            source->owner, error);
        if (!engine || !application_network_q1_qc_client(engine, row->actor, &slot, error) ||
            slot != row->client_slot + 1) return false;
        *entity = slot;
        return true;
    }
    if (source->kind == APPLICATION_PROVIDER_Q2) {
        qa_builtin_player_info client;
        if (!qa_q2_player_projection(source->state.q2, row->actor, &client) ||
            !client.connected || client.slot != row->client_slot) {
            application_fail(error, QA_ERROR_ARGUMENT, "Unified player differs from its physical Q2 client");
            return false;
        }
        *entity = client.slot + 1;
        return true;
    }
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine) {
        const struct application_native_q2 *engine = source->state.native.q2_engine;
        slot = row->client_slot + 1;
        if (slot >= 257) {
            application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 client exceeds its physical extent");
            return false;
        }
        const application_native_q2_client *client = engine->clients + slot;
        qa_native_slot_binding binding;
        if (!client->connected || !client->begun || client->disconnect_started ||
            !qa_actor_id_equal(client->actor, row->actor) || !source->state.native.host ||
            !qa_native_slot(qa_native_host_instance(source->state.native.host), slot, &binding, error) ||
            binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, row->actor)) {
            application_fail(error, QA_ERROR_ARGUMENT, "Unified player differs from its original Q2 physical binding");
            return false;
        }
        *entity = slot;
        return true;
    }
    slot = row->client_slot;
    if (!qa_application_network_q3_client_bound(source->application, source->owner, row->actor, slot)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Unified player differs from its physical Q3 client");
        return false;
    }
    *entity = slot;
    return true;
}

static bool player_read(qa_application *app, qa_net_client_id client, qa_net_seat_id seat,
    qa_unified_session_player *out, bool checkpoint, qa_error *error)
{
    application_unified_source source;
    if (!out || !source_read(app, &source, checkpoint, error)) return false;
    const application_player_record *row = application_players_connection_read(app, client, seat);
    qa_application_control_view control;
    if (!row || row->deferred || row->source_begin_pending || row->client_slot >= source.max_clients ||
        !qa_actors_get(qa_session_actors(app->session), row->actor) ||
        !qa_application_control_read(app, row->actor, &control))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified connection has no current admitted movement player");
    uint32_t entity;
    if (!physical_player(primary(app), row, &entity, error)) return false;
    application_provider *arsenal = application_provider_for(app, row->actor, QA_ROLE_ARSENAL, "");
    const char *instance = arsenal && arsenal->launch ? arsenal->launch->selection.instance : NULL;
    if (!instance || !instance[0] || !source_current(app, &source, checkpoint))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified player lost its selected arsenal or Source receipt");
    *out = (qa_unified_session_player){.actor = row->actor, .seat = seat,
        .movement = control.state.kind, .arsenal = {(const uint8_t *)instance, strlen(instance)},
        .source_owner = source.owner, .source_slot = entity};
    return true;
}
bool application_unified_player_read(qa_application *app, qa_net_client_id client, qa_net_seat_id seat,
    qa_unified_session_player *out, qa_error *error)
{ return player_read(app,client,seat,out,false,error); }
bool application_unified_player_checkpoint_read(qa_application *app, qa_net_client_id client, qa_net_seat_id seat,
    qa_unified_session_player *out, qa_error *error)
{ return player_read(app,client,seat,out,true,error); }

bool application_unified_player_current(qa_application *app, qa_net_client_id client,
    const qa_unified_session_player *player)
{
    qa_unified_session_player actual;
    return player && application_unified_player_read(app, client, player->seat, &actual, NULL) &&
        qa_actor_id_equal(actual.actor, player->actor) && actual.movement == player->movement &&
        actual.source_owner == player->source_owner && actual.source_slot == player->source_slot &&
        same_bytes(actual.arsenal, player->arsenal);
}
bool application_unified_player_checkpoint_current(qa_application *app, qa_net_client_id client,
    const qa_unified_session_player *player)
{
    qa_unified_session_player actual;
    return player && player_read(app,client,player->seat,&actual,true,NULL) &&
        qa_actor_id_equal(actual.actor,player->actor) && actual.movement==player->movement &&
        actual.source_owner==player->source_owner && actual.source_slot==player->source_slot &&
        same_bytes(actual.arsenal,player->arsenal);
}

bool application_unified_player_input(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, qa_actor_id actor, const qa_unified_input *input, qa_error *error)
{
    qa_unified_session_player actual;
    if (!input || !application_unified_player_read(app, client, seat, &actual, error) ||
        !qa_actor_id_equal(actor, actual.actor) || input->command.kind != actual.movement ||
        (input->has_arsenal && !same_bytes(input->arsenal.provider, actual.arsenal)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified input lost its admitted full Source player");
    return qa_application_control_unified_command(app, actor, input, error);
}

bool application_unified_player_disconnect(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, qa_error *error)
{
    return application_players_connection_disconnect(app, client, seat, error);
}

typedef struct unified_info_pair {
    const char *key, *value;
    size_t key_size, value_size;
} unified_info_pair;

/* The Source host uses q2Userinfo for every family: first duplicate wins,
 * unmatched trailing keys are ignored, and replacing a key retains order. */
static size_t info_pairs(const char *text, unified_info_pair *pairs)
{
    const char *cursor = text + (text[0] == '\\');
    size_t count = 0;
    while (*cursor) {
        const char *separator = strchr(cursor, '\\');
        if (!separator) break;
        const char *value = separator + 1, *end = strchr(value, '\\');
        size_t key_size = (size_t)(separator - cursor);
        size_t value_size = end ? (size_t)(end - value) : strlen(value);
        bool duplicate = false;
        for (size_t i = 0; i < count; ++i)
            if (pairs[i].key_size == key_size && !memcmp(pairs[i].key, cursor, key_size))
                duplicate = true;
        if (!duplicate) pairs[count++] = (unified_info_pair){cursor, value, key_size, value_size};
        if (!end) break;
        cursor = end + 1;
    }
    return count;
}

static bool info_key(unified_info_pair pair, const char *key)
{
    return strlen(key) == pair.key_size && !memcmp(pair.key, key, pair.key_size);
}

static char *info_value_copy(const char *text, const char *key, qa_error *error)
{
    unified_info_pair *pairs = calloc(strlen(text) / 2 + 3, sizeof(*pairs));
    if (!pairs) { application_fail(error, QA_ERROR_MEMORY, "Reading actual unified userinfo value"); return NULL; }
    size_t count = info_pairs(text, pairs);
    const char *value = ""; size_t size = 0;
    for (size_t i = 0; i < count; ++i) if (info_key(pairs[i], key)) {
        value = pairs[i].value; size = pairs[i].value_size; break;
    }
    char *copy = malloc(size + 1);
    if (copy) { memcpy(copy, value, size); copy[size] = 0; }
    else application_fail(error, QA_ERROR_MEMORY, "Retaining actual unified userinfo value");
    free(pairs); return copy;
}

static char *info_canonical(const char *text, const char *address, bool q1_language,
    qa_error *error)
{
    size_t length = strlen(text);
    if (length > 8192 || strchr(address, '\\')) {
        application_fail(error, QA_ERROR_FORMAT, "Unified userinfo exceeds its wire or endpoint domain");
        return NULL;
    }
    unified_info_pair *pairs = calloc(length / 2 + 3, sizeof(*pairs));
    if (!pairs) { application_fail(error, QA_ERROR_MEMORY, "Reading unified Source userinfo"); return NULL; }
    size_t count = info_pairs(text, pairs), ip = count;
    bool language = false;
    for (size_t i = 0; i < count; ++i) {
        if (info_key(pairs[i], "ip")) ip = i;
        language |= info_key(pairs[i], "language");
    }
    if (ip == count) ++count;
    pairs[ip] = (unified_info_pair){"ip", address, 2, strlen(address)};
    if (q1_language && !language)
        pairs[count++] = (unified_info_pair){"language", "english", 8, 7};
    size_t size = 0;
    for (size_t i = 0; i < count; ++i) size += 2 + pairs[i].key_size + pairs[i].value_size;
    char *result = malloc(size + 1);
    if (!result) {
        free(pairs); application_fail(error, QA_ERROR_MEMORY, "Retaining canonical Source userinfo"); return NULL;
    }
    char *write = result;
    for (size_t i = 0; i < count; ++i) {
        *write++ = '\\'; memcpy(write, pairs[i].key, pairs[i].key_size); write += pairs[i].key_size;
        *write++ = '\\'; memcpy(write, pairs[i].value, pairs[i].value_size); write += pairs[i].value_size;
    }
    *write = 0; free(pairs); return result;
}

bool application_unified_player_bind_local(qa_application *app, const qa_net_client *peer,
    qa_net_seat_id seat, uint32_t application_seat, qa_unified_session_player *out, qa_error *error)
{
    if (!application_players_local_connection_bind(app, peer, seat, application_seat, error)) return false;
    char address[256];
    if (peer->endpoint.kind == QA_NET_LOOPBACK) memcpy(address, "localhost", sizeof("localhost"));
    else if (!qa_net_address_format(&peer->endpoint, address, sizeof(address), error)) return false;
    application_player_record *row = (application_player_record *)application_players_connection_read(app, peer->id, seat);
    char *userinfo = info_canonical(row->userinfo ? row->userinfo : "", address,
        primary(app)->kind == APPLICATION_PROVIDER_Q1, error);
    if (!userinfo) return false;
    free(row->userinfo); row->userinfo = userinfo;
    return player_read(app, peer->id, seat, out, app->operation == APPLICATION_PERSISTING, error);
}

static bool userinfo_source(application_provider *source, const qa_unified_session_player *player,
    uint32_t client_slot, const char *value, qa_error *error)
{
    if (source->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_source_client_userinfo(source->state.q1, player->actor, value, error);
    if (source->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_player_userinfo(source->state.q2, player->actor, value, error);
    if (source->kind == APPLICATION_PROVIDER_Q3)
        return application_native_q3_wire_userinfo(source, client_slot, value, error) &&
            application_native_q3_client_userinfo_changed(source, player->actor, error);
    if (source->kind == APPLICATION_PROVIDER_QC)
        return application_qc_client_userinfo(source, player->actor, error);
    if (source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine)
        return application_native_q2_client_userinfo(source, player->source_slot, value, error);
    return application_q3_guest_client_userinfo(source, client_slot, value, error);
}

static void q1_name(const char *value, char out[46])
{
    qa_bytes text={(const uint8_t *)value,strlen(value)};
    size_t cursor=0, units=0, written=0; uint32_t scalar;
    while (units<15 && qa_utf8_next(text,&cursor,&scalar)) {
        if (scalar>0xffff && units==14) {
            uint32_t high=0xd800+((scalar-0x10000)>>10);
            out[written++]=(char)(0xe0|(high>>12));
            out[written++]=(char)(0x80|((high>>6)&63));
            out[written++]=(char)(0x80|(high&63));
            break;
        }
        written+=qa_utf8_encode(scalar,out+written);
        units+=scalar>0xffff?2:1;
    }
    out[written]=0;
}

bool application_unified_player_userinfo(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, const char *value, qa_error *error)
{
    qa_unified_session_player player;
    application_unified_source receipt;
    if (!value || !application_unified_source_read(app, &receipt, error) ||
        !application_unified_player_read(app, client, seat, &player, error)) return false;
    const application_player_record *row = application_players_connection_read(app, client, seat);
    const char *old = row->userinfo ? row->userinfo : "";
    unified_info_pair *pairs = calloc(strlen(old) / 2 + 3, sizeof(*pairs));
    if (!pairs) return application_fail(error, QA_ERROR_MEMORY, "Retaining unified client endpoint");
    size_t count = info_pairs(old, pairs);
    char *address = NULL;
    for (size_t i = 0; i < count; ++i) if (info_key(pairs[i], "ip")) {
        address = malloc(pairs[i].value_size + 1);
        if (address) { memcpy(address, pairs[i].value, pairs[i].value_size); address[pairs[i].value_size] = 0; }
        break;
    }
    free(pairs);
    if (!address) return application_fail(error, QA_ERROR_ARGUMENT, "Unified Source userinfo lost its admitted endpoint");
    application_provider *source = primary(app);
    char *canonical = info_canonical(value, address, source->kind == APPLICATION_PROVIDER_Q1, error);
    free(address);
    if (!canonical) return false;
    uint32_t slot = row->client_slot;
    application_player_record *actual = (application_player_record *)row;
    free(actual->userinfo); actual->userinfo = canonical;
    if (!userinfo_source(source, &player, slot, canonical, error) ||
        !application_client_userinfo_changed(app, player.actor, error) ||
        !application_unified_source_current(app, &receipt) ||
        !application_unified_player_current(app, client, &player)) {
        application_fault(app, error); return false;
    }
    return true;
}

static bool command_provider(application_provider *provider, const qa_command_invocation *command,
    bool source, bool *handled, qa_error *error)
{
    *handled = false;
    if (provider->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_game_console_command(provider->state.q1, command->context.actor, command, handled, error);
    if (provider->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_game_console_command(provider->state.q2, command->context.actor, command, handled, error);
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        return source ? application_native_q3_client_command(provider, command->context.actor, command, handled, error) :
            qa_q3_game_console_command(provider->state.q3, command->context.actor, command, handled, error);
    if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
        return application_native_q2_client_command(provider, command->context.actor, command, handled, error);
    if (provider->kind == APPLICATION_PROVIDER_QVM || provider->kind == APPLICATION_PROVIDER_NATIVE) {
        bool okay = application_q3_guest_client_command_vector(provider, command->context.actor,
            command->argv, command->argc, error);
        *handled = okay; return okay;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
        "Unified QC command requires its actual Source host command boundary");
}

static int32_t command_color(const char *word)
{
    long value=strtol(word,NULL,10);
    unsigned long color=(unsigned long)value & 15u;
    return color>13?13:(int32_t)color;
}

bool application_unified_player_command(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, const char *name, const char *const *arguments, size_t count, qa_error *error)
{
    qa_unified_session_player player;
    if (!name || !name[0] || count > 128 || (count && !arguments) ||
        !application_unified_player_read(app, client, seat, &player, error)) return false;
    application_provider *source = primary(app);
    if (source->kind == APPLICATION_PROVIDER_QC && strcmp(name, "kill") && strcmp(name, "suicide"))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Unified QC command requires its actual Source host command boundary");
    const char *words[129]; words[0] = name;
    size_t extent = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!arguments[i] || strlen(arguments[i]) > 8192)
            return application_fail(error, QA_ERROR_FORMAT, "Unified command has an invalid actual argument");
        words[i + 1] = arguments[i]; extent += strlen(arguments[i]) + (i != 0);
    }
    char *args = malloc(extent + 1);
    if (!args) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual unified command arguments");
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i) args[offset++] = ' ';
        size_t size = strlen(arguments[i]); memcpy(args + offset, arguments[i], size); offset += size;
    }
    args[offset] = 0;
    const application_player_record *row = application_players_connection_read(app, client, seat);
    qa_command_invocation command = {.console = app->console, .argc = count + 1,
        .argv = words, .args_text = args, .context = {.origin = row->remote ? QA_COMMAND_REMOTE : QA_COMMAND_LOCAL,
        .owner = source->owner, .actor = player.actor}};
    command.context.seat = row->seat;
    command.context.dialect = source->product->family == QA_GAME_Q1 ? QA_CONSOLE_Q1 : QA_CONSOLE_Q3;
    if (source->product->family == QA_GAME_Q1)
        command.context.dialect = source->product->edition == QA_EDITION_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
    else if (source->product->family == QA_GAME_Q2)
        command.context.dialect = source->product->edition == QA_EDITION_RERELEASE ? QA_CONSOLE_Q2_RERELEASE : QA_CONSOLE_Q2;
    bool okay = qa_application_capture_command_context(app, &command.context, &command.context, error);
    bool handled = false;
    if (okay && source->kind == APPLICATION_PROVIDER_Q1 && !strcmp(name, "name")) {
        const char *value = count ? arguments[0] : "unconnected";
        char declared_name[46]; q1_name(value,declared_name);
        okay = qa_q1_source_client_name(source->state.q1, player.actor, declared_name, error); handled = true;
        qa_buffer userinfo = {0}; qa_q1_source_client_view current;
        if (okay) okay = qa_q1_source_client_read(source->state.q1,player.actor,&current) &&
            qa_q1_source_client_userinfo_read(source->state.q1,player.actor,true,&userinfo,error);
        char *declared = okay ? malloc(strlen(current.name)+1) : NULL;
        if (okay && !declared) okay = application_fail(error,QA_ERROR_MEMORY,"Retaining changed Source client name");
        if (okay) {
            strcpy(declared,current.name);
            application_player_record *actual = (application_player_record *)row;
            free(actual->name); actual->name=declared;
            free(actual->userinfo); actual->userinfo=(char *)userinfo.data; userinfo=(qa_buffer){0};
            okay=application_client_userinfo_changed(app,player.actor,error);
        }
        qa_buffer_free(&userinfo);
    }
    if (okay && !handled && source->kind == APPLICATION_PROVIDER_Q1 && !strcmp(name, "color")) {
        const char *top = count ? arguments[0] : "0";
        const char *bottom = count > 1 ? arguments[1] : top;
        okay = qa_q1_source_client_colors(source->state.q1, player.actor,
            command_color(top), command_color(bottom), error); handled = true;
    }
    if (okay && !handled && source->kind == APPLICATION_PROVIDER_QC &&
        (!strcmp(name, "kill") || !strcmp(name, "suicide"))) {
        okay = qa_application_network_q1_kill(app, player.actor, error); handled = true;
    }
    static const char *const arsenal_names[] = {"use", "drop", "weapnext", "weapprev", "weaplast",
        "invnext", "invprev", "invuse", "invdrop", "useholdable", "weapon", "impulse"};
    bool selected = false;
    for (size_t i = 0; i < sizeof(arsenal_names) / sizeof(arsenal_names[0]); ++i)
        selected |= !strcmp(name, arsenal_names[i]);
    application_provider *arsenal = selected ? application_provider_for(app, player.actor, QA_ROLE_ARSENAL, "") : NULL;
    if (okay && !handled && arsenal && arsenal != source) {
        qa_command_invocation selection = command; selection.context.owner = arsenal->owner;
        okay = command_provider(arsenal, &selection, false, &handled, error);
    }
    if (okay && !handled) okay = command_provider(source, &command, true, &handled, error);
    free(args);
    if (!okay) { application_fault(app, error); return false; }
    return true;
}

bool application_unified_component_command(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, const qa_unified_component_owner *owner,
    const char *const *arguments, size_t count, qa_error *error)
{
    qa_unified_session_player player;
    if (!owner || !arguments || !count || count > 128 ||
        !application_unified_player_read(app, client, seat, &player, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component command requires its admitted Source recipient");
    size_t extent = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!arguments[i] || strlen(arguments[i]) > 8192)
            return application_fail(error, QA_ERROR_FORMAT, "Component command argument exceeds its actual immutable extent");
        if (i) extent += strlen(arguments[i]) + (i > 1);
    }
    char *tail = malloc(extent + 1);
    if (!tail) return application_fail(error, QA_ERROR_MEMORY, "Retaining component command arguments");
    size_t offset = 0;
    for (size_t i = 1; i < count; ++i) {
        if (i > 1) tail[offset++] = ' ';
        size_t length = strlen(arguments[i]); memcpy(tail + offset, arguments[i], length); offset += length;
    }
    tail[offset] = 0;
    const application_player_record *row = application_players_connection_read(app, client, seat);
    qa_command_invocation command = {.console = app->console, .argc = count, .argv = arguments,
        .args_text = tail, .context = {.origin = row->remote ? QA_COMMAND_REMOTE : QA_COMMAND_LOCAL, .owner = player.source_owner,
        .actor = player.actor, .seat = row->seat, .dialect = QA_CONSOLE_Q3}};
    bool handled = false;
    bool okay = qa_application_capture_command_context(app, &command.context, &command.context, error) &&
        application_q3_components_command(app, player.actor, owner, &command, &handled, error);
    free(tail);
    if (okay && !handled)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component command names no admitted recipient handler");
    return okay;
}

bool application_unified_source_command(qa_application *app,qa_net_client_id client,
    qa_net_seat_id seat,const qa_unified_source_command *value,qa_error *error)
{
    application_unified_source source;
    qa_unified_session_player player;
    if (!value || !value->instance || !value->arguments ||
        !value->argument_count || value->argument_count>128 ||
        !application_unified_source_read(app,&source,error) ||
        !application_unified_player_read(app,client,seat,&player,error)) return false;
    if (value->publication!=source.publication || value->map_revision!=source.map_revision)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source command belongs to a retired activation");
    application_provider *provider=NULL;
    for (size_t i=0;i<app->provider_count;++i) {
        application_provider *candidate=app->providers[i];
        if (!candidate->launch || strcmp(candidate->launch->selection.instance,value->instance)) continue;
        if (provider) return application_fail(error,QA_ERROR_FORMAT,"Source command instance has duplicate installed providers");
        provider=candidate;
    }
    if (!provider || provider->application!=app || !provider->constructed || !provider->attached ||
        provider->close_pending || provider->kind!=APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        qa_launch_snapshot_find(source.launch,value->instance)!=provider->launch)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source command has no installed compiled GAME activation");
    uint32_t slot;
    qa_q3_native_client native;
    qa_q3_source_binding binding;
    if (!qa_q3_native_client_slot(provider->state.q3,player.actor,&slot,error) ||
        !qa_q3_source_binding_read(provider->state.q3,slot,&binding,error) || !binding.in_use ||
        binding.client_slot!=(int32_t)slot || !qa_actor_id_equal(binding.actor,player.actor) ||
        !qa_q3_client_read(provider->state.q3,player.actor,&native,error) || native.connected!=QA_Q3_CLIENT_CONNECTED)
        return application_fail(error,QA_ERROR_ARGUMENT,"Source command lost its admitted full physical GAME client");
    size_t extent=0;
    for (size_t i=0;i<value->argument_count;++i) {
        if (!value->arguments[i] || strlen(value->arguments[i])>8192 || (!i && !value->arguments[i][0]))
            return application_fail(error,QA_ERROR_FORMAT,"Source command has an invalid lexical argument");
        if (i) extent+=strlen(value->arguments[i])+(i>1);
    }
    char *tail=malloc(extent+1);
    if (!tail) return application_fail(error,QA_ERROR_MEMORY,"Retaining scoped Source command arguments");
    size_t offset=0;
    for (size_t i=1;i<value->argument_count;++i) {
        if (i>1) tail[offset++]=' ';
        size_t length=strlen(value->arguments[i]); memcpy(tail+offset,value->arguments[i],length); offset+=length;
    }
    tail[offset]=0;
    const application_player_record *row=application_players_connection_read(app,client,seat);
    qa_command_invocation command={.console=app->console,.argc=value->argument_count,
        .argv=value->arguments,.args_text=tail,.context={.origin=row->remote?QA_COMMAND_REMOTE:QA_COMMAND_LOCAL,
        .owner=provider->owner,.actor=player.actor,.seat=row->seat,.dialect=QA_CONSOLE_Q3}};
    bool handled=false;
    bool okay=qa_application_capture_command_context(app,&command.context,&command.context,error) &&
        application_native_q3_source_client_command(provider,player.actor,&command,&handled,error);
    free(tail);
    if (!okay) { application_fault(app,error); return false; }
    return (handled && application_unified_source_current(app,&source) &&
        application_unified_player_current(app,client,&player)) ||
        application_fail(error,QA_ERROR_ARGUMENT,"Source command changed its actual admitted activation");
}

bool application_unified_player_admit(qa_application *app,
    qa_network_runtime *runtime, const qa_application_remote_player_request *request,
    qa_unified_session_player *out, qa_error *error)
{
    application_unified_source source;
    if (!request || !out || !runtime || request->bot || request->defer_source_begin ||
        !application_unified_source_read(app, &source, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified admission requires its immediate remote human Source request");
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(runtime), request->client);
    bool bound = false;
    for (size_t i = 0; peer && i < peer->seat_count; ++i)
        bound |= peer->seats[i].seat.owner == request->seat.owner && peer->seats[i].seat.index == request->seat.index;
    if (!peer || peer->protocol.kind != QA_NET_UNIFIED_1 || !bound || !qa_network_callbacks_idle(runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified admission lost its authentic transport seat");
    char address[256];
    if (peer->endpoint.kind == QA_NET_LOOPBACK) memcpy(address, "localhost", sizeof("localhost"));
    else if (!qa_net_address_format(&peer->endpoint, address, sizeof(address), error)) return false;
    char *userinfo = info_canonical(request->userinfo ? request->userinfo : "", address,
        primary(app)->kind == APPLICATION_PROVIDER_Q1, error);
    if (!userinfo) return false;
    qa_application_remote_player_request canonical = *request;
    canonical.userinfo = userinfo;
    char *name = info_value_copy(userinfo, "name", error);
    char *skin = info_value_copy(userinfo, "skin", error);
    char *team = info_value_copy(userinfo, "team", error);
    if (!name || !skin || !team) { free(name); free(skin); free(team); free(userinfo); return false; }
    canonical.name = name[0] || source.family != QA_GAME_Q1 ? name : "unconnected";
    canonical.skin = skin; canonical.team = team;
    application_provider *provider = primary(app);
    if (source.family == QA_GAME_Q3 && canonical.source_slot == UINT32_MAX) {
        qa_cvars *registry = provider->kind == APPLICATION_PROVIDER_Q3 ?
            application_native_q3_console_registry(provider) : application_guest_q3_console_registry(provider);
        const qa_cvar_view *reserved = registry ? qa_cvars_find(registry, "sv_privateClients") : NULL;
        const qa_cvar_view *password = registry ? qa_cvars_find(registry, "sv_privatePassword") : NULL;
        char *received = info_value_copy(userinfo, "password", error);
        if (!registry || !reserved || !password || !received) {
            free(received); free(name); free(skin); free(team); free(userinfo);
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified Q3 admission lost its actual private-slot policy");
        }
        uint32_t first = strcmp(received, password->value) && reserved->integer > 0 ?
            (uint32_t)reserved->integer : 0;
        free(received);
        for (uint32_t slot = first; slot < source.max_clients; ++slot) {
            bool occupied;
            if (!application_unified_source_slot_occupied(app, slot, &occupied, error)) {
                free(name); free(skin); free(team); free(userinfo); return false;
            }
            if (!occupied) { canonical.source_slot = slot; break; }
        }
        if (canonical.source_slot == UINT32_MAX) {
            free(name); free(skin); free(team); free(userinfo);
            return application_fail(error, QA_ERROR_ARGUMENT, "Server is full");
        }
    }
    qa_actor_id actor;
    bool admitted = qa_application_remote_player_attach(app, &canonical, &actor, error);
    free(name); free(skin); free(team); free(userinfo);
    if (!admitted) return false;
    if (!application_unified_player_read(app, request->client, request->seat, out, error) ||
        !qa_actor_id_equal(actor, out->actor)) {
        application_fault(app, error);
        return false;
    }
    return true;
}
