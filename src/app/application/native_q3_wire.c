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
    bool native_areas, ignore_areas;
    qa_error failure;
} q3_wire_visibility;

typedef struct q3_wire_entity {
    qa_q3_entity state;
    int32_t clusters[128];
} q3_wire_entity;

struct application_q3_wire_capture {
    q3_wire_entity *records;
    qa_q3_visibility_entity *entities;
    size_t capacity;
    uint32_t count, stride;
    uint64_t address, application_frame, mutation, actors_revision;
    uint64_t publication, map_revision, source_frame, source_time, elapsed, debt;
    const void *source;
    qa_session *session;
    qa_world *world;
    qa_collision_geometry *geometry;
    bool ready, paused;
    qa_application_network_q3_frame candidate;
};

void application_q3_wire_capture_dispose(application_provider *provider)
{
    struct application_q3_wire_capture *capture = provider->q3_wire_capture;
    if (!capture) return;
    free(capture->records); free(capture->entities); free(capture);
    provider->q3_wire_capture = NULL;
}

static struct application_q3_wire_capture *capture_workspace(application_provider *provider,
    qa_error *error)
{
    if (!provider->q3_wire_capture) {
        provider->q3_wire_capture = calloc(1, sizeof(*provider->q3_wire_capture));
        if (!provider->q3_wire_capture)
            application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 source snapshot workspace");
    }
    return provider->q3_wire_capture;
}

static bool capture_entities(application_provider *provider, qa_q3_game *game,
    qa_q3_host *host, const qa_q3_host_game_data *data, uint32_t count,
    bool original_words, struct application_q3_wire_capture **out, qa_error *error)
{
    qa_application *app = provider->application;
    qa_clock_state clock = {0};
    bool clock_present = qa_session_clock(app->session, provider->owner, &clock);
    uint64_t frame = application_frame_revision(app);
    uint64_t actors = qa_actors_revision(qa_session_actors(app->session));
    qa_collision_geometry *geometry = qa_world_geometry(app->world);
    const void *source = game ? (const void *)game : (const void *)host;
    uint64_t address = data ? data->entities_address : 0;
    uint32_t stride = data ? data->entity_stride : 0;
    struct application_q3_wire_capture *capture = capture_workspace(provider, error);
    if (!capture) return false;
    *out = capture;
    if (clock_present && capture->ready && capture->source == source && capture->session == app->session &&
        capture->world == app->world && capture->geometry == geometry &&
        capture->count == count && capture->address == address && capture->stride == stride &&
        capture->application_frame == frame && capture->mutation == app->snapshot_mutation &&
        capture->actors_revision == actors && capture->publication == app->publication_generation &&
        capture->map_revision == app->map_revision && capture->source_frame == clock.frame.number &&
        capture->source_time == clock.frame.time_ns && capture->elapsed == clock.elapsed_ns &&
        capture->debt == clock.debt_ns && capture->paused == clock.paused) return true;
    capture->ready = false;
    if (count > capture->capacity) {
        q3_wire_entity *records = malloc((size_t)count * sizeof(*records));
        qa_q3_visibility_entity *entities = malloc((size_t)count * sizeof(*entities));
        if (!records || !entities) {
            free(records); free(entities);
            return application_fail(error, QA_ERROR_MEMORY, "Growing Q3 source snapshot workspace");
        }
        free(capture->records); free(capture->entities);
        capture->records = records; capture->entities = entities; capture->capacity = count;
    }
    if (count) memset(capture->entities, 0, (size_t)count * sizeof(*capture->entities));
    uint64_t mutation = app->snapshot_mutation;
    qa_world *world = app->world;
    for (uint32_t i = 0; i < count; ++i) {
        q3_wire_entity *record = &capture->records[i];
        qa_q3_visibility_entity *entity = &capture->entities[i];
        if (game) {
            qa_q3_source_binding binding;
            if (!qa_q3_source_binding_read(game, i, &binding, error)) return false;
            if (!binding.in_use) continue;
            qa_q3_wire_visibility link;
            qa_q3_wire_native_visibility visibility;
            if (!qa_q3_wire_entity_read(game, i, &record->state, &link, error) ||
                !qa_q3_wire_native_visibility_read(game, i, &visibility, error)) return false;
            if (visibility.cluster_count > 128)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q3 source visibility cluster extent is invalid");
            memcpy(record->clusters, visibility.clusters, visibility.cluster_count * sizeof(*record->clusters));
            *entity = (qa_q3_visibility_entity){.state = &record->state,
                .linked = visibility.present && visibility.linked, .flags = visibility.server_flags,
                .single_client = visibility.single_client, .area = visibility.area, .area2 = visibility.area2,
                .last_cluster = visibility.last_cluster, .clusters = record->clusters,
                .cluster_count = visibility.cluster_count};
        } else {
            qa_qvm_entity_shared shared;
            qa_q3_host_visibility visibility;
            bool present;
            bool ok = original_words ? qa_q3_host_source_entity(host, i, &record->state, &shared, error) :
                qa_q3_host_entity(host, i, &record->state, &shared, error);
            if (!ok || !qa_q3_host_visibility_read(host, i, &visibility, &present, error)) return false;
            if (visibility.cluster_count > 16)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source visibility cluster storage is invalid");
            memcpy(record->clusters, visibility.clusters, visibility.cluster_count * sizeof(*record->clusters));
            *entity = (qa_q3_visibility_entity){.state = &record->state, .linked = shared.linked && present,
                .flags = (uint32_t)shared.server_flags, .single_client = shared.single_client,
                .area = visibility.area, .area2 = visibility.area2, .last_cluster = visibility.last_cluster,
                .clusters = record->clusters, .cluster_count = visibility.cluster_count};
        }
    }
    if (app->snapshot_mutation != mutation || app->world != world)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 entity capture changed its returned Source boundary");
    capture->source = source; capture->session = app->session;
    capture->world = world; capture->geometry = geometry; capture->count = count;
    capture->address = address; capture->stride = stride; capture->application_frame = frame;
    capture->mutation = mutation; capture->actors_revision = actors;
    capture->publication = app->publication_generation; capture->map_revision = app->map_revision;
    capture->source_frame = clock.frame.number; capture->source_time = clock.frame.time_ns;
    capture->elapsed = clock.elapsed_ns; capture->debt = clock.debt_ns; capture->paused = clock.paused;
    capture->ready = clock_present;
    return true;
}

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
    if (owner->native_areas) {
        if (owner->ignore_areas) return true;
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
    const qa_cvar_view *areas = cvars ? qa_cvars_find(cvars, "cm_noAreas") : NULL;
    if (!geometry || !cvars || !areas)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 view has no actual collision world");
    struct application_q3_wire_capture *capture = capture_workspace(provider, error);
    if (!capture) return false;
    qa_q3_player player;
    bool ok = qa_q3_wire_player_read(game, slot, &player, error) &&
        capture_entities(provider, game, NULL, NULL, count, false, &capture, error);
    q3_wire_visibility owner = {.geometry = geometry, .native_areas = true,
        .ignore_areas = areas->number != 0};
    qa_q3_visibility_world world = {.context = &owner, .point = wire_point,
        .area_bits = wire_area_bits, .areas_connected = wire_connected,
        .cluster_visible = wire_cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&player,
        capture->entities, count, &world, false, &capture->candidate.visible, error);
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
    if (ok) { *player_out = player; *visible_out = capture->candidate.visible; }
    return ok;
}

static bool native_snapshot(application_provider *provider, uint32_t slot,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    int32_t milliseconds;
    if (!out || message < 0 || commands < 0 || !native_ready(provider, error) ||
        !application_q3_wire_time(provider, &milliseconds, error)) return false;
    struct application_q3_wire_capture *capture = capture_workspace(provider, error);
    if (!capture) return false;
    qa_application_network_q3_frame *candidate = &capture->candidate;
    candidate->snapshot = (qa_q3_snapshot){0};
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
    struct application_q3_wire_capture *capture = capture_workspace(provider, error);
    if (!capture) return false;
    qa_application_network_q3_frame *candidate = &capture->candidate;
    candidate->snapshot = (qa_q3_snapshot){0};
    bool original_words = engine->game->abi == QA_QVM_Q3_MODERN;
    bool ok = original_words ?
        qa_q3_host_source_player(engine->game->host, slot, &candidate->snapshot.player, error) :
        qa_q3_host_player(engine->game->host, slot, &candidate->snapshot.player, error);
    if (ok) candidate->snapshot.player.product = engine->product;
    if (ok) ok = capture_entities(provider, NULL, engine->game->host, &data,
        data.entity_count, original_words, &capture, error);
    q3_wire_visibility owner = {.geometry = geometry};
    qa_q3_visibility_world world = {.context = &owner, .point = wire_point,
        .area_bits = wire_area_bits, .areas_connected = wire_connected,
        .cluster_visible = wire_cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&candidate->snapshot.player,
        capture->entities, data.entity_count, &world, false, &candidate->visible, error);
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
    return ok;
}
