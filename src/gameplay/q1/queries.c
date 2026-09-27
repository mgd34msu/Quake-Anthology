#include "internal.h"

bool q1_snapshot_actors(qa_q1_game *g, q1_actor_snapshot **out, qa_error *error) {
    q1_actor_snapshot *snapshot = g->snapshots;
    while (snapshot && snapshot->borrowed)
        snapshot = snapshot->next;
    if (!snapshot) {
        snapshot = calloc(1, sizeof(*snapshot));
        if (!snapshot) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q1 actor snapshot allocation failed");
            return false;
        }
        snapshot->next = g->snapshots;
        g->snapshots = snapshot;
    }
    if (!qa_builtin_observations(&g->services, &snapshot->shared, error))
        return false;
    snapshot->actors = snapshot->shared.ids;
    snapshot->count = snapshot->shared.count;
    snapshot->borrowed = true;
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
