#include "internal.h"
#include <float.h>

static q1_actor *sacrifice(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && entity->map->kind == Q1_MAP_SACRIFICE ? entity : NULL;
}

static bool sacrifice_float(double value, float *out, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127)
        return q1_map_fail(error, "Q1 Sacrifice value exceeds finite float range");
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}

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
    qa_actor_id id = entity->id;
    entity->map->use_enabled = true;
    if (!qa_combat_set_health(g->services.combat, id, 100, error))
        return false;
    entity = sacrifice(g, id);
    if (!entity) return true;
    entity->max_health = 100;
    entity->physics.solid = QA_PHYSICS_BOX;
    bool damageable = !(entity->spawnflags & 1);
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, id, &traits, error)) return false;
    if (!sacrifice(g, id)) return true;
    traits.can_take_damage = damageable;
    if (!qa_combat_set_traits(g->services.combat, id, &traits, error)) return false;
    if (!sacrifice(g, id)) return true;
    if (!qa_combat_read_traits(g->services.combat, id, &traits, error)) return false;
    entity = sacrifice(g, id);
    if (!entity) return true;
    entity->aimed_damage = traits.can_take_damage;
    entity->physics.motion = QA_PHYSICS_STEP;
    bool floating = (entity->spawnflags & 2) != 0;
    if (floating) {
        if (!q1_model(g, entity, "progs/player_hanging.mdl", error)) return false;
        entity->count = 0;
        entity->physics.angular_velocity = qa_v3(0, 36, 0);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        entity = sacrifice(g, id);
        if (!entity) return true;
        entity->map->pending.mover.destination = body.origin;
    } else {
        entity->count = (float)(5 + floor((double)q1_random(g) * 65 + .5));
        if (!q1_model(g, entity, "progs/player_hanging_animated.mdl", error)) return false;
        entity->frame = (int32_t)entity->count;
    }
    if (!q1_map_schedule(g, entity, .1,
                          floating ? Q1_MAP_SACRIFICE_FLOAT : Q1_MAP_SACRIFICE_ANIMATE,
                          error)) return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
    if (!sacrifice(g, id)) return true;
    body.bounds = (qa_bounds){{-16, -16, -56}, {16, 16, 0}};
    if (!qa_world_body_write(g->services.world, id, &body, error)) return false;
    entity = sacrifice(g, id);
    return !entity || q1_link(g, entity, error);
}

bool q1_map_sacrifice_gib(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!sacrifice(g, id)) return true;
    qa_vec3 origin = body.origin;
    origin.x += 0;
    origin.y += 0;
    if (!sacrifice_float((double)origin.z - 32, &origin.z, error)) return false;
    if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
    if (!sacrifice(g, id)) return true;
    body.origin = origin;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    if (!sacrifice(g, id)) return true;
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < sizeof(models) / sizeof(*models); ++i) {
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!sacrifice(g, id)) return true;
        if (!q1_gib_at(g, id, body.origin, -10, models[i], error))
            return false;
        if (!sacrifice(g, id))
            return true;
    }
    if (!q1_sound(g, id, q1_random(g) < .5f ? "player/gib.wav" : "player/udeath.wav", 2, 0,
                  error))
        return false;
    entity = sacrifice(g, id);
    if (!entity) return true;
    if (!q1_map_targets(g, entity, entity->activator, error)) return false;
    entity = sacrifice(g, id);
    return !entity || q1_remove(g, entity, error);
}

bool q1_map_sacrifice_think(qa_q1_game *g, q1_actor *entity, q1_map_action action,
                            qa_error *error) {
    qa_actor_id id = entity->id;
    if (action == Q1_MAP_SACRIFICE_ANIMATE) {
        if (++entity->count > 75)
            entity->count = 5;
        entity->frame = (int32_t)entity->count;
    } else {
        if (!sacrifice_float((double)entity->count + .1, &entity->count, error))
            return false;
        qa_vec3 origin = entity->map->pending.mover.destination;
        origin.x += 0;
        origin.y += 0;
        if (!sacrifice_float((double)origin.z + cos((double)entity->count * 90) * 16,
                              &origin.z, error))
            return false;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!sacrifice(g, id)) return true;
        body.origin = origin;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = sacrifice(g, id);
        if (!entity) return true;
        if (!q1_link(g, entity, error)) return false;
        if (!sacrifice(g, id)) return true;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!sacrifice(g, id)) return true;
        qa_vec3 angles = body.angles;
        angles.x += 0;
        angles.z += 0;
        if (!sacrifice_float((double)angles.y + 3.6, &angles.y, error)) return false;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!sacrifice(g, id)) return true;
        body.angles = angles;
        if (!qa_world_body_write(g->services.world, id, &body, error)) return false;
    }
    entity = sacrifice(g, id);
    return !entity || q1_map_schedule(g, entity, .1, action, error);
}
