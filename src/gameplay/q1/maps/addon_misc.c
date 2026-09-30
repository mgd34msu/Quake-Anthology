#include "internal.h"

bool qa_q1_game_map_new_game_travel(const qa_q1_game *g) {
    return g && g->maps && g->maps->final_new_game_travel;
}

bool qa_q1_game_map_finish_addon(qa_q1_game *g, qa_q1_map_ending ending, qa_error *error) {
    if (!g || !g->maps || (ending != QA_Q1_MAP_END_DOPA && ending != QA_Q1_MAP_END_MG3))
        return q1_map_fail(error, "invalid Q1 addon ending");
    q1_actor *world = q1_entity(g, g->maps->world_actor);
    if (!world || !world->map)
        return q1_map_fail(error, "Q1 addon ending requires its authored world");
    const char *map = "start";
    const char *text =
        ending == QA_Q1_MAP_END_DOPA ? "$map_dopa_endtext_final" : "$mg3_qc_boss_finale";
    uint32_t flags = *g->maps->options.server_flags;
    if (ending == QA_Q1_MAP_END_MG3 && (flags & QA_Q1_BLOODY_NIGHTMARE_ACTIVE)) {
        if (flags & QA_Q1_BLOODY_NIGHTMARE_NEWGAME)
            map = "boss2";
        else {
            map = "map1";
            *g->maps->options.server_flags = QA_Q1_BLOODY_NIGHTMARE_ACTIVE |
                                             QA_Q1_BLOODY_NIGHTMARE_DISCOVERED |
                                             QA_Q1_BLOODY_NIGHTMARE_NEWGAME;
            g->maps->final_new_game_travel = true;
        }
    }
    qa_string_id next;
    return qa_builtin_resource(&g->services, text, &world->map->intermissiontext, error) &&
           qa_builtin_resource(&g->services, map, &next, error) &&
           qa_q1_level_begin(g->maps->options.level, next, (qa_actor_id){0}, g->time, error);
}

bool q1_map_sacrifice_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    entity->max_health = 100;
    entity->aimed_damage = !(entity->spawnflags & 1);
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->map->use_enabled = true;
    if (!qa_combat_set_health(g->services.combat, entity->id, 100, error) ||
        !q1_map_damageable(g, entity, entity->aimed_damage, error))
        return false;
    bool floating = (entity->spawnflags & 2) != 0;
    if (floating) {
        entity->count = 0;
        entity->physics.angular_velocity = qa_v3(0, 36, 0);
        entity->map->pending.mover.destination = body.origin;
    } else {
        entity->count = (float)(5 + floor((double)q1_random(g) * 65 + .5));
        entity->frame = (int32_t)entity->count;
    }
    body.bounds = (qa_bounds){{-16, -16, -56}, {16, 16, 0}};
    return q1_model(g, entity,
                    floating ? "progs/player_hanging.mdl" : "progs/player_hanging_animated.mdl",
                    error) &&
           qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_map_schedule(g, entity, .1,
                           floating ? Q1_MAP_SACRIFICE_FLOAT : Q1_MAP_SACRIFICE_ANIMATE, error) &&
           q1_link(g, entity, error);
}

bool q1_map_sacrifice_gib(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    body.origin.z -= 32;
    if (!qa_world_body_write(g->services.world, entity->id, &body, error))
        return false;
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < sizeof(models) / sizeof(*models); ++i) {
        if (!q1_gib_at(g, entity->id, body.origin, -10, models[i], error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    if (!q1_sound(g, entity->id, q1_random(g) < .5f ? "player/gib.wav" : "player/udeath.wav", 2, 0,
                  error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    return q1_map_targets(g, entity, entity->activator, error) &&
           (!q1_alive(g, entity->id) || q1_remove(g, entity, error));
}

bool q1_map_sacrifice_think(qa_q1_game *g, q1_actor *entity, q1_map_action action,
                            qa_error *error) {
    if (action == Q1_MAP_SACRIFICE_ANIMATE) {
        if (++entity->count > 75)
            entity->count = 5;
        entity->frame = (int32_t)entity->count;
    } else {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        entity->count += .1f;
        body.origin = entity->map->pending.mover.destination;
        body.origin.z += cosf(entity->count * 90) * 16;
        body.angles.y += 3.6f;
        if (!qa_world_body_write(g->services.world, entity->id, &body, error) ||
            !q1_link(g, entity, error))
            return false;
    }
    return !q1_alive(g, entity->id) || q1_map_schedule(g, entity, .1, action, error);
}
