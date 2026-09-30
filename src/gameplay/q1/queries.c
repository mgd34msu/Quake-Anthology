#include "internal.h"

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
