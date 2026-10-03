#include "internal.h"

typedef struct swing_bounds {
    int16_t near, far, bottom, top;
} swing_bounds;
static const swing_bounds bounds_by_frame[] = {
    {-176, -120, 48, 128}, {-172, -112, 12, 88}, {-160, -96, -22, 50}, {-138, -70, -51, 17},
    {-110, -38, -72, -8},  {-76, 0, -83, -23},   {-40, 40, -88, -32},  {0, 76, -83, -23},
    {38, 100, -72, -8},    {70, 138, -51, 17},   {96, 160, -22, 50},   {112, 172, 12, 88},
    {120, 176, 48, 128}};
static q1_actor *pendulum(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && entity->map->kind == Q1_MAP_PENDULUM ? entity : NULL;
}
bool q1_map_pendulum_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!entity->spawnflags)
        entity->spawnflags = 2;
    if (!(entity->spawnflags & 3))
        return q1_map_fail(error, "Unimplemented Pendulum Type (pendulum.qc)");
    if (!q1_model(g, entity, "progs/pendulum.mdl", error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = pendulum(g, id);
    if (!entity)
        return true;
    bool along_y = (entity->spawnflags & 2) != 0;
    body.angles = qa_v3(0, along_y ? 0 : 270, 0);
    body.bounds = along_y ? (qa_bounds){{-8, -24, -100}, {8, 24, 100}}
                          : (qa_bounds){{-24, -8, -100}, {24, 8, 100}};
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = pendulum(g, id);
    if (!entity)
        return true;
    entity->map->current_ammo = entity->map->current_ammo != 0 ? entity->map->current_ammo : 5;
    entity->delay = entity->delay != 0 ? entity->delay : 1;
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    if (!q1_map_damageable(g, entity, false, error))
        return false;
    entity = pendulum(g, id);
    if (!entity)
        return true;
    entity->map->touch_enabled = true;
    if (q1_alive(g, g->maps->world_actor))
        g->maps->pendulum_impact = 0;
    entity->map->use_enabled = (entity->spawnflags & 8) != 0;
    if (!entity->map->use_enabled &&
        !q1_map_schedule(g, entity, entity->delay, Q1_MAP_PENDULUM_SWING, error))
        return false;
    return q1_link(g, entity, error);
}
bool q1_map_pendulum_use(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    entity->map->pending.pendulum_step = 0;
    return q1_map_schedule(g, entity, entity->delay, Q1_MAP_PENDULUM_SWING, error);
}
bool q1_map_pendulum_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    unsigned step = entity->map->pending.pendulum_step;
    if (step >= 26)
        return q1_map_fail(error, "invalid Rogue pendulum continuation");
    unsigned frame = step < 13 ? step : 25 - step;
    entity->frame = (int32_t)frame;
    if (step != 13) {
        swing_bounds bounds = bounds_by_frame[frame];
        if (step == 21)
            bounds.far = -28;
        if (step == 22)
            bounds.near = -172;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = pendulum(g, id);
        if (!entity)
            return true;
        body.bounds =
            entity->spawnflags & 2
                ? (qa_bounds){{-8, bounds.near, bounds.bottom}, {8, bounds.far, bounds.top}}
                : (qa_bounds){{bounds.near, -8, bounds.bottom}, {bounds.far, 8, bounds.top}};
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = pendulum(g, id);
        if (!entity || !q1_link(g, entity, error))
            return entity == NULL;
    }
    entity = pendulum(g, id);
    if (!entity)
        return true;
    if ((step == 0 || step == 14) && q1_alive(g, g->maps->world_actor))
        g->maps->pendulum_impact = step == 0 ? 1 : -1;
    if (step == 3 || step == 16) {
        qa_string_id sound;
        if (!qa_builtin_resource(&g->services, "pendulum/swing.wav", &sound, error) ||
            !q1_sound_resource(g, id, sound, 0, 1, .5f, error))
            return false;
    }
    entity = pendulum(g, id);
    if (!entity)
        return true;
    entity->map->pending.pendulum_step = (uint8_t)((step + 1) % 26);
    unsigned distance = frame < 6 ? 6 - frame : frame - 6;
    return q1_map_schedule(g, entity, .05 + .02 * distance, Q1_MAP_PENDULUM_SWING, error);
}
bool q1_map_pendulum_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id id = entity->id;
    if (q1_health(g, other) < 1 || !q1_damageable(g, other))
        return true;
    entity = pendulum(g, id);
    if (!entity)
        return true;
    if (entity->map->cooldown < g->time) {
        if (!q1_sound(g, id, "pendulum/hit.wav", 2, 1, error))
            return false;
        entity = pendulum(g, id);
        if (!entity)
            return true;
        entity->map->cooldown = g->time + 1;
    }
    if (!q1_damage(g, other, id, id, entity->map->current_ammo, QA_Q1_WEAPON_COUNT, error))
        return false;
    qa_body_state body;
    qa_error read_error = {0};
    if (!qa_world_body_read(g->services.world, other, &body, &read_error)) {
        if (read_error.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = read_error;
        return false;
    }
    entity = pendulum(g, id);
    if (!entity || !q1_alive(g, other))
        return true;
    float impact = q1_alive(g, g->maps->world_actor) ? g->maps->pendulum_impact : 0;
    if (entity->spawnflags & 2)
        body.velocity.y = impact * -250;
    else
        body.velocity.x = impact * 250;
    body.velocity.z = 200;
    if (!qa_world_body_write(g->services.world, other, &body, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = other,
                              .time_ns = g->time_ns,
                              .origin = body.origin,
                              .value = 1,
                              .count = 1};
    return qa_builtin_resource(&g->services, "meat-spray", &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
