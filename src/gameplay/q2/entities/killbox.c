#include "internal.h"

static bool overlaps(qa_bounds a, qa_bounds b) {
    return a.mins.x <= b.maxs.x && a.maxs.x >= b.mins.x && a.mins.y <= b.maxs.y &&
           a.maxs.y >= b.mins.y && a.mins.z <= b.maxs.z && a.maxs.z >= b.mins.z;
}
static bool classic(qa_q2_game *g, q2_actor *source, qa_actor_id credit, bool *clear, qa_error *e) {
    *clear = false;
    for (;;) {
        qa_body_state body;
        qa_trace_result hit;
        if (!qa_world_body_read(g->services.world, source->id, &body, e) ||
            !q2_player_trace(g, source->id, body.origin, body.origin, &body.bounds, 0x2010003, &hit,
                             e))
            return false;
        if (hit.hit != QA_TRACE_HIT_ACTOR) {
            *clear = !hit.start_solid && !hit.all_solid;
            return true;
        }
        qa_actor_id victim = hit.actor;
        if (!q2_target_damageable(g, victim))
            return true;
        if (!q2_entity_damage(g, source, victim, credit, 100000, 0, 21, 32, e))
            return false;
        if (!q2_actor_live(g, source->id))
            return true;
        if (!q2_actor_live(g, victim))
            continue;
        q2_actor *native = q2_actor_get(g, victim, false, NULL);
        if (native && native->physics_bound) {
            if (native->physics.solid != QA_PHYSICS_NOT_SOLID)
                return true;
        } else {
            if (!q2_player_trace(g, source->id, body.origin, body.origin, &body.bounds, 0x2010003,
                                 &hit, e))
                return false;
            *clear = hit.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(hit.actor, victim);
            return true;
        }
    }
}
bool q2_killbox(qa_q2_game *g, qa_actor_id id, qa_actor_id credited, bool spawning, bool exact,
                bool *clear, qa_error *e) {
    if (!g || !clear || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 killbox");
        return false;
    }
    q2_actor local = {.id = id};
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a)
        a = &local;
    if (g->options.edition == QA_Q2_CLASSIC)
        return classic(g, a, credited, clear, e);
    *clear = true;
    if (a->client && a->client->info.noclip)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    qa_bounds box = {qa_vec_add(body.origin, body.bounds.mins),
                     qa_vec_add(body.origin, body.bounds.maxs)};
    q2_trace_frame *frame = q2_scratch_acquire(g, e);
    if (!frame)
        return false;
    bool okay = false;
    qa_actor_registry *actors = qa_session_actors(g->services.session);
    if (!qa_builtin_snapshot_reserve(&frame->snapshot, qa_actors_count(actors), e))
        goto out;
    frame->snapshot.count = 0;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    while (qa_actors_next(actors, &cursor, &record))
        frame->snapshot.ids[frame->snapshot.count++] = record->id;
    qa_builtin_actor_traits source_traits = {0};
    if (g->services.actor_traits)
        g->services.actor_traits(g->services.context, id, &source_traits);
    for (size_t i = 0; i < frame->snapshot.count; i++) {
        qa_actor_id target = frame->snapshot.ids[i];
        qa_linked_body linked;
        if (qa_actor_id_equal(id, target) || !q2_actor_live(g, target) ||
            !qa_world_linked(g->services.world, target, &linked) ||
            !overlaps(box, linked.absolute_bounds) || !q2_target_damageable(g, target))
            continue;
        q2_actor *native = q2_actor_get(g, target, false, NULL);
        if (native && native->physics_bound && native->physics.solid != QA_PHYSICS_BOX)
            continue;
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits)
            g->services.actor_traits(g->services.context, target, &traits);
        if (spawning && g->options.cooperative && !g->player_runtime->rules.coop_player_collision &&
            traits.player)
            continue;
        if (exact && a->entity) {
            bool inside;
            if (!q2_entity_clip(g, a, target, &inside, e))
                goto out;
            if (!inside)
                continue;
        }
        if (g->options.cooperative && source_traits.player && traits.player) {
            if (a->client)
                a->client->player_collision = false;
            if (native && native->client)
                native->client->player_collision = false;
            if (a->physics_bound)
                a->physics.clip_mask &= ~Q2_PLAYER_CONTENTS;
            if (native && native->physics_bound)
                native->physics.clip_mask &= ~Q2_PLAYER_CONTENTS;
            qa_q2_player_services *services = &g->player_runtime->services;
            if (services->player_collision &&
                (!services->player_collision(services->context, id, false, e) ||
                 (q2_actor_live(g, target) &&
                  !services->player_collision(services->context, target, false, e))))
                goto out;
        } else if (!q2_entity_damage(g, a, target, credited, 100000, 0, spawning ? 57 : 21, 32, e))
            goto out;
        if (!q2_actor_live(g, id))
            break;
    }
    okay = true;
out:
    frame->active = false;
    return okay;
}
bool qa_q2_entities_killbox(qa_q2_game *g, qa_actor_id id, qa_actor_id credited, bool *clear,
                            qa_error *e) {
    return q2_killbox(g, id, credited, false, false, clear, e);
}
