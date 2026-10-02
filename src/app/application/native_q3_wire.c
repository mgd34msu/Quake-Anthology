#include "native_q3_wire.h"
#include "guest_q3_private.h"
#include "native_q3_wire_state.h"
#include "native_q3_console.h"
#include "native_q3_match.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "control_frame.h"

static bool native_source_actor(application_provider *provider, qa_actor_id actor,
    uint32_t *slot, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    return (app && !app->destroy_requested && provider->kind == APPLICATION_PROVIDER_Q3 &&
        provider->state.q3 && provider->constructed && provider->attached &&
        !provider->close_pending && qa_actors_get(qa_session_actors(app->session), actor) &&
        qa_q3_native_client_slot(provider->state.q3, actor, slot, error)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire capability lost its actual source actor");
}

static bool native_movement(void *context, qa_actor_id actor, qa_q3_movement_state *out,
    bool *selected, qa_error *error)
{
    application_provider *provider = context;
    qa_application_control_view control;
    uint32_t slot;
    if (!out || !selected || !native_source_actor(provider, actor, &slot, error) ||
        !qa_application_control_read(provider->application, actor, &control))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire has no actual selected control holder");
    *selected = control.state.kind == QA_MOVEMENT_Q3;
    if (*selected) *out = control.state.data.q3;
    return true;
}

static bool native_movement_flags(void *context, qa_actor_id actor, uint32_t clear,
    uint32_t set, qa_error *error)
{
    return application_control_q3_flags(context, actor, clear, set, error);
}

static bool native_movement_policy(void *context, qa_actor_id actor, uint8_t fields,
    const qa_q3_wire_policy *policy, qa_error *error)
{
    return application_control_q3_policy(context, actor, fields, policy, error);
}

static bool native_mode(void *context, qa_actor_id actor, qa_q3_wire_mode *out,
    qa_error *error)
{
    application_provider *provider = context;
    qa_application *app = provider ? provider->application : NULL;
    uint32_t slot, actual_slot;
    qa_q3_wire_mode value = {0};
    if (!out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire requires its match score output");
    if (!native_source_actor(provider, actor, &slot, error)) return false;
    bool scoped = application_native_q3_source_entered(provider);
    qa_mode_id mode;
    if (scoped) {
        bool found;
        if (!application_native_q3_source_mode(provider, &mode, &found, error)) return false;
        if (!found)
            return application_fail(error, QA_ERROR_NOT_FOUND,
                "Native Q3 wire has no actual associated match score owner");
    } else {
        if (!app->primary_mode_ready)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Native Q3 wire has no current selected match score owner");
        mode = app->primary_mode;
    }
    if (!qa_modes_score(app->modes, mode, actor, &value.score, error) ||
        (scoped && !application_native_q3_source_mode_current(provider, mode, error)) ||
        !native_source_actor(provider, actor, &actual_slot, error)) return false;
    if (actual_slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire has no current selected match score owner");
    *out = value;
    return true;
}

bool application_native_q3_wire_bind_sources(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->application || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire binding requires its real GAME owner");
    qa_q3_wire_services services = {.context = provider, .movement = native_movement,
        .mode = native_mode, .movement_flags = native_movement_flags,
        .movement_policy = native_movement_policy};
    return qa_q3_wire_bind_services(provider->state.q3, &services, error);
}

typedef struct q3_wire_visibility {
    qa_collision_geometry *geometry;
    qa_cvars *native_cvars;
    qa_error failure;
} q3_wire_visibility;

typedef struct q3_wire_entity {
    qa_q3_entity state;
    qa_q3_host_visibility visibility;
} q3_wire_entity;

bool application_q3_wire_time(const application_provider *provider, int32_t *out,
    qa_error *error)
{
    qa_clock_state clock;
    if (!provider || !out || !provider->application ||
        !qa_session_clock(provider->application->session, provider->owner, &clock) ||
        clock.frame.provider != provider->owner || clock.frame.kind != QA_CLOCK_Q3 ||
        (clock.frame.number && clock.frame.phase != QA_FRAME_EXIT))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 snapshot requires its completed source server clock");
    uint32_t bits = (uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    memcpy(out, &bits, sizeof(bits));
    return true;
}

static bool wire_point(void *context, const float origin[3], int32_t *area,
    int32_t *cluster, qa_error *error)
{
    q3_wire_visibility *owner = context;
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(owner->geometry,
            qa_v3(origin[0], origin[1], origin[2]), &leaf, error)) return false;
    if (leaf.area < INT32_MIN || leaf.area > INT32_MAX ||
        leaf.cluster < INT32_MIN || leaf.cluster > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q3 snapshot visibility index exceeds the source range");
    *area = (int32_t)leaf.area;
    *cluster = (int32_t)leaf.cluster;
    return true;
}

static bool wire_area_bits(void *context, int32_t area, uint8_t accumulator[32],
    size_t *bytes, qa_error *error)
{
    q3_wire_visibility *owner = context;
    uint8_t bits[32];
    if (!qa_collision_area_bits(owner->geometry, area, bits, sizeof(bits), bytes,
                                error)) return false;
    for (size_t i = 0; i < *bytes; ++i) accumulator[i] |= bits[i];
    return true;
}

static bool wire_connected(void *context, int32_t first, int32_t second)
{
    q3_wire_visibility *owner = context;
    if (owner->native_cvars) {
        const qa_cvar_view *policy = qa_cvars_find(owner->native_cvars, "cm_noAreas");
        if (!policy)
            return application_fail(&owner->failure, QA_ERROR_NOT_FOUND,
                                    "Native Q3 area policy lost its actual source cvar");
        if (policy->number != 0) return true;
        if (first < 0 || second < 0) return false;
    }
    bool value = false;
    return qa_collision_areas_connected(owner->geometry, first, second, &value,
                                        &owner->failure) && value;
}

static bool wire_cluster_visible(void *context, int32_t first, int32_t second)
{
    q3_wire_visibility *owner = context;
    bool value = false;
    return qa_collision_cluster_visible(owner->geometry, first, second, false,
                                         &value, &owner->failure) && value;
}

static bool native_ready(application_provider *provider, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    return (app && app->state == QA_APPLICATION_RUNNING && !app->destroy_requested &&
        provider->kind == APPLICATION_PROVIDER_Q3 && provider->state.q3 &&
        provider->constructed && provider->attached && !provider->close_pending &&
        qa_session_safe(app->session) && !qa_session_faulted(app->session) &&
        qa_world_idle(app->world) && application_native_q3_wire_idle(provider)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 observation requires its actual idle GAME source");
}

static bool native_baselines(application_provider *provider, qa_q3_gamestate *out,
    qa_error *error)
{
    uint32_t count;
    if (!out || !native_ready(provider, error) ||
        !qa_q3_source_entity_count(provider->state.q3, &count, error)) return false;
    if (count > QA_Q3_ENTITY_NONE)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 physical entity extent exceeds baseline storage");
    qa_q3_gamestate *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return application_fail(error, QA_ERROR_MEMORY, "Capturing native Q3 physical source baselines");
    bool ok = true;
    for (uint32_t slot = 1; ok && slot < count; ++slot) {
        qa_q3_source_binding binding;
        qa_q3_wire_visibility visibility;
        ok = qa_q3_source_binding_read(provider->state.q3, slot, &binding, error);
        if (!ok || !binding.in_use) continue;
        ok = qa_q3_wire_entity_read(provider->state.q3, slot,
            &candidate->baselines[slot], &visibility, error);
        if (ok) candidate->baseline_present[slot] = visibility.present && visibility.linked;
    }
    if (ok) {
        memcpy(out->baselines, candidate->baselines, sizeof(out->baselines));
        memcpy(out->baseline_present, candidate->baseline_present, sizeof(out->baseline_present));
    }
    free(candidate);
    return ok;
}

bool application_native_q3_wire_current_view(application_provider *provider, uint32_t slot,
    qa_q3_player *player_out, qa_q3_visible_entities *visible_out, qa_error *error)
{
    uint32_t count;
    application_native_q3_wire_client_view client;
    bool admitted;
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !player_out || !visible_out || app->destroy_requested ||
        app->state != QA_APPLICATION_RUNNING || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached || provider->close_pending ||
        !application_native_q3_wire_client_admission_read(provider, slot, &client, &admitted, error) ||
        !qa_q3_source_entity_count(provider->state.q3, &count, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 view requires its current physical source");
    if (!admitted || count > QA_Q3_ENTITIES)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 view has no admitted physical source client");
    qa_q3_game *game = provider->state.q3;
    qa_world *source_world = app->world;
    qa_collision_geometry *geometry = qa_world_geometry(source_world);
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    if (!geometry || !cvars || !qa_cvars_find(cvars, "cm_noAreas"))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 view has no actual collision world");
    size_t extent = count ? count : 1;
    qa_q3_entity *records = calloc(extent, sizeof(*records));
    qa_q3_wire_native_visibility *visibility = calloc(extent, sizeof(*visibility));
    qa_q3_visibility_entity *entities = calloc(extent, sizeof(*entities));
    qa_q3_visible_entities *candidate = calloc(1, sizeof(*candidate));
    if (!records || !visibility || !entities || !candidate) {
        free(records); free(visibility); free(entities); free(candidate);
        return application_fail(error, QA_ERROR_MEMORY, "Observing native Q3 source visibility");
    }
    qa_q3_player player;
    bool ok = qa_q3_wire_player_read(game, slot, &player, error);
    for (uint32_t number = 0; ok && number < count; ++number) {
        qa_q3_source_binding binding;
        ok = qa_q3_source_binding_read(game, number, &binding, error);
        if (!ok || !binding.in_use) continue;
        qa_q3_wire_visibility source_link;
        ok = qa_q3_wire_entity_read(game, number, &records[number], &source_link, error) &&
             qa_q3_wire_native_visibility_read(game, number, &visibility[number], error);
        if (!ok) break;
        if (visibility[number].cluster_count > 128) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 source visibility cluster extent is invalid");
            break;
        }
        entities[number] = (qa_q3_visibility_entity){.state = &records[number],
            .linked = visibility[number].present && visibility[number].linked,
            .flags = visibility[number].server_flags, .single_client = visibility[number].single_client,
            .area = visibility[number].area, .area2 = visibility[number].area2,
            .last_cluster = visibility[number].last_cluster, .clusters = visibility[number].clusters,
            .cluster_count = visibility[number].cluster_count};
    }
    q3_wire_visibility owner = {.geometry = geometry, .native_cvars = cvars};
    qa_q3_visibility_world world = {.context = &owner, .point = wire_point,
        .area_bits = wire_area_bits, .areas_connected = wire_connected,
        .cluster_visible = wire_cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&player,
        entities, count, &world, false, candidate, error);
    if (ok && owner.failure.code) {
        if (error) *error = owner.failure;
        ok = false;
    }
    application_native_q3_wire_client_view actual;
    bool current;
    if (ok && (app->world != source_world || provider->state.q3 != game ||
        !application_native_q3_wire_client_admission_read(provider, slot, &actual, &current, error) ||
        !current || !qa_actor_id_equal(actual.actor, client.actor)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 view changed its source during observation");
    if (ok) { *player_out = player; *visible_out = *candidate; }
    free(records); free(visibility); free(entities); free(candidate);
    return ok;
}

static bool native_snapshot(application_provider *provider, uint32_t slot,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    int32_t milliseconds;
    if (!out || message < 0 || commands < 0 || !native_ready(provider, error) ||
        !application_q3_wire_time(provider, &milliseconds, error)) return false;
    qa_application_network_q3_frame *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return application_fail(error, QA_ERROR_MEMORY, "Observing native Q3 source snapshot");
    bool ok = application_native_q3_wire_current_view(provider, slot,
        &candidate->snapshot.player, &candidate->visible, error);
    if (ok) {
        candidate->snapshot.valid = true;
        candidate->snapshot.message_number = message;
        candidate->snapshot.server_command_number = commands;
        candidate->snapshot.server_time = milliseconds;
        candidate->snapshot.delta_number = -1;
        candidate->snapshot.flags = flags;
        candidate->snapshot.area_bytes = candidate->visible.area_bytes;
        memcpy(candidate->snapshot.area_mask, candidate->visible.area_mask, sizeof(candidate->snapshot.area_mask));
        candidate->snapshot.entity_count = candidate->visible.count;
        *out = *candidate;
        out->snapshot.entities = out->visible.entities;
    }
    free(candidate);
    return ok;
}

bool application_q3_wire_host_baselines(application_provider *provider,
    qa_q3_gamestate *out, qa_error *error)
{
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3)
        return native_baselines(provider, out, error);
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_application *app = provider ? provider->application : NULL;
    qa_q3_host_game_data data;
    if (!app || !out || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !engine || engine->restore_pending || !engine->map_ready || !engine->game ||
        !engine->game->ready || !engine->game->initialized || engine->game->retired ||
        !engine->game->host || engine->calls || engine->draining_clients ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) || !application_q3_guest_idle(provider) ||
        !qa_q3_host_game_data_read(engine->game->host, &data) ||
        data.entity_count > QA_Q3_ENTITY_NONE)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 baselines require their idle physical GAME source");
    qa_q3_gamestate *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 physical baselines");
    bool original_words = engine->game->abi == QA_QVM_Q3_MODERN;
    bool ok = true;
    for (uint32_t slot = 1; ok && slot < data.entity_count; ++slot) {
        qa_qvm_entity_shared shared;
        ok = original_words ?
            qa_q3_host_source_entity(engine->game->host, slot,
                                     &candidate->baselines[slot], &shared, error) :
            qa_q3_host_entity(engine->game->host, slot,
                              &candidate->baselines[slot], &shared, error);
        if (ok) candidate->baseline_present[slot] = shared.linked;
    }
    if (ok) {
        memcpy(out->baselines, candidate->baselines, sizeof(out->baselines));
        memcpy(out->baseline_present, candidate->baseline_present,
               sizeof(out->baseline_present));
    }
    free(candidate);
    return ok;
}

bool application_q3_wire_host_snapshot(application_provider *provider, uint32_t slot,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3)
        return native_snapshot(provider, slot, message, commands, flags, out, error);
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !out || slot >= 64 || message < 0 || commands < 0 ||
        app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !engine || engine->restore_pending || !engine->map_ready || !engine->game ||
        !engine->game->ready || !engine->game->initialized || engine->game->retired ||
        !engine->game->host || engine->calls || engine->draining_clients ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) ||
        !application_q3_guest_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 snapshot requires its idle admitted GAME source");
    const q3g_client *client = &engine->clients[slot];
    uint32_t actual_slot;
    if (!client->connected || client->pending_retirement ||
        !qa_actors_get(qa_session_actors(app->session), client->actor) ||
        !qa_q3_host_actor_slot(engine->game->host, client->actor, &actual_slot, error) ||
        actual_slot != slot)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 snapshot client has no current physical source admission");
    int32_t milliseconds;
    qa_q3_host_game_data data;
    qa_collision_geometry *geometry = qa_world_geometry(app->world);
    if (!application_q3_wire_time(provider, &milliseconds, error)) return false;
    if (!geometry || !qa_q3_host_game_data_read(engine->game->host, &data) ||
        data.entity_count > QA_Q3_ENTITIES || slot >= data.client_count)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 snapshot source records or collision geometry are absent");
    size_t count = data.entity_count ? data.entity_count : 1;
    q3_wire_entity *records = calloc(count, sizeof(*records));
    qa_q3_visibility_entity *entities = calloc(count, sizeof(*entities));
    qa_application_network_q3_frame *candidate = calloc(1, sizeof(*candidate));
    if (!records || !entities || !candidate) {
        free(records); free(entities); free(candidate);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating Q3 source snapshot");
    }
    bool original_words = engine->game->abi == QA_QVM_Q3_MODERN;
    bool ok = original_words ?
        qa_q3_host_source_player(engine->game->host, slot, &candidate->snapshot.player, error) :
        qa_q3_host_player(engine->game->host, slot, &candidate->snapshot.player, error);
    if (ok) candidate->snapshot.player.product = engine->product;
    for (uint32_t i = 0; ok && i < data.entity_count; ++i) {
        qa_qvm_entity_shared shared;
        bool present;
        ok = original_words ?
            qa_q3_host_source_entity(engine->game->host, i, &records[i].state, &shared, error) :
            qa_q3_host_entity(engine->game->host, i, &records[i].state, &shared, error);
        if (ok) ok = qa_q3_host_visibility_read(engine->game->host, i,
                                               &records[i].visibility, &present, error);
        if (!ok) break;
        if (records[i].visibility.cluster_count > 16) {
            ok = application_fail(error, QA_ERROR_FORMAT,
                                  "Q3 source visibility cluster storage is invalid");
            break;
        }
        entities[i] = (qa_q3_visibility_entity){
            .state = &records[i].state, .linked = shared.linked && present,
            .flags = (uint32_t)shared.server_flags, .single_client = shared.single_client,
            .area = records[i].visibility.area, .area2 = records[i].visibility.area2,
            .last_cluster = records[i].visibility.last_cluster,
            .clusters = records[i].visibility.clusters,
            .cluster_count = records[i].visibility.cluster_count};
    }
    q3_wire_visibility owner = {.geometry = geometry};
    qa_q3_visibility_world world = {.context = &owner, .point = wire_point,
        .area_bits = wire_area_bits, .areas_connected = wire_connected,
        .cluster_visible = wire_cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&candidate->snapshot.player,
        entities, data.entity_count, &world, false, &candidate->visible, error);
    if (ok && owner.failure.code) {
        if (error) *error = owner.failure;
        ok = false;
    }
    if (ok) {
        candidate->snapshot.valid = true;
        candidate->snapshot.message_number = message;
        candidate->snapshot.server_command_number = commands;
        candidate->snapshot.server_time = milliseconds;
        candidate->snapshot.delta_number = -1;
        candidate->snapshot.flags = flags;
        candidate->snapshot.area_bytes = candidate->visible.area_bytes;
        memcpy(candidate->snapshot.area_mask, candidate->visible.area_mask,
               sizeof(candidate->snapshot.area_mask));
        candidate->snapshot.entity_count = candidate->visible.count;
        *out = *candidate;
        out->snapshot.entities = out->visible.entities;
    }
    free(records); free(entities); free(candidate);
    return ok;
}
