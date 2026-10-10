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
    bool ok = g->host.check_client(g->host.context, actor, &client, error);
    if (ok && q1_alive(g, actor) && q1_alive(g, client)) {
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
    case Q1_MAP_HIP_FINALE: {
        if (g->options.deathmatch)
            return q1_remove(g, entity, error);
        state->field_state = 0;
        state->use_enabled = true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body.angles = state->mangle;
        return qa_world_body_write(g->services.world, entity->id, &body, error);
    }
    case Q1_MAP_START_ENDTEXT:
        state->use_enabled = true;
        return true;
    case Q1_MAP_SOUND: {
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->classname);
        bool thunder = !strncmp(name, "random_thunder", 14);
        bool periodic = !strstr(name, "_triggered");
        state->volume = state->volume != 0 ? state->volume : 1;
        entity->speed = entity->speed == 0 ? 1 : entity->speed == -1 ? 0 : entity->speed;
        if ((entity->spawnflags & 1) && !state->impulse)
            state->impulse = 7;
        if (!qa_builtin_resource(&g->services, "misc/null.wav", &state->noise[1], error) ||
            (thunder &&
             !qa_builtin_resource(&g->services, "ambience/thunder1.wav", &state->noise[0], error)))
            return false;
        state->use_enabled = true;
        if (periodic) {
            entity->wait = entity->wait != 0 ? entity->wait : 20;
            entity->delay = entity->delay != 0 ? entity->delay : 2;
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
                                      state->volume != 0 ? state->volume : .5f, error);
        return q1_map_fail(error, "unknown Hipnotic ambient source");
    }
    case Q1_MAP_COMMAND:
        if (!q1_map_text(g, entity->message))
            return true;
        if (!g->maps->options.server_command)
            return q1_map_fail(error, "Q1 authored command has no command owner");
        return g->maps->options.server_command(g->maps->options.context, entity->message, error);
    case Q1_MAP_EXPLODER:
        entity->damage = entity->damage != 0 ? fmaxf(0, entity->damage) : 120;
        entity->speed = entity->speed != 0 ? entity->speed : 1;
        if (q1_classnamed(g, entity->id, "func_multi_exploder")) {
            entity->model = QA_STRING_NONE;
            entity->physics.motion = QA_PHYSICS_STATIONARY;
            entity->wait = entity->wait != 0 ? entity->wait : .25f;
            state->duration = state->duration != 0 ? state->duration : 1;
            state->volume = state->volume != 0 ? state->volume : .5f;
        } else
            state->volume = state->volume != 0 ? state->volume : 1;
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
        entity->damage = entity->damage != 0 ? entity->damage : .8f;
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
    return q1_map_targets(g, entity, q1_ref_actor(g, entity->activator), error);
}
static bool explode(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (q1_classnamed(g, entity->id, "func_multi_exploder"))
        return q1_multi_explosion_think(g, entity, error);
    if (!q1_map_targets(g, entity, q1_ref_actor(g, entity->activator), error))
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
    if (!q1_radius(g, entity->id, q1_ref_actor(g, entity->owner), entity->damage, entity->id, QA_Q1_WEAPON_COUNT,
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
    qa_actor_id parent = entity->id;
    qa_body_state source;
    if (!qa_world_body_read(g->services.world, parent, &source, error))
        return false;
    entity = q1_entity(g, parent);
    if (!entity)
        return true;
    double count = fmax(1, entity->count);
    int32_t variant = entity->map->style;
    for (size_t i = 0; (double)i < count; ++i) {
        int32_t model = variant ? variant : (int32_t)floorf(1 + 3 * q1_random(g));
        q1_actor *piece;
        if (!q1_create(g, "hip_rubble", Q1_MAP, (qa_actor_id){0}, &piece, error))
            return false;
        qa_actor_id child = piece->id;
        entity = q1_entity(g, parent);
        if (!entity)
            goto retired;
        if (!q1_map_allocate(g, piece, error))
            goto failed;
        piece->map->kind = Q1_MAP_RUBBLE;
        piece->map->touch_enabled = true;
        piece->physics.motion = QA_PHYSICS_BOUNCE;
        piece->physics.solid = QA_PHYSICS_BOX;
        if (!q1_think_deadline(g->time, 0, &piece->physics.q1_pusher.local_seconds, error))
            goto failed;
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
            !qa_world_body_write(g->services.world, child, &body, error))
            goto failed;
        piece = q1_entity(g, child);
        if (!piece || !q1_entity(g, parent))
            goto retired;
        if (!q1_map_schedule(g, piece, 13 + q1_random(g) * 10, Q1_MAP_REMOVE, error) ||
            !q1_link(g, piece, error))
            goto failed;
        if (!q1_entity(g, child) || !q1_entity(g, parent))
            goto retired;
        continue;
failed:
        if (qa_actors_get(qa_session_actors(g->services.session), child))
            (void)qa_session_release(g->services.session, child, NULL);
        return false;
retired:
        if (qa_actors_get(qa_session_actors(g->services.session), child))
            (void)qa_session_release(g->services.session, child, NULL);
        return true;
    }
    return true;
}
static bool start_endtext(qa_q1_game *g, q1_actor *entity, qa_actor_id activator,
                           qa_error *error) {
    const qa_q1_level_state *level = qa_q1_level_read(g->maps->options.level);
    qa_string_id map = level->next_map ? level->next_map : g->maps->options.current_map;
    if (!qa_q1_level_cutscene(g->maps->options.level, map, activator, g->time, error))
        return false;
    qa_q1_intermission_result result;
    if (!qa_q1_level_client_connected(g->maps->options.level, g->time, false, &result, error))
        return false;
    return !q1_alive(g, entity->id) ||
           q1_map_present_intermission(g, entity->id, 4, &result, error);
}
bool qa_q1_game_map_intermission_input(qa_q1_game *g, qa_actor_id actor, bool pressed,
                                        bool *handled, qa_error *error) {
    *handled = false;
    if (!g->maps || !q1_alive(g, actor))
        return true;
    const qa_q1_level_state *level = qa_q1_level_read(g->maps->options.level);
    if (!level->intermission)
        return true;
    *handled = true;
    bool same_level = false;
    if (g->host.cvars) {
        float value;
        if (!q1_source_value(g, QA_Q1_SOURCE_SAMELEVEL, 0, &value, error))
            return false;
        same_level = value != 0;
    }
    qa_q1_intermission_result result;
    return qa_q1_level_request_exit(g->maps->options.level, g->time, pressed, same_level,
                                     &result, error) &&
           q1_map_present_intermission(g, actor, 4, &result, error);
}
static bool effect_finale(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->map->field_state == 1)
        return true;
    entity->map->field_state = 1;
    qa_actor_id id = entity->id;
    qa_actor_id camera = q1_find_target(g, entity->target);
    if (!camera.registry)
        return q1_map_fail(error, "no target in finale");
    qa_body_state camera_body;
    qa_vec3 angles = qa_v3(0, 0, 0);
    if (!qa_world_body_read(g->services.world, camera, &camera_body, error))
        return false;
    (void)qa_targets_vector(g->maps->options.targets, camera, "mangle", &angles);
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .time_ns = g->time_ns,
                              .origin = camera_body.origin, .direction = angles};
    if (!qa_builtin_resource(&g->services, "cutscene", &event.resource, error) ||
        !qa_builtin_resource(&g->services, "", &event.text, error) ||
        !qa_builtin_emit(&g->services, &event, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_builtin_snapshot_frame *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    bool ok = true;
    if (!(entity->spawnflags & 2)) {
        qa_actor_id path = q1_find_target(g, entity->map->mdl);
        qa_authored_target authored;
        qa_body_state source;
        qa_actor_id position = (entity->spawnflags & 1)
                                  ? players->snapshot.count ? players->snapshot.ids[0]
                                                              : g->maps->world_actor
                                  : path;
        ok = qa_targets_read(g->maps->options.targets, path, &authored) &&
             qa_world_body_read(g->services.world, position, &source, error);
        if (!ok && (!error || error->code == QA_OK))
            q1_map_fail(error, "Hipnotic finale has no decoy path");
        if (ok && q1_alive(g, id)) {
            qa_actor_id decoy;
            ok = q1_become_decoy(g, source.origin, authored.target, &decoy, error);
        }
    }
    if (ok && q1_alive(g, id) && !g->maps->options.control_player)
        ok = q1_map_fail(error, "Hipnotic finale requires selected player control owners");
    for (size_t i = 0; ok && i < players->snapshot.count && q1_alive(g, id); ++i)
        if (q1_alive(g, players->snapshot.ids[i]))
            ok = g->maps->options.control_player(g->maps->options.context,
                players->snapshot.ids[i], camera_body.origin, angles, qa_v3(0, 0, 0), error);
    qa_builtin_snapshot_release(players);
    if (!ok || !q1_alive(g, id))
        return ok;
    g->maps->finale.origin = camera_body.origin;
    g->maps->finale.angles = angles;
    if (entity->map->spawn_function)
        return q1_map_schedule(g, entity, entity->wait, Q1_MAP_HIP_FINALE_NEXT, error);
    return true;
}
bool q1_map_hip_misc_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    switch (entity->map->kind) {
    case Q1_MAP_HIP_FINALE:
        return effect_finale(g, entity, error);
    case Q1_MAP_START_ENDTEXT:
        return start_endtext(g, entity, activator, error);
    case Q1_MAP_SOUND:
        return play_sound(g, entity, error);
    case Q1_MAP_EXPLODER: {
        entity->activator = q1_ref_from(g, activator);
        if (entity->delay == 0)
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
    double local_time;
    if (!q1_local_time(entity, &local_time, error))
        return false;
    qa_combat_state combat;
    if (local_time < entity->map->cooldown ||
        !qa_combat_read(g->services.combat, other, &combat, NULL) || !combat.can_take_damage)
        return true;
    if (!q1_damage(g, other, entity->id, q1_ref_actor(g, entity->owner), 10, QA_Q1_WEAPON_COUNT, error))
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
    if (action == Q1_MAP_HIP_FINALE_NEXT) {
        const char *function = qa_strings_cstr(qa_session_strings(g->services.session),
                                               entity->map->spawn_function);
        if (function && !strcmp(function, "info_startendtext_use"))
            return start_endtext(g, entity, q1_ref_actor(g, entity->activator), error);
        if (function && !strcmp(function, "effect_finale_use"))
            return effect_finale(g, entity, error);
        if (function && !strcmp(function, "SUB_Remove"))
            return q1_remove(g, entity, error);
        return q1_map_fail(error, "Hipnotic finale has an unknown native continuation");
    }
    if (action == Q1_MAP_EXPLODER_FIRE)
        return explode(g, entity, error);
    if (action != Q1_MAP_SOUND_REPEAT)
        return q1_map_fail(error, "unknown Hipnotic miscellaneous continuation");
    return q1_map_schedule(g, entity, fmax(entity->delay, entity->wait * q1_random(g)),
                           Q1_MAP_SOUND_REPEAT, error) &&
           play_sound(g, entity, error);
}
static bool after_physics(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g || !g->maps || !q1_alive(g, g->maps->world_actor) || !q1_alive(g, actor))
        return true;
    if (g->options.program == QA_Q1_ROGUE)
        return q1_map_rogue_ending(g, actor, error);
    qa_actor_id world = g->maps->world_actor;
    if (!qa_world_body_storage_serial(g->services.world, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_physics_properties ground_flags;
    if (!g->services.physics || !g->services.physics->services.read(
        g->services.physics->services.context, actor, &ground_flags)) return true;
    if (!q1_alive(g, world) || !q1_alive(g, actor))
        return true;
    if (g->maps->earthquake_end <= g->time) {
        if (!g->maps->quake_active)
            return true;
        if (!q1_sound(g, actor, "misc/quakeend.wav", 2, 0, error))
            return false;
        if (!q1_alive(g, world) || !q1_alive(g, actor))
            return true;
        g->maps->quake_active = false;
        return true;
    }
    if (!g->maps->quake_active) {
        if (!q1_sound(g, actor, "misc/quake.wav", 2, 0, error))
            return false;
        if (!q1_alive(g, world) || !q1_alive(g, actor))
            return true;
        g->maps->quake_active = true;
    }
    if (!(ground_flags.flags & QA_PHYSICS_ONGROUND))
        return true;
    body.velocity.z += q1_random(g) * 150;
    return qa_world_body_write(g->services.world, actor, &body, error);
}

bool qa_q1_game_map_after_physics(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g || !g->maps)
        return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = after_physics(g, actor, error);
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 source retired during map after-physics");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
