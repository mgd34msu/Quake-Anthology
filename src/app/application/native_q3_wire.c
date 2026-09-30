#include "native_q3_wire.h"
#include "guest_q3_private.h"

typedef struct q3_wire_visibility {
    qa_collision_geometry *geometry;
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

bool application_q3_wire_host_snapshot(application_provider *provider, uint32_t slot,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
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
    if (!client->connected || !client->begun || client->pending_retirement ||
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
