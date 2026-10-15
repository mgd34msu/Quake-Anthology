#include "internal.h"
#include "hints.h"
#include "../monsters/internal.h"
#include "qa/stamp.h"

static q2_actor *node(qa_q2_game *g, qa_actor_id id) {
    return q2_ent(g, id);
}

static qa_actor_id next_node(qa_q2_game *g, const q2_actor *a) {
    return qa_actor_reference_resolve(qa_session_actors(g->services.session),
                                      a->entity->hint_next);
}

static void diagnostic(qa_q2_game *g, qa_actor_id id, const char *reason) {
    if (!g->hooks.source_log) return;
    qa_body_state body = {0};
    (void)qa_world_body_read(g->services.world, id, &body, NULL);
    char text[256];
    (void)snprintf(text, sizeof(text), "hint_path at (%.0f %.0f %.0f): %s\n",
                  body.origin.x, body.origin.y, body.origin.z, reason);
    g->hooks.source_log(g->hooks.context, text);
}

bool q2_hint_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = s->classname == g->runtime_names[Q2_NAME_HINT_PATH];
    if (!*handled) return true;
    if (g->options.deathmatch) return qa_session_release(g->services.session, a->id, e);
    if (!s->target && !s->targetname) {
        qa_actor_id id = a->id;
        diagnostic(g, id, "unlinked");
        return !q2_actor_live(g, id) || qa_session_release(g->services.session, id, e);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e)) return false;
    if (!q2_actor_live(g, a->id)) return true;
    s->kind = Q2E_PATH;
    s->hint_chain_id = -1;
    s->hint_next = (qa_actor_reference){0};
    s->touchable = true;
    s->visual.visible = false;
    body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
    return q2_entity_body(g, a, &body, false, e) && q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e);
}

bool q2_hints_init(qa_q2_game *g, qa_error *e) {
    q2_entities *r = g->entity_runtime;
    r->hints_present = false;
    r->hint_count = 0;
    memset(r->hint_starts, 0, sizeof(r->hint_starts));
    if (!r->services.targets) return true;
    qa_target_cursor first = {0};
    qa_actor_id probe;
    if (!qa_targets_next_authored(r->services.targets,
            g->runtime_names[Q2_NAME_HINT_PATH], &first, &probe)) return true;
    qa_world_actor_snapshot custody = {0};
    if (!q2_scratch_acquire(g, &custody, e)) return false;
    qa_arena scratch;
    qa_arena_init_pool(&scratch, &g->player_runtime->scratch_pages);
    uint32_t *marks = qa_arena_alloc(&scratch, g->capacity * sizeof(*marks),
                                    _Alignof(uint32_t), e);
    bool okay = marks != NULL;
    qa_stamp_set visited;
    if (!okay) goto out;
    qa_stamp_set_init(&visited, marks, g->capacity);
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    qa_targets *targets = r->services.targets;
    while (qa_targets_next_authored(targets, g->runtime_names[Q2_NAME_HINT_PATH], &cursor, &id)) {
        q2_actor *a = node(g, id);
        if (!a) continue;
        q2_entity_state *s = a->entity;
        s->hint_chain_id = -1;
        s->hint_next = (qa_actor_reference){0};
        r->hints_present = true;
        if (!(s->spawnflags & 1u) || !s->target) continue;
        if (s->targetname) {
            diagnostic(g, id, "endpoint has both target and targetname");
            continue;
        }
        if (r->hint_count == QA_Q2_HINT_CHAIN_LIMIT) continue;
        r->hint_starts[r->hint_count++] = id;
    }
    for (size_t chain = 0; chain < r->hint_count; ++chain) {
        qa_actor_id root = r->hint_starts[chain], current = root;
        qa_stamp_set_begin(&visited);
        while (current.registry) {
            q2_actor *a = node(g, current);
            if (!a) break;
            a->entity->hint_chain_id = (int32_t)chain;
            (void)qa_stamp_set_mark(&visited, current.slot);
            qa_string_id target = a->entity->target;
            if (!target) break;
            qa_target_cursor found = {0};
            qa_actor_id next = {0}, extra;
            if (!qa_targets_next(targets, target, &found, &next)) break;
            bool fork = qa_targets_next(targets, target, &found, &extra);
            q2_actor *following = node(g, next);
            bool circular = following && (qa_stamp_set_test(&visited, next.slot) ||
                qa_actor_reference_present(following->entity->hint_next));
            if (fork || circular) {
                diagnostic(g, current, circular ? "circular path" : "forked path");
                a = node(g, root);
                if (a) a->entity->hint_next = (qa_actor_reference){0};
                break;
            }
            if (!following) {
                diagnostic(g, current, "target has no Q2 authored state");
                break;
            }
            a = node(g, current);
            if (!a) break;
            a->entity->hint_next = qa_actor_reference_lifetime(next);
            current = next;
        }
    }
out:
    qa_arena_destroy(&scratch);
    q2_scratch_release(g, &custody);
    return okay;
}

static bool go(q2m_context *c, qa_actor_id point, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, point, &body, e))
        return !q2m_alive(c) || !q2_actor_live(c->game, point);
    if (!q2m_alive(c)) return true;
    struct qa_q2_monster *m = c->monster;
    qa_vec3 direction = qa_vec_sub(body.origin, c->body.origin);
    m->ideal_yaw = direction.x == 0.0f
        ? direction.y == 0.0f ? 0.0f : direction.y > 0.0f ? 90.0f : 270.0f
        : qa_vec_yaw(direction, c->game->options.rules == QA_RULESET_Q2_RERELEASE
            ? QA_DIRECTION_FLOAT_RATIO : QA_DIRECTION_NATIVE_FLOAT, true);
    m->goal = m->move_target = point;
    c->actor->physics.goal = qa_actor_reference_lifetime(point);
    c->actor->physics.ideal_yaw = m->ideal_yaw;
    m->pause_ns = 0;
    m->hint_path = true;
    m->sound_target.present = false;
    m->pursuit_last_seen = m->pursue_next = m->pursue_temporary = false;
    m->search_ns = c->game->now_ns;
    return q2m_run_state(c, e);
}

bool q2_hint_stop(q2m_context *c, qa_error *e) {
    struct qa_q2_monster *m = c->monster;
    m->goal = m->move_target = m->goal_hint = (qa_actor_id){0};
    c->actor->physics.goal = (qa_actor_reference){0};
    m->last_hint_ns = c->game->now_ns;
    m->hint_path = false;
    qa_combat_state combat;
    if (q2_actor_live(c->game, m->enemy) && !m->sound_target.present &&
        qa_combat_read(c->game->services.combat, m->enemy, &combat, NULL) && combat.health >= 1) {
        bool visible;
        if (!q2m_visible(c, m->enemy, &visible, e)) return false;
        if (!q2m_alive(c)) return true;
        return visible ? q2m_found_target(c, m->enemy, e) : q2m_hunt_target(c, true, e);
    }
    m->enemy = (qa_actor_id){0};
    c->actor->physics.enemy = (qa_actor_reference){0};
    m->pause_ns = c->game->options.rules == QA_RULESET_Q2_RERELEASE
        ? UINT64_MAX : q2m_after(c->game->now_ns, 100000000.0);
    return q2m_stand_state(c, e);
}

static bool visible_from(qa_q2_game *g, qa_actor_id observer, qa_actor_id target,
                          bool *visible, qa_error *e) {
    *visible = false;
    qa_body_state from, to;
    if (!qa_world_body_read(g->services.world, observer, &from, NULL) ||
        !qa_world_body_read(g->services.world, target, &to, NULL)) return true;
    qa_builtin_actor_traits observer_traits = {0}, target_traits = {0};
    if (g->services.actor_traits) {
        g->services.actor_traits(g->services.context, observer, &observer_traits);
        g->services.actor_traits(g->services.context, target, &target_traits);
    }
    from.origin.z += observer_traits.view_height;
    to.origin.z += target_traits.view_height;
    qa_trace_query query = {.start = from.origin, .end = to.origin, .pass_actor = observer,
        .policy = qa_collision_ruleset_policy(g->options.rules)};
    query.policy.contents_mask = qa_collision_contents_mask(Q2M_OPAQUE_MASK, QA_GAME_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e)) return false;
    *visible = trace.fraction == 1.0f;
    return true;
}

typedef struct hint_candidate {
    qa_actor_id start, destination;
    uint64_t order;
} hint_candidate;

bool q2_hint_check_lost(q2m_context *c, bool *accepted, qa_error *e) {
    *accepted = false;
    qa_q2_game *g = c->game;
    struct qa_q2_monster *m = c->monster;
    q2_entities *r = g->entity_runtime;
    if (g->now_ns < m->trail_ns || g->now_ns - m->trail_ns < 5 * Q2M_SECOND ||
        g->now_ns < m->last_hint_ns || g->now_ns - m->last_hint_ns < 10 * Q2M_SECOND) return true;
    m->last_hint_ns = g->now_ns;
    if (!r->hints_present || !r->hint_count || !m->enemy.registry || m->stand_ground ||
        m->definition->species == Q2M_TURRET) return true;
    qa_body_state enemy;
    if (!qa_world_body_read(g->services.world, m->enemy, &enemy, NULL)) return true;
    qa_world_actor_snapshot custody = {0};
    if (!q2_scratch_acquire(g, &custody, e)) return false;
    qa_arena scratch;
    qa_arena_init_pool(&scratch, &g->player_runtime->scratch_pages);
    hint_candidate *candidates = qa_arena_alloc(&scratch, r->hint_count * sizeof(*candidates),
                                               _Alignof(hint_candidate), e);
    uint32_t *marks = qa_arena_alloc(&scratch, g->capacity * sizeof(*marks),
                                    _Alignof(uint32_t), e);
    bool okay = candidates && marks;
    if (!okay) goto out;
    memset(candidates, 0, r->hint_count * sizeof(*candidates));
    qa_stamp_set visited;
    qa_stamp_set_init(&visited, marks, g->capacity);
    uint64_t order = 0;
    for (size_t chain = 0; chain < r->hint_count; ++chain) {
        qa_stamp_set_begin(&visited);
        for (qa_actor_id id = r->hint_starts[chain]; id.registry;) {
            q2_actor *a = node(g, id);
            if (!a || !qa_stamp_set_mark(&visited, id.slot)) break;
            qa_actor_id next = next_node(g, a);
            int32_t group = a->entity->hint_chain_id;
            qa_body_state body;
            if (group >= 0 && (size_t)group < r->hint_count &&
                qa_world_body_read(g->services.world, id, &body, NULL) &&
                qa_vec_length(qa_vec_sub(c->body.origin, body.origin)) <= 512.0f) {
                bool visible;
                if (!q2m_visible(c, id, &visible, e)) { okay = false; goto out; }
                if (!q2m_alive(c)) goto out;
                if (visible && q2_actor_live(g, id))
                    candidates[group] = (hint_candidate){.start = id, .order = ++order};
            }
            id = next;
        }
    }
    for (size_t chain = 0; chain < r->hint_count; ++chain) {
        if (!candidates[chain].start.registry) continue;
        qa_stamp_set_begin(&visited);
        for (qa_actor_id id = r->hint_starts[chain]; id.registry;) {
            q2_actor *a = node(g, id);
            if (!a || !qa_stamp_set_mark(&visited, id.slot)) break;
            qa_actor_id next = next_node(g, a);
            int32_t group = a->entity->hint_chain_id;
            qa_body_state body;
            if (group >= 0 && (size_t)group < r->hint_count &&
                qa_world_body_read(g->services.world, id, &body, NULL) &&
                qa_vec_length(qa_vec_sub(enemy.origin, body.origin)) <= 512.0f) {
                bool visible;
                if (!visible_from(g, m->enemy, id, &visible, e)) { okay = false; goto out; }
                if (!q2m_alive(c)) goto out;
                if (visible && q2_actor_live(g, id) &&
                    qa_vec_length(qa_vec_sub(c->body.origin, body.origin)) < 10000000.0f)
                    candidates[group].destination = id;
            }
            id = next;
        }
    }
    hint_candidate chosen = {0};
    for (size_t i = 0; i < r->hint_count; ++i)
        if (candidates[i].destination.registry && candidates[i].order > chosen.order)
            chosen = candidates[i];
    if (chosen.start.registry && q2_actor_live(g, chosen.start) &&
        q2_actor_live(g, chosen.destination)) {
        m->goal_hint = chosen.destination;
        okay = go(c, chosen.start, e);
        if (okay) *accepted = true;
    }
out:
    qa_arena_destroy(&scratch);
    q2_scratch_release(g, &custody);
    return okay;
}

bool q2_hint_run(q2m_context *c, float distance, bool *handled, qa_error *e) {
    *handled = c->monster->hint_path;
    if (!*handled) return true;
    if (!q2m_move_to_goal(c, distance, e)) return false;
    if (!q2m_alive(c)) return true;
    if (!q2_actor_live(c->game, c->monster->enemy)) return q2_hint_stop(c, e);
    bool visible;
    if (!q2m_visible(c, c->monster->enemy, &visible, e)) return false;
    if (!q2m_alive(c)) return true;
    if (visible) return q2_hint_stop(c, e);
    return !c->game->options.cooperative || q2m_find_target(c, &(bool){false}, e);
}

bool q2_hint_touch(qa_q2_game *g, q2_actor *hint, qa_actor_id other, qa_error *e) {
    qa_actor_id hint_id = hint->id;
    q2_actor *a = q2_actor_get(g, other, false, NULL);
    if (!a || !a->monster || a->projectile.kind != Q2_PROJECTILE_NONE ||
        !qa_actor_id_equal(a->monster->move_target, hint_id)) return true;
    q2m_context c = {.game = g, .actor = a, .monster = a->monster};
    if (!q2m_refresh(&c, e)) return false;
    hint = node(g, hint_id);
    if (!q2m_alive(&c) || !hint) return true;
    qa_actor_id goal = c.monster->goal_hint;
    if (qa_actor_id_equal(goal, hint_id)) return q2_hint_stop(&c, e);
    int32_t chain = hint->entity->hint_chain_id;
    if (chain < 0 || (size_t)chain >= g->entity_runtime->hint_count) return q2_hint_stop(&c, e);
    qa_actor_id next = {0};
    bool goal_found = false;
    for (qa_actor_id id = g->entity_runtime->hint_starts[chain]; id.registry;) {
        q2_actor *current = node(g, id);
        if (!current) break;
        qa_actor_id following = next_node(g, current);
        if (qa_actor_id_equal(id, hint_id)) { next = following; break; }
        if (qa_actor_id_equal(id, goal)) goal_found = true;
        if (goal_found && qa_actor_id_equal(following, hint_id)) { next = id; break; }
        id = following;
    }
    if (!next.registry || !q2_actor_live(g, next)) return q2_hint_stop(&c, e);
    float wait = hint->entity->wait;
    if (!go(&c, next, e)) return false;
    if (q2m_alive(&c) && wait != 0.0f) c.monster->next_frame_ns = q2m_after(g->now_ns, wait);
    return true;
}
