#include "internal.h"
#include <stdio.h>

bool qa_q1_game_map_coordinates_enabled(const qa_q1_game *g) {
    return g && !g->destroy_pending && g->options.program == QA_Q1_HIPNOTIC && g->maps &&
           g->maps->dump_coordinates && q1_alive((qa_q1_game *)g, g->maps->world_actor);
}

bool qa_q1_game_map_coordinate_dump(qa_q1_game *g, qa_actor_id actor,
                                     double attack_finished, qa_error *error) {
    if (!g || !isfinite(attack_finished))
        return q1_map_fail(error, "invalid Hipnotic coordinate dump continuation");
    if (!qa_q1_game_map_coordinates_enabled(g) || !q1_alive(g, actor) ||
        g->time < attack_finished)
        return true;
    if (!g->host.check_client)
        return q1_map_fail(error, "Hipnotic coordinate dump requires client observation owner");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    qa_actor_id client = {0};
    bool ok = true;
    if (g->host.check_client(g->host.context, actor, &client) &&
        q1_alive(g, actor) && q1_alive(g, client)) {
        qa_body_state body;
        ok = qa_world_body_read(g->services.world, client, &body, error);
        if (ok && q1_alive(g, actor) && q1_alive(g, client)) {
            char text[160];
            snprintf(text, sizeof(text), "Player: '%g %g %g'\n", body.origin.x, body.origin.y,
                     body.origin.z);
            ok = q1_message(g, (qa_actor_id){0}, text, error);
        }
    }
    if (ok && !qa_q1_game_operation_live(&operation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                     "Q1 source retired during coordinate dump");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}

static bool sound(qa_q1_game *g, q1_actor *entity, qa_string_id resource, int32_t channel,
                  qa_error *error) {
    return q1_sound_resource(g, entity->id, resource, channel, entity->speed, entity->map->volume,
                             error);
}
static bool play_sound(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_state *state = entity->map;
    qa_string_id resource = state->noise[0];
    if (entity->spawnflags & 1) {
        state->effect_active = !state->effect_active;
        if (!state->effect_active)
            resource = state->noise[1];
    }
    int32_t channel = state->impulse;
    if (channel < 0 || channel > 6)
        channel = 7;
    return sound(g, entity, resource, channel, error);
}
bool q1_map_hip_misc_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_state *state = entity->map;
    switch (state->kind) {
    case Q1_MAP_SOUND: {
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->classname);
        bool thunder = !strncmp(name, "random_thunder", 14);
        bool periodic = !strstr(name, "_triggered");
        state->volume = state->volume ? state->volume : 1;
        entity->speed = entity->speed == 0 ? 1 : entity->speed == -1 ? 0 : entity->speed;
        if ((entity->spawnflags & 1) && !state->impulse)
            state->impulse = 7;
        if (!qa_builtin_resource(&g->services, "misc/null.wav", &state->noise[1], error) ||
            (thunder &&
             !qa_builtin_resource(&g->services, "ambience/thunder1.wav", &state->noise[0], error)))
            return false;
        state->use_enabled = true;
        if (periodic) {
            entity->wait = entity->wait ? entity->wait : 20;
            entity->delay = entity->delay ? entity->delay : 2;
            if (!q1_map_schedule(g, entity, fmax(entity->delay, entity->wait * q1_random(g)),
                                 Q1_MAP_SOUND_REPEAT, error))
                return false;
        }
        if (thunder)
            state->impulse = 6;
        return true;
    }
    case Q1_MAP_HIP_AMBIENT: {
        static const struct {
            const char *name, *path;
        } sounds[] = {{"ambient_humming", "ambient/humming.wav"},
                      {"ambient_rushing", "ambient/rushing.wav"},
                      {"ambient_running_water", "ambient/runwater.wav"},
                      {"ambient_fan_blowing", "ambient/fanblow.wav"},
                      {"ambient_waterfall", "ambient/waterfal.wav"},
                      {"ambient_riftpower", "ambient/riftpowr.wav"}};
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        for (size_t i = 0; i < sizeof(sounds) / sizeof(*sounds); ++i)
            if (q1_classnamed(g, entity->id, sounds[i].name))
                return q1_map_ambient(g, body.origin, sounds[i].path,
                                      state->volume ? state->volume : .5f, error);
        return q1_map_fail(error, "unknown Hipnotic ambient source");
    }
    case Q1_MAP_COMMAND:
        if (!q1_map_text(g, entity->message))
            return true;
        if (!g->maps->options.server_command)
            return q1_map_fail(error, "Q1 authored command has no command owner");
        return g->maps->options.server_command(g->maps->options.context, entity->message, error);
    case Q1_MAP_EXPLODER:
        entity->damage = entity->damage ? fmaxf(0, entity->damage) : 120;
        entity->speed = entity->speed ? entity->speed : 1;
        if (q1_classnamed(g, entity->id, "func_multi_exploder")) {
            entity->model = QA_STRING_NONE;
            entity->physics.motion = QA_PHYSICS_STATIONARY;
            entity->wait = entity->wait ? entity->wait : .25f;
            state->duration = state->duration ? state->duration : 1;
            state->volume = state->volume ? state->volume : .5f;
        } else
            state->volume = state->volume ? state->volume : 1;
        state->use_enabled = true;
        return true;
    case Q1_MAP_RUBBLE_SOURCE:
        state->style = q1_classnamed(g, entity->id, "func_rubble1")   ? 1
                       : q1_classnamed(g, entity->id, "func_rubble2") ? 2
                       : q1_classnamed(g, entity->id, "func_rubble3") ? 3
                                                                      : 0;
        state->use_enabled = true;
        return true;
    case Q1_MAP_EARTHQUAKE:
        entity->damage = entity->damage ? entity->damage : .8f;
        g->maps->quake_active = false;
        state->use_enabled = true;
        return true;
    case Q1_MAP_TELEPORT_EFFECT:
        state->use_enabled = true;
        return true;
    default:
        return q1_map_fail(error, "unknown Hipnotic miscellaneous map actor");
    }
}
bool q1_map_multi_explosion_begin(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!entity->map || entity->map->kind != Q1_MAP_EXPLODER || entity->map->effect_active)
        return true;
    entity->map->effect_active = true;
    entity->state.effect.expires = g->time + entity->map->duration;
    entity->state.effect.volume = entity->map->volume;
    return q1_map_targets(g, entity, entity->activator, error);
}
static bool explode(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (q1_classnamed(g, entity->id, "func_multi_exploder"))
        return q1_multi_explosion_think(g, entity, error);
    if (!q1_map_targets(g, entity, entity->activator, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    qa_string_id resource;
    if (!qa_builtin_resource(&g->services,
                             entity->damage < 120 ? "misc/shortexp.wav" : "misc/longexpl.wav",
                             &resource, error) ||
        !sound(g, entity, resource, 0, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    if (!q1_radius(g, entity->id, entity->owner, entity->damage, entity->id, QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    if ((entity->spawnflags & 1) &&
        !q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id, body.origin, 1, 0, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity)
        return true;
    if (!q1_sprite_prepare(g, entity, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !q1_link(g, entity, error))
        return entity == NULL;
    entity = q1_entity(g, id);
    return !entity || q1_schedule(g, entity, .1, Q1_THINK_SPRITE, error);
}
static bool rubble(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state source;
    if (!qa_world_body_read(g->services.world, entity->id, &source, error))
        return false;
    double count = fmax(1, entity->count);
    int32_t variant = entity->map->style;
    for (size_t i = 0; (double)i < count; ++i) {
        int32_t model = variant ? variant : (int32_t)floorf(1 + 3 * q1_random(g));
        q1_actor *piece;
        if (!q1_create(g, "hip_rubble", Q1_MAP, (qa_actor_id){0}, &piece, error))
            return false;
        if (!q1_map_allocate(g, piece, error)) {
            (void)q1_remove(g, piece, NULL);
            return false;
        }
        piece->map->kind = Q1_MAP_RUBBLE;
        piece->map->touch_enabled = true;
        piece->physics.motion = QA_PHYSICS_BOUNCE;
        piece->physics.solid = QA_PHYSICS_BOX;
        piece->physics.local_time_ns = (int64_t)g->time_ns;
        qa_body_state body = {.origin = source.origin};
        body.velocity.x = 70 * (q1_random(g) * 2 - 1);
        body.velocity.y = 70 * (q1_random(g) * 2 - 1);
        body.velocity.z = 140 + 70 * q1_random(g);
        piece->physics.angular_velocity.x = q1_random(g) * 600;
        piece->physics.angular_velocity.y = q1_random(g) * 600;
        piece->physics.angular_velocity.z = q1_random(g) * 600;
        entity->map->pause_time = (float)g->time;
        if (!q1_model(g, piece,
                      model == 1   ? "progs/rubble1.mdl"
                      : model == 2 ? "progs/rubble3.mdl"
                                   : "progs/rubble2.mdl",
                      error) ||
            !qa_world_body_write(g->services.world, piece->id, &body, error) ||
            !q1_map_schedule(g, piece, 13 + q1_random(g) * 10, Q1_MAP_REMOVE, error) ||
            !q1_link(g, piece, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    return true;
}
bool q1_map_hip_misc_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    switch (entity->map->kind) {
    case Q1_MAP_SOUND:
        return play_sound(g, entity, error);
    case Q1_MAP_EXPLODER: {
        entity->activator = activator;
        if (!entity->delay)
            return explode(g, entity, error);
        float delay = entity->delay;
        entity->delay = 0;
        return q1_map_schedule(g, entity, delay, Q1_MAP_EXPLODER_FIRE, error);
    }
    case Q1_MAP_TELEPORT_EFFECT: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !q1_effect(g, QA_BUILTIN_TELEPORT, entity->id, body.origin, 1, 0, error))
            return false;
        return !q1_alive(g, entity->id) || q1_sound(g, entity->id, "misc/r_tele1.wav", 2, 1, error);
    }
    case Q1_MAP_RUBBLE_SOURCE:
        return rubble(g, entity, error);
    case Q1_MAP_EARTHQUAKE:
        if (q1_alive(g, g->maps->world_actor))
            g->maps->earthquake_end = fmax(g->maps->earthquake_end, g->time + entity->damage);
        return true;
    default:
        return true;
    }
}
bool q1_map_hip_misc_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (entity->map->kind != Q1_MAP_RUBBLE)
        return true;
    double local_time = (double)entity->physics.local_time_ns / 1000000000.0;
    qa_combat_state combat;
    if (local_time < entity->map->cooldown ||
        !qa_combat_read(g->services.combat, other, &combat, NULL) || !combat.can_take_damage)
        return true;
    if (!q1_damage(g, other, entity->id, entity->owner, 10, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_sound(g, entity->id, "zombie/z_hit.wav", 1, 1, error))
        return false;
    if (q1_alive(g, entity->id))
        entity->map->cooldown = local_time + .1;
    return true;
}
bool q1_map_hip_misc_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    if (action == Q1_MAP_EXPLODER_FIRE)
        return explode(g, entity, error);
    if (action != Q1_MAP_SOUND_REPEAT)
        return q1_map_fail(error, "unknown Hipnotic miscellaneous continuation");
    return q1_map_schedule(g, entity, fmax(entity->delay, entity->wait * q1_random(g)),
                           Q1_MAP_SOUND_REPEAT, error) &&
           play_sound(g, entity, error);
}
bool qa_q1_game_map_after_physics(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g || !g->maps || !q1_alive(g, g->maps->world_actor) || !q1_alive(g, actor))
        return true;
    if (g->options.program == QA_Q1_ROGUE)
        return q1_map_rogue_ending(g, actor, error);
    if (g->maps->earthquake_end <= g->time) {
        if (!g->maps->quake_active)
            return true;
        if (!q1_sound(g, actor, "misc/quakeend.wav", 2, 0, error))
            return false;
        g->maps->quake_active = false;
        return true;
    }
    if (!g->maps->quake_active) {
        if (!q1_sound(g, actor, "misc/quake.wav", 2, 0, error))
            return false;
        g->maps->quake_active = true;
    }
    if (!q1_alive(g, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!body.ground.registry)
        return true;
    body.velocity.z += q1_random(g) * 150;
    return qa_world_body_write(g->services.world, actor, &body, error);
}
