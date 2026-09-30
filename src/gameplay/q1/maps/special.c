#include "internal.h"
#include <stdio.h>

bool q1_map_make_static(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    qa_q1_static_model model = {.model = entity->model,
                                .origin = body.origin,
                                .angles = body.angles,
                                .frame = entity->frame,
                                .skin = entity->skin,
                                .color_map = entity->map->color_map};
    if (!g->maps->options.static_model(g->maps->options.context, &model, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || q1_remove(g, entity, error);
}
static bool ambient(qa_q1_game *g, q1_actor *entity, qa_vec3 origin, qa_error *error) {
    bool addon = g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
                 g->options.program == QA_Q1_MG3;
    qa_actor_id id = entity->id;
    if (addon && q1_classnamed(g, id, "ambient_generic")) {
        qa_string_id sound = entity->map->noise[0];
        if (!sound)
            return q1_remove(g, entity, error);
        float volume = entity->map->volume ? entity->map->volume : .5f;
        float attenuation = entity->delay ? entity->delay : 3;
        if (!g->maps->options.ambient(g->maps->options.context, origin, sound, volume,
                                    attenuation, error))
            return false;
        entity = q1_entity(g, id);
        return !entity || q1_map_make_static(g, entity, error);
    }
    static const struct {
        const char *classname, *sound;
        float volume;
    } sounds[] = {{"ambient_suck_wind", "ambience/suck1.wav", 1},
                  {"ambient_flouro_buzz", "ambience/buzz1.wav", 1},
                  {"ambient_drip", "ambience/drip1.wav", .5f},
                  {"ambient_thunder", "ambience/thunder1.wav", .5f},
                  {"ambient_light_buzz", "ambience/fl_hum1.wav", .5f},
                  {"ambient_swamp1", "ambience/swamp1.wav", .5f},
                  {"ambient_swamp2", "ambience/swamp2.wav", .5f},
                  {"ambient_drone", "ambience/drone6.wav", .5f},
                  {"ambient_comp_hum", "ambience/comp1.wav", 1}};
    for (size_t i = 0; i < sizeof(sounds) / sizeof(*sounds); ++i)
        if (q1_classnamed(g, id, sounds[i].classname)) {
            if (!q1_map_ambient(g, origin, sounds[i].sound, sounds[i].volume, error))
                return false;
            entity = q1_entity(g, id);
            return !addon || !entity || q1_map_make_static(g, entity, error);
        }
    return q1_map_fail(error, "unknown Q1 ambient source");
}
static bool corpse(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (g->options.program == QA_Q1_MG3 &&
        ((g->options.coop ? entity->spawnflags & 131072u : entity->spawnflags & 32768u) ||
         (entity->spawnflags &
          (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags)))))
        return q1_remove(g, entity, error);
    static const struct {
        const char *model;
        int32_t frame;
    } poses[] = {
        {"demon", 53}, {"dog", 16}, {"dog", 25}, {"enforcer", 54}, {"enforcer", 65},
        {"fish", 38}, {"hknight", 53}, {"hknight", 62}, {"knight", 85}, {"knight", 96},
        {"ogre", 116}, {"ogre", 126}, {"shalrath", 22}, {"shambler", 87},
        {"soldier", 17}, {"soldier", 28}, {"wizard", 53}, {"player", 49},
        {"player", 60}, {"player", 69}, {"player", 84}, {"player", 93}, {"player", 102},
        {"h_demon", 0}, {"h_dog", 0}, {"h_guard", 0}, {"h_hellkn", 0}, {"h_knight", 0},
        {"h_mega", 0}, {"h_ogre", 0}, {"h_player", 0}, {"h_shal", 0}, {"h_shams", 0},
        {"h_wizard", 0}, {"h_zombie", 0}, {"gib1", 0}, {"gib2", 0}, {"gib3", 0}};
    int32_t style = entity->map->style;
    if (style < 0 || (size_t)style >= sizeof(poses) / sizeof(*poses))
        return q1_map_fail(error, "Q1 misc_corpse has invalid style");
    entity->frame = poses[style].frame;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    char model[48];
    snprintf(model, sizeof(model), "progs/%s.mdl", poses[style].model);
    qa_actor_id id = entity->id;
    if (!q1_model(g, entity, model, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || q1_link(g, entity, error);
}
bool q1_map_special_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    q1_map_state *state = entity->map;
    switch (state->kind) {
    case Q1_MAP_GATE: {
        uint32_t flags = *g->maps->options.server_flags;
        bool hidden = q1_classnamed(g, entity->id, "func_episodegate")
                          ? !(flags & entity->spawnflags)
                          : (flags & 15) == 15;
        if (hidden) {
            entity->model = QA_STRING_NONE;
            return true;
        }
        if (!state->has_inline_model)
            return q1_map_fail(error, "Q1 campaign gate has no brush model");
        entity->physics.solid = QA_PHYSICS_BRUSH;
        entity->physics.motion = QA_PHYSICS_PUSH;
        state->use_enabled = true;
        body.angles = qa_v3(0, 0, 0);
        break;
    }
    case Q1_MAP_STATIC:
        if (q1_classnamed(g, id, "misc_corpse"))
            return corpse(g, entity, error);
        if (q1_classnamed(g, entity->id, "func_illusionary")) {
            body.angles = qa_v3(0, 0, 0);
            if (!qa_world_body_write(g->services.world, entity->id, &body, error))
                return false;
        } else if (q1_classnamed(g, entity->id, "light_globe")) {
            if (!q1_model(g, entity, "progs/s_light.spr", error))
                return false;
        } else {
            const char *model = q1_classnamed(g, entity->id, "light_torch_small_walltorch")
                                    ? "progs/flame.mdl"
                                    : "progs/flame2.mdl";
            entity->frame = q1_classnamed(g, entity->id, "light_flame_large_yellow") ? 1 : 0;
            if (!q1_model(g, entity, model, error) ||
                !q1_map_ambient(g, body.origin, "ambience/fire1.wav", .5f, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        return q1_map_make_static(g, entity, error);
    case Q1_MAP_AMBIENT:
        return ambient(g, entity, body.origin, error);
    case Q1_MAP_SIGIL: {
        uint32_t bits = entity->spawnflags & 15;
        if (!bits)
            return q1_map_fail(error, "Q1 sigil has no episode flag");
        unsigned number = bits & 8 ? 4 : bits & 4 ? 3 : bits & 2 ? 2 : 1;
        char model[32];
        snprintf(model, sizeof(model), "progs/end%u.mdl", number);
        if (!q1_model(g, entity, model, error))
            return false;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        entity->physics.motion = QA_PHYSICS_TOSS;
        state->touch_enabled = true;
        body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
        if (!q1_map_schedule(g, entity, .2, Q1_MAP_SIGIL_PLACE, error))
            return false;
        break;
    }
    case Q1_MAP_SHOOTER:
        state->movedir = q1_map_direction(body.angles);
        state->use_enabled = true;
        body.angles = qa_v3(0, 0, 0);
        if (!q1_classnamed(g, entity->id, "trap_spikeshooter")) {
            entity->wait = entity->wait ? entity->wait : 1;
            if (!q1_map_schedule(g, entity, state->initial_think + entity->wait,
                                 Q1_MAP_SHOOTER_FIRE, error))
                return false;
        }
        break;
    case Q1_MAP_FIREBALL_SOURCE:
        entity->speed = entity->speed ? entity->speed : 1000;
        return qa_builtin_resource(&g->services, "fireball", &entity->classname, error) &&
               q1_map_schedule(g, entity, q1_random(g) * 5, Q1_MAP_FIREBALL_FLY, error);
    case Q1_MAP_BUBBLES:
        return g->options.deathmatch ? q1_remove(g, entity, error)
                                     : q1_map_schedule(g, entity, 1, Q1_MAP_BUBBLES_MAKE, error);
    case Q1_MAP_VIEW:
        return q1_model(g, entity, "progs/player.mdl", error);
    case Q1_MAP_NOISE:
        return q1_map_schedule(g, entity, .1 + q1_random(g), Q1_MAP_NOISE_REPEAT, error);
    case Q1_MAP_LIGHTNING:
        state->use_enabled = true;
        return true;
    default:
        return q1_map_fail(error, "unknown Q1 special map actor");
    }
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error);
}
static bool shooter_fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    bool laser = (entity->spawnflags & 2) != 0;
    if (!q1_sound(g, entity->id, laser ? "enforcer/enfire.wav" : "weapons/spike2.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float speed = laser && !q1_classnamed(g, entity->id, "trap_shooter") ? 600 : 500;
    qa_vec3 direction = laser ? qa_vec_normalize(entity->map->movedir) : entity->map->movedir;
    q1_actor *missile;
    q1_projectile_kind kind = laser                    ? Q1_ENFORCER_LASER
                              : entity->spawnflags & 1 ? Q1_SUPERSPIKE
                                                       : Q1_SPIKE;
    if (!q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, kind, body.origin,
                             qa_vec_scale(direction, speed), &missile, error))
        return false;
    return kind != Q1_SUPERSPIKE || q1_model(g, missile, "progs/spike.mdl", error);
}
bool q1_map_special_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    if (entity->map->kind == Q1_MAP_LIGHTNING)
        return q1_map_lightning_use(g, entity, activator, error);
    if (entity->map->kind == Q1_MAP_GATE) {
        entity->frame = 1 - entity->frame;
        return true;
    }
    return entity->map->kind != Q1_MAP_SHOOTER || shooter_fire(g, entity, error);
}
bool q1_map_special_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (entity->map->kind == Q1_MAP_SIGIL &&
        (g->options.program == QA_Q1_DOPA || g->options.program == QA_Q1_MG1 ||
         g->options.program == QA_Q1_MG3))
        return q1_map_addon_sigil_touch(g, entity, other, error);
    if (entity->map->kind == Q1_MAP_FIREBALL) {
        if (!q1_damage(g, other, entity->id, entity->id, 20, QA_Q1_WEAPON_COUNT, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    if (entity->map->kind != Q1_MAP_SIGIL || entity->physics.solid != QA_PHYSICS_TRIGGER ||
        !q1_map_player(g, other) || q1_health(g, other) <= 0)
        return true;
    const char *message =
        g->options.edition == QA_Q1_CLASSIC ? "You got the rune!" : "$qc_got_rune";
    if (!q1_message(g, other, message, error))
        return false;
    if (!q1_alive(g, entity->id) || !q1_alive(g, other))
        return true;
    if (!q1_sound(g, other, "misc/runekey.wav", 3, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
        !q1_effect(g, QA_BUILTIN_ITEM, other, body.origin, 1, 0, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->model = QA_STRING_NONE;
    entity->map->touch_enabled = false;
    if (!q1_link(g, entity, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    *g->maps->options.server_flags |= entity->spawnflags & 15;
    entity->classname = QA_STRING_NONE;
    return q1_map_targets(g, entity, other, error);
}
static bool fireball_fly(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float x = q1_random(g) * 100 - 50;
    float y = q1_random(g) * 100 - 50;
    float z = entity->speed + q1_random(g) * 200;
    q1_actor *ball;
    if (!q1_create(g, "fireball", Q1_MAP, entity->id, &ball, error))
        return false;
    if (!q1_map_allocate(g, ball, error)) {
        (void)q1_remove(g, ball, NULL);
        return false;
    }
    ball->map->kind = Q1_MAP_FIREBALL;
    ball->map->touch_enabled = true;
    ball->physics.solid = QA_PHYSICS_TRIGGER;
    ball->physics.motion = QA_PHYSICS_TOSS;
    qa_vec3 velocity = qa_v3(x, y, z);
    qa_body_state missile = {.origin = body.origin,
                             .velocity = velocity,
                             .angles = {atan2f(z, hypotf(x, y)) * 57.29577951308232f,
                                        qa_builtin_angle_mod(atan2f(y, x) * 57.29577951308232f),
                                        0}};
    if (!q1_model(g, ball, "progs/lavaball.mdl", error) ||
        !qa_world_body_write(g->services.world, ball->id, &missile, error) ||
        !q1_map_schedule(g, ball, 5, Q1_MAP_REMOVE, error) || !q1_link(g, ball, error))
        return false;
    return !q1_alive(g, entity->id) ||
           q1_map_schedule(g, entity, q1_random(g) * 5 + 3, Q1_MAP_FIREBALL_FLY, error);
}
bool q1_map_special_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    switch (action) {
    case Q1_MAP_SIGIL_PLACE: {
        qa_actor_id id = entity->id;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        qa_vec3 start = qa_vec_add(body.origin, qa_v3(0, 0, 6));
        qa_trace_query query = {.start = start,
                                .end = qa_vec_add(start, qa_v3(0, 0, -256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = entity->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        if (trace.all_solid || trace.fraction == 1)
            return q1_remove(g, entity, error);
        body.origin = trace.end;
        body.velocity = qa_v3(0, 0, 0);
        body.ground = trace.actor;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        return !entity || !entity->map || q1_link(g, entity, error);
    }
    case Q1_MAP_SHOOTER_FIRE:
        return shooter_fire(g, entity, error) &&
               (!q1_alive(g, entity->id) ||
                q1_map_schedule(g, entity, entity->wait, Q1_MAP_SHOOTER_FIRE, error));
    case Q1_MAP_FIREBALL_FLY:
        return fireball_fly(g, entity, error);
    case Q1_MAP_BUBBLES_MAKE: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !q1_spawn_bubble(g, body.origin, qa_v3(0, 0, 15), false, error))
            return false;
        return !q1_alive(g, entity->id) ||
               q1_map_schedule(g, entity, q1_random(g) + .5, Q1_MAP_BUBBLES_MAKE, error);
    }
    case Q1_MAP_NOISE_REPEAT: {
        static const char *const sounds[] = {"enfire", "enfstop", "sight1", "sight2",
                                             "sight3", "sight4",  "pain1"};
        for (size_t i = 0; i < sizeof(sounds) / sizeof(*sounds); ++i) {
            char path[40];
            snprintf(path, sizeof(path), "enforcer/%s.wav", sounds[i]);
            if (!q1_sound(g, entity->id, path, (int32_t)i + 1, 1, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        return q1_map_schedule(g, entity, .5, Q1_MAP_NOISE_REPEAT, error);
    }
    default:
        return q1_map_fail(error, "unknown Q1 special continuation");
    }
}
