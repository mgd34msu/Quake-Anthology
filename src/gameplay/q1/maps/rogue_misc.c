#include "internal.h"

static q1_actor *misc_actor(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_rogue_misc(e->map->kind) ? e : NULL;
}
bool q1_map_radius_only(const qa_q1_game *g, qa_actor_id id) {
    const q1_actor *e = q1_entity_const(g, id);
    return e && e->native && e->map && e->map->kind == Q1_MAP_ROGUE_EXPLOSION_TRIGGER;
}
bool q1_map_rogue_misc_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    switch (e->map->kind) {
    case Q1_MAP_ROGUE_RUBBLE_SOURCE:
        if (!q1_map_text(g, e->target))
            return q1_map_fail(error, "rubble_generator has no target");
        e->delay = e->delay ? e->delay : 5;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        e->map->use_enabled = true;
        return !(e->spawnflags & 2) || q1_map_rogue_misc_use(g, e, error);
    case Q1_MAP_ROGUE_EXPLOSION_TRIGGER: {
        if (!q1_map_trigger_init(g, e, false, error))
            return false;
        e = misc_actor(g, id);
        if (!e)
            return true;
        e->max_health = q1_health(g, id);
        if (!e->max_health)
            e->max_health = 20;
        float health = e->max_health;
        if (!qa_combat_set_health(g->services.combat, id, health, error))
            return false;
        e = misc_actor(g, id);
        if (!e)
            return true;
        e->physics.solid = QA_PHYSICS_BOX;
        if (!q1_map_damageable(g, e, true, error))
            return false;
        e = misc_actor(g, id);
        return !e || q1_link(g, e, error);
    }
    case Q1_MAP_ROGUE_LAMP:
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        e->physics.motion = QA_PHYSICS_STATIONARY;
        if (!q1_model(g, e, q1_classnamed(g, id, "light_lantern") ? "progs/lantern.mdl"
                                                                : "progs/candle.mdl", error))
            return false;
        e = misc_actor(g, id);
        return !e || q1_map_make_static(g, e, error);
    default:
        return q1_map_fail(error, "invalid Rogue miscellaneous spawn");
    }
}
bool q1_map_rogue_misc_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->map->kind != Q1_MAP_ROGUE_RUBBLE_SOURCE)
        return true;
    if (e->wait == 0) {
        e->wait = 1;
        return q1_map_schedule(g, e, e->delay, Q1_MAP_ROGUE_RUBBLE_THROW, error);
    }
    e->wait = 0;
    q1_map_cancel(g, e);
    return true;
}
bool q1_map_rogue_rubble_throw(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id source = e->id, target = g->maps->world_actor;
    qa_actor_id found;
    if (qa_targets_first(g->maps->options.targets, e->target, &found))
        target = found;
    e = misc_actor(g, source);
    if (!e)
        return true;
    if (!q1_alive(g, target))
        return q1_map_fail(error, "Rubble generator requires worldspawn");
    qa_body_state destination, body;
    if (!qa_world_body_read(g->services.world, target, &destination, error) ||
        !qa_world_body_read(g->services.world, source, &body, error))
        return false;
    e = misc_actor(g, source);
    if (!e)
        return true;
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(destination.origin, body.origin));
    q1_actor *piece;
    if (!q1_create(g, "rubble", Q1_MAP, source, &piece, error))
        return false;
    qa_actor_id id = piece->id;
    e = misc_actor(g, source);
    if (!e)
        goto retire;
    if (!q1_map_allocate(g, piece, error))
        goto fail;
    piece->map->kind = Q1_MAP_ROGUE_RUBBLE;
    piece->map->touch_enabled = true;
    piece->physics.solid = QA_PHYSICS_BOX;
    piece->physics.motion = QA_PHYSICS_BOUNCE;
    piece->skin = (e->spawnflags & 1) != 0;
    if (!q1_model(g, piece, "progs/rubble.mdl", error))
        goto fail;
    piece = misc_actor(g, id);
    if (!piece)
        goto retire;
    qa_body_state current;
    if (!qa_world_body_read(g->services.world, source, &current, error))
        goto fail;
    if (!misc_actor(g, source) || !misc_actor(g, id))
        goto retire;
    body.origin = current.origin;
    body.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
    body.angles = qa_v3(0, 0, 0);
    body.velocity.x = (float)(((double)direction.x + (double)q1_random(g) * .2 - .1) * 300);
    body.velocity.y = (float)(((double)direction.y + (double)q1_random(g) * .2 - .1) * 300);
    body.velocity.z = (float)(((double)direction.z + (double)q1_random(g) * .2 - .1) * 300);
    body.ground = (qa_actor_id){0};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        goto fail;
    piece = misc_actor(g, id);
    if (!piece || !misc_actor(g, source))
        goto retire;
    if (!q1_map_schedule(g, piece, 30, Q1_MAP_REMOVE, error) || !q1_link(g, piece, error))
        goto fail;
    e = misc_actor(g, source);
    if (!e)
        goto retire;
    return q1_map_schedule(g, e, e->delay, Q1_MAP_ROGUE_RUBBLE_THROW, error);
fail:
    if (qa_actors_get(qa_session_actors(g->services.session), id))
        (void)qa_session_release(g->services.session, id, NULL);
    return false;
retire:
    return !qa_actors_get(qa_session_actors(g->services.session), id) ||
           qa_session_release(g->services.session, id, error);
}
bool q1_map_rogue_misc_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (e->map->kind != Q1_MAP_ROGUE_RUBBLE)
        return true;
    qa_actor_id id = e->id;
    qa_builtin_actor_traits traits = {0};
    if (!g->services.actor_traits ||
        !g->services.actor_traits(g->services.context, other, &traits) ||
        (!traits.player && !traits.monster) || !misc_actor(g, id) || !q1_alive(g, other))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    return !misc_actor(g, id) || !q1_alive(g, other) || qa_vec_length(body.velocity) == 0 ||
           q1_damage(g, other, id, id, 10, QA_Q1_WEAPON_COUNT, error);
}
bool q1_map_rogue_misc_reaction(qa_q1_game *g, q1_actor *e,
                                const qa_damage_outcome *outcome, qa_error *error) {
    if (e->map->kind != Q1_MAP_ROGUE_EXPLOSION_TRIGGER ||
        outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    qa_actor_id id = e->id;
    if (!q1_map_targets(g, e, outcome->request.attack.attacker, error))
        return false;
    e = misc_actor(g, id);
    if (!e)
        return true;
    e->map->touch_enabled = false;
    return q1_map_schedule(g, e, .1, Q1_MAP_REMOVE, error);
}
