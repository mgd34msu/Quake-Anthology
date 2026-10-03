#include "wire_internal.h"
#include "maps/internal.h"
#include "qa/game_q1_bots.h"

bool qa_q1_check_client_eye_read(const qa_q1_game *g, uint32_t slot, qa_vec3 *out,
    qa_error *error) {
    if (!g || !out || g->destroy_pending || g->continuation_pending || !g->wire ||
        slot >= g->options.max_clients || !g->wire->board) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q1 check-client eye lost its physical Source row");
        return false;
    }
    *out = g->wire->board[slot].eye;
    return true;
}
bool qa_q1_check_client_eye_store(qa_q1_game *g, qa_actor_id actor, qa_vec3 eye,
    qa_error *error) {
    uint32_t slot;
    if (!qa_vec_finite(eye) || !qa_q1_native_client_slot(g, actor, &slot, error) ||
        !g->wire || !g->wire->board) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 check-client eye lost its actual Source client");
        return false;
    }
    g->wire->board[slot].eye = eye;
    return true;
}
bool qa_q1_check_client_eye_clear(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    return qa_q1_check_client_eye_store(g, actor, qa_v3(0, 0, 0), error);
}
static bool client_health(qa_q1_game *g, qa_actor_id actor, float *out, qa_error *error) {
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, actor, &combat, error)) return false;
    *out = combat.health;
    return true;
}
static bool observer_eye(qa_q1_game *g, qa_actor_id actor, qa_q1_check_client_eye read,
    void *context, qa_vec3 *out, qa_error *error) {
    uint32_t slot;
    if (qa_q1_native_client_slot(g, actor, &slot, NULL)) return read(context, actor, out, error);
    const q1_actor *entity = q1_entity_const(g, actor);
    qa_body_state body;
    if (!entity || !entity->native || !qa_world_body_read(g->services.world, actor, &body, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 check-client observer lost its Source entity");
        return false;
    }
    qa_vec3 offset = qa_v3(0, 0, 0);
    if (entity->map && entity->map->has_view_offset) offset = entity->map->view_offset;
    else if (entity->kind == Q1_MONSTER) {
        qa_builtin_actor_traits traits;
        if (!qa_q1_game_actor_traits(g, actor, &traits)) return false;
        offset.z = traits.view_height;
    }
    *out = qa_vec_add(body.origin, offset);
    return true;
}
bool qa_q1_game_check_client(qa_q1_game *g, qa_actor_id observer,
    qa_q1_check_client_eye read, void *context, qa_actor_id *out, qa_error *error) {
    if (!read || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, observer.slot, "Q1 check-client requires its actual eye reader and result");
        return false;
    }
    *out = (qa_actor_id){0};
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    qa_clock_state clock;
    bool okay = g->wire && g->wire->board && g->options.max_clients != 0 &&
        qa_session_clock(g->services.session, g->options.provider, &clock);
    if (!okay) qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 check-client requires its physical clients and Source server clock");
    if (okay && (double)clock.frame.time_ns / 1000000000.0 - g->check_client_time >= 0.1) {
        uint32_t previous = g->check_client_slot;
        if (previous < 1) previous = 1;
        if (previous > g->options.max_clients) previous = g->options.max_clients;
        uint32_t slot = previous == g->options.max_clients ? 1 : previous + 1;
        qa_actor_id candidate = {0};
        for (;;) {
            bool present = qa_q1_source_client_actor(g, slot - 1, &candidate);
            if (slot == previous) break;
            if (present) {
                float health;
                qa_q1_source_client_view client;
                okay = client_health(g, candidate, &health, error) &&
                    qa_q1_source_client_read(g, candidate, &client);
                if (!okay || (!(health <= 0) && !client.no_target)) break;
            }
            slot = slot == g->options.max_clients ? 1 : slot + 1;
        }
        if (okay) {
            g->check_client_slot = slot;
            g->check_client_time = (double)clock.frame.time_ns / 1000000000.0;
            qa_vec3 eye;
            if (qa_q1_source_client_actor(g, slot - 1, &candidate))
                okay = read(context, candidate, &eye, error) &&
                    qa_q1_check_client_eye_store(g, candidate, eye, error);
            else okay = qa_q1_check_client_eye_read(g, slot - 1, &eye, error);
            qa_collision_leaf leaf;
            if (okay) okay = qa_collision_point_leaf(qa_world_geometry(g->services.world), eye, &leaf, error);
            if (okay && (leaf.cluster < -1 || leaf.cluster > INT32_MAX)) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 check-client leaf exceeds its Source index");
                okay = false;
            }
            if (okay) g->check_client_cluster = (int32_t)leaf.cluster;
        }
    }
    qa_actor_id candidate;
    if (okay && g->check_client_slot &&
        qa_q1_source_client_actor(g, g->check_client_slot - 1, &candidate)) {
        float health;
        okay = client_health(g, candidate, &health, error);
        if (okay && !(health <= 0)) {
            qa_vec3 eye;
            qa_collision_leaf leaf;
            bool visible;
            okay = observer_eye(g, observer, read, context, &eye, error) &&
                qa_collision_point_leaf(qa_world_geometry(g->services.world), eye, &leaf, error);
            if (okay && (leaf.cluster < -1 || leaf.cluster > INT32_MAX)) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 check-client observer exceeds its Source leaf index");
                okay = false;
            }
            if (okay) okay = qa_collision_cluster_visible(qa_world_geometry(g->services.world),
                g->check_client_cluster, (int32_t)leaf.cluster, false, &visible, error);
            if (okay && visible) *out = candidate;
        }
    }
    if (okay && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, observer.slot, "Q1 check-client lost its retained Source operation");
        okay = false;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}

static q1_actor_snapshot *snapshot_slot(qa_q1_game *g, qa_error *error) {
    q1_actor_snapshot *snapshot = g->snapshots;
    while (snapshot && snapshot->borrowed)
        snapshot = snapshot->next;
    if (!snapshot) {
        snapshot = calloc(1, sizeof(*snapshot));
        if (!snapshot) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q1 actor snapshot allocation failed");
            return NULL;
        }
        snapshot->next = g->snapshots;
        g->snapshots = snapshot;
    }
    return snapshot;
}
static bool snapshot_acquire(qa_q1_game *g, bool players, q1_actor_snapshot **out,
                             qa_error *error) {
    q1_actor_snapshot *snapshot = snapshot_slot(g, error);
    if (!snapshot)
        return false;
    snapshot->borrowed = true;
    if (!(players ? qa_builtin_players(&g->services, &snapshot->shared, error)
                  : qa_builtin_observations(&g->services, &snapshot->shared, error))) {
        snapshot->borrowed = false;
        return false;
    }
    snapshot->actors = snapshot->shared.ids;
    snapshot->count = snapshot->shared.count;
    *out = snapshot;
    return true;
}

bool q1_snapshot_actors(qa_q1_game *g, q1_actor_snapshot **out, qa_error *error) {
    return snapshot_acquire(g, false, out, error);
}
bool q1_snapshot_players(qa_q1_game *g, q1_actor_snapshot **out, qa_error *error) {
    return snapshot_acquire(g, true, out, error);
}
bool q1_snapshot_targets(qa_q1_game *g, qa_targets *targets, qa_string_id name,
                         q1_actor_snapshot **out, qa_error *error) {
    q1_actor_snapshot *snapshot = snapshot_slot(g, error);
    if (!snapshot ||
        !qa_builtin_snapshot_reserve(
            &snapshot->shared, qa_actors_capacity(qa_session_actors(g->services.session)), error))
        return false;
    snapshot->borrowed = true;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    size_t count = 0;
    while (qa_targets_next(targets, name, &cursor, &actor))
        snapshot->shared.ids[count++] = actor;
    snapshot->shared.count = snapshot->count = count;
    snapshot->actors = snapshot->shared.ids;
    *out = snapshot;
    return true;
}

bool q1_radius_snapshot(qa_q1_game *g, qa_vec3 origin, float radius, q1_actor_snapshot **out,
                        qa_error *error) {
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id actor = snapshot->actors[i];
        const q1_actor *native = q1_entity_const(g, actor);
        qa_body_state body;
        if ((native && native->physics.solid == QA_PHYSICS_NOT_SOLID) ||
            !qa_world_body_read(g->services.world, actor, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(origin, center)) <= radius)
            snapshot->actors[count++] = actor;
    }
    snapshot->count = count;
    *out = snapshot;
    return true;
}
