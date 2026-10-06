#include "monsters/internal.h"
#include "entities/internal.h"
#include "items/internal.h"

static bool think_due(const qa_q2_game *g, const q2_actor *a) {
    if (a->entity && a->entity->think != Q2ET_NONE && a->entity->due_ns <= g->now_ns)
        return true;
    if (a->item) {
        if (a->item->companion) {
            if (a->item->companion->next_ns <= g->now_ns) return true;
        } else if (a->item->think != Q2_ITEM_IDLE && a->item->due_ns <= g->now_ns)
            return true;
    }
    const struct qa_q2_monster *m = a->monster;
    if (!m) return false;
    if (m->controller_kind != Q2M_CONTROLLER_NONE)
        return m->controller_ns <= g->now_ns;
    if (m->start_phase == Q2M_START_DORMANT || m->start_phase == Q2M_START_MANUAL ||
        m->start_due_ns > g->now_ns)
        return false;
    if (m->start_phase != Q2M_START_ACTIVE) return true;
    if (m->definition->species == Q2M_WIDOW2 && m->death_ns)
        return m->death_ns <= g->now_ns;
    if (m->gibbed) return false;
    if (m->corpse)
        return m->corpse_phase != Q2M_CORPSE_IDLE && m->corpse_due_ns <= g->now_ns;
    return !m->turret_attached;
}

static qa_source_frame source_frame(const qa_q2_game *g) {
    return (qa_source_frame){.provider = g->options.owner,
        .kind = g->options.edition == QA_Q2_CLASSIC ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE,
        .phase = QA_ENTITY_PHYSICS, .time_ns = g->now_ns, .elapsed_ns = g->frame_ns,
        .start_ns = g->now_ns >= g->frame_ns ? g->now_ns - g->frame_ns : 0};
}

static bool push_team(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->physics.flags & QA_PHYSICS_TEAM_SLAVE) return true;
    qa_actor_id leader = a->id;
    q2_push_frame **slot = &g->push_frames;
    while (*slot && (*slot)->active) slot = &(*slot)->next;
    q2_push_frame *frame = *slot;
    if (!frame) {
        frame = calloc(1, sizeof(*frame));
        if (!frame) {
            qa_error_set(e, QA_ERROR_MEMORY, leader.slot, "Capturing Q2 pusher team");
            return false;
        }
        *slot = frame;
    }
    frame->active = true;
    qa_physics_push *parts = frame->parts;
    bool ok = true;
    size_t count = 0;
    for (;;) {
        a = q2_actor_get(g, leader, false, NULL);
        if (!a) break;
        count = 0;
        q2_actor *part = a;
        while (part) {
            if (count == g->capacity) {
                qa_error_set(e, QA_ERROR_FORMAT, leader.slot, "Cyclic Q2 pusher team");
                ok = false;
                break;
            }
            for (size_t i = 0; i < count; ++i)
                if (qa_actor_id_equal(parts[i].actor, part->id)) {
                    qa_error_set(e, QA_ERROR_FORMAT, leader.slot, "Cyclic Q2 pusher team");
                    ok = false;
                    break;
                }
            if (!ok) break;
            if (count == frame->capacity) {
                size_t capacity = frame->capacity ? frame->capacity * 2 : 8;
                if (capacity < frame->capacity || capacity > SIZE_MAX / sizeof(*parts)) {
                    qa_error_set(e, QA_ERROR_MEMORY, leader.slot, "Q2 pusher team size overflow");
                    ok = false;
                    break;
                }
                qa_physics_push *grown = realloc(parts, capacity * sizeof(*parts));
                if (!grown) {
                    qa_error_set(e, QA_ERROR_MEMORY, leader.slot, "Capturing Q2 pusher team");
                    ok = false;
                    break;
                }
                parts = frame->parts = grown;
                frame->capacity = capacity;
            }
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, part->id, &body, e)) {
                ok = false;
                break;
            }
            float seconds = (float)((double)g->frame_ns / 1e9);
            parts[count++] = (qa_physics_push){.actor = part->id,
                .displacement = qa_vec_scale(body.velocity, seconds),
                .angular_displacement = qa_vec_scale(part->physics.angular_velocity, seconds)};
            qa_actor_reference link = part->entity ? part->entity->team_next : (qa_actor_reference){0};
            qa_actor_id next = qa_actor_reference_resolve(qa_session_actors(g->services.session), link);
            part = next.registry ? q2_actor_get(g, next, false, NULL) : NULL;
            if (next.registry && (!part || !part->entity || !part->physics_bound)) {
                qa_error_set(e, QA_ERROR_FORMAT, leader.slot, "Q2 pusher team lost its source part");
                ok = false;
                break;
            }
        }
        if (!ok) break;
        qa_physics_result result;
        if (!qa_physics_push_team(g->services.physics, parts, count, &result, e)) {
            ok = false;
            break;
        }
        if (result.status == QA_PHYSICS_BLOCKED) {
            if (g->options.edition == QA_Q2_RERELEASE && result.obstacle.registry &&
                !q2_actor_live(g, result.obstacle))
                continue;
            if (g->options.edition == QA_Q2_CLASSIC && !g->services.physics->services.blocked)
                for (size_t i = 0; i < count; ++i) {
                    part = q2_actor_get(g, parts[i].actor, false, NULL);
                    if (part && part->entity && part->entity->due_ns)
                        part->entity->due_ns = q2_deadline(part->entity->due_ns, g->frame_ns);
                }
            break;
        }
        /* G_RunEntity's current source actor remains the captain while each
         * part thinks; Move_Calc uses that identity to begin its team motion. */
        for (size_t i = 0; i < count; ++i) {
            part = q2_actor_get(g, parts[i].actor, false, NULL);
            if (part && !q2_actor_think(g, part, e)) {
                ok = false;
                break;
            }
        }
        break;
    }
    frame->active = false;
    return ok;
}

static bool step_environment(qa_q2_game *g, q2_actor *a, bool was_grounded, qa_error *e) {
    if (g->options.edition != QA_Q2_RERELEASE || !a->monster ||
        !a->monster->definition || !(a->physics.flags & QA_PHYSICS_MONSTER))
        return true;
    if (!qa_physics_check_ground(g->services.physics, a->id, e) ||
        !qa_physics_categorize_water(g->services.physics, a->id, e))
        return false;
    if (!q2_actor_live(g, a->id)) return true;
    q2m_context call = {.game = g, .actor = a, .monster = a->monster};
    if (!q2m_refresh(&call, e) || !q2m_world_effects(&call, e)) return false;
    if (!q2m_alive(&call)) return true;
    a->monster->water_level = a->physics.water_level;
    a->monster->water_type = a->physics.water_type;
    return qa_q2_monster_physics_changed(g, a->id, was_grounded, e);
}

static bool recover_step_origin(qa_q2_game *g, qa_actor_id id, qa_vec3 previous, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || a->physics.motion != QA_PHYSICS_STEP) return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e)) return false;
    if (body.origin.x == previous.x && body.origin.y == previous.y && body.origin.z == previous.z)
        return true;
    bool rerelease = g->options.edition == QA_Q2_RERELEASE;
    uint32_t mask = UINT32_C(0x02010003);
    if (rerelease) {
        mask = a->physics.clip_mask;
        if (!mask) mask = (a->physics.flags & QA_PHYSICS_MONSTER)
                             ? UINT32_C(0x42010003) : Q2_SHOT_MASK & ~UINT32_C(0x04000000);
        if (a->physics.solid == QA_PHYSICS_NOT_SOLID || a->physics.solid == QA_PHYSICS_TRIGGER ||
            ((a->physics.flags & QA_PHYSICS_DEAD) &&
             (a->physics.flags & (QA_PHYSICS_PLAYER | QA_PHYSICS_MONSTER))))
            mask &= ~(UINT32_C(0x02000000) | Q2_PLAYER_CONTENTS);
    }
    qa_trace_query query = {.start = body.origin, .end = previous,
        .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds}, .pass_actor = id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = mask;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e)) return false;
    if (!q2_actor_live(g, id) || (!trace.start_solid && !trace.all_solid)) return true;
    if (!qa_world_body_read(g->services.world, id, &body, e)) return false;
    body.origin = previous;
    return qa_world_body_write(g->services.world, id, &body, e);
}

static bool actor_physics(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_actor_id id = a->id;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    bool owned = record && record->owner == g->options.owner;
    bool recover = owned && a->physics_bound && a->physics.motion == QA_PHYSICS_STEP &&
        !a->client && a->projectile.kind == Q2_PROJECTILE_NONE &&
        (g->options.edition == QA_Q2_RERELEASE || g->options.product == QA_Q2_ROGUE);
    qa_body_state previous = {0};
    if (recover && !qa_world_body_read(g->services.world, id, &previous, e)) return false;
    if (a->entity && !q2_entity_prethink(g, a, e)) return false;
    a = q2_actor_get(g, id, false, NULL);
    if (!a) return true;
    if (!owned || !a->physics_bound ||
        !g->services.physics || a->client || a->projectile.kind != Q2_PROJECTILE_NONE)
        return q2_actor_think(g, a, e);
    qa_physics_motion motion = a->physics.motion;
    if (motion == QA_PHYSICS_PUSH || motion == QA_PHYSICS_STOP)
        return push_team(g, a, e);
    if (motion == QA_PHYSICS_STATIONARY)
        return q2_actor_think(g, a, e);
    qa_source_frame frame = source_frame(g);
    qa_physics_result result;
    if (motion != QA_PHYSICS_STEP) {
        bool noclip_think = motion == QA_PHYSICS_NOCLIP && think_due(g, a);
        if (!q2_actor_think(g, a, e)) return false;
        a = q2_actor_get(g, id, false, NULL);
        return !a || noclip_think || a->projectile.kind != Q2_PROJECTILE_NONE ||
               qa_physics_step(g->services.physics, id, &frame, &result, e);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e)) return false;
    if (!qa_actor_reference_present(body.ground)) {
        if (!qa_physics_check_ground(g->services.physics, id, e)) return false;
        if (!q2_actor_live(g, id)) return true;
        if (!qa_world_body_read(g->services.world, id, &body, e)) return false;
    }
    bool was_grounded = qa_actor_reference_present(body.ground);
    if (!qa_physics_step(g->services.physics, id, &frame, &result, e)) return false;
    a = q2_actor_get(g, id, false, NULL);
    if (!a || !step_environment(g, a, was_grounded, e)) return !a;
    a = q2_actor_get(g, id, false, NULL);
    if (a && !q2_actor_think(g, a, e)) return false;
    return !recover || recover_step_origin(g, id, previous.origin, e);
}

bool q2_actor_physics(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_actor_id id = a->id;
    if (!actor_physics(g, a, e)) return false;
    a = q2_actor_get(g, id, false, NULL);
    return !a || q2m_controller_postthink(g, a, e);
}
