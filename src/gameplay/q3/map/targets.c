#include "internal.h"

static bool map_event(qa_q3_game *game, qa_q3_map_event_kind kind,
                      qa_q3_map_actor_state *state, qa_actor_id other,
                      qa_actor_id activator, qa_error *error) {
    return q3_map_emit(game, &(qa_q3_map_event){.kind = kind,
                                                .actor = activator,
                                                .other = other,
                                                .text = state->message,
                                                .origin = state->origin,
                                                .points = state->count,
                                                .flags = state->spawnflags},
                       error);
}

static bool allocate_target(qa_q3_game *game, qa_q3_map_actor_state *state,
                            bool link, qa_error *error) {
    state->usable = state->kind != QA_Q3_MAP_TARGET_POSITION &&
                    state->kind != QA_Q3_MAP_TARGET_LOCATION &&
                    state->kind != QA_Q3_MAP_TARGET_LASER;
    return q3_map_allocate(game, state, NULL, link, error);
}

static bool normalize_speaker_noise(qa_q3_game *game, qa_q3_map_actor_state *state,
                                    qa_error *error) {
    const char *noise = q3_map_cstr(game, state->noise);
    if (!noise)
        return q3_map_fail(error, "Q3 target_speaker has no noise key");
    if (noise[0] == '*')
        state->spawnflags |= 8u;
    char path[64];
    int written = snprintf(path, sizeof(path), strstr(noise, ".wav") ? "%s" : "%s.wav", noise);
    if (written < 0)
        return q3_map_fail(error, "invalid Q3 target_speaker noise");
    path[sizeof(path) - 1] = '\0';
    return q3_map_intern_cstr(game, path, &state->noise, error);
}

bool q3_map_spawn_target(qa_q3_game *game, const qa_q3_map_fields *fields,
                         qa_q3_map_actor_state *state, qa_error *error) {
    const char *name = q3_map_cstr(game, state->classname);
    if (!name)
        return q3_map_fail(error, "missing Q3 target classname");
    if (!strcmp(name, "target_give"))
        state->kind = QA_Q3_MAP_TARGET_GIVE;
    else if (!strcmp(name, "target_remove_powerups"))
        state->kind = QA_Q3_MAP_TARGET_REMOVE_POWERUPS;
    else if (!strcmp(name, "target_delay")) {
        state->kind = QA_Q3_MAP_TARGET_DELAY;
        if (state->has_delay)
            state->wait = state->delay;
        else {
            double wait;
            if (!q3_map_number(fields, "wait", 1, &wait, error))
                return false;
            state->wait = (float)wait;
        }
        if (state->wait == 0)
            state->wait = 1;
    } else if (!strcmp(name, "target_score")) {
        state->kind = QA_Q3_MAP_TARGET_SCORE;
        if (!state->count)
            state->count = 1;
    } else if (!strcmp(name, "target_print"))
        state->kind = QA_Q3_MAP_TARGET_PRINT;
    else if (!strcmp(name, "target_speaker")) {
        state->kind = QA_Q3_MAP_TARGET_SPEAKER;
        double wait, random;
        if (!q3_map_number(fields, "wait", 0, &wait, error) ||
            !q3_map_number(fields, "random", 0, &random, error))
            return false;
        state->wait = (float)wait;
        state->random = (float)random;
        if (!normalize_speaker_noise(game, state, error))
            return false;
        if (!qa_q3_sound_index(game, q3_map_cstr(game, state->noise),
                               &state->noise_index, error))
            return false;
        state->sound_frame = q3_map_float_to_int(state->wait * 10.0f);
        state->sound_random = q3_map_float_to_int(state->random * 10.0f);
    } else if (!strcmp(name, "target_push")) {
        state->kind = QA_Q3_MAP_TARGET_PUSH;
        if (state->speed == 0)
            state->speed = 1000;
        state->direction = q3_map_direction(state->angles);
        state->angles = qa_v3(0, 0, 0);
        state->launch_velocity = qa_vec_scale(state->direction, state->speed);
        if (!qa_q3_sound_index(game, (state->spawnflags & 1u)
                ? "sound/world/jumppad.wav" : "sound/misc/windfly.wav",
                &state->noise_index, error))
            return false;
    } else if (!strcmp(name, "target_laser"))
        state->kind = QA_Q3_MAP_TARGET_LASER;
    else if (!strcmp(name, "target_teleporter")) {
        state->kind = QA_Q3_MAP_TARGET_TELEPORTER;
        if (!q3_map_text(game, state->targetname))
            q3_map_warn(game, (qa_actor_id){0}, "untargeted Q3 target_teleporter");
    } else if (!strcmp(name, "target_kill"))
        state->kind = QA_Q3_MAP_TARGET_KILL;
    else if (!strcmp(name, "target_location"))
        state->kind = QA_Q3_MAP_TARGET_LOCATION;
    else if (!strcmp(name, "target_relay"))
        state->kind = QA_Q3_MAP_TARGET_RELAY;
    else if (!strcmp(name, "target_position"))
        state->kind = QA_Q3_MAP_TARGET_POSITION;
    else
        return q3_map_fail(error, "unsupported Q3 target classname");
    if (!qa_actors_get(qa_session_actors(game->options.services.session), state->actor)) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    bool link = state->kind == QA_Q3_MAP_TARGET_SPEAKER;
    if (!allocate_target(game, state, false, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return q3_map_fail(error, "missing Q3 target state");
    qa_actor_id actor = stored->actor;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire)
        return q3_rollback_spawn(game, actor, error);
    if (stored->kind == QA_Q3_MAP_TARGET_SPEAKER) {
        uint32_t source_slot;
        if (!qa_q3_source_actor_slot(game, actor, &source_slot, error))
            return q3_rollback_spawn(game, actor, error);
        wire->type = 7;
        wire->event_parameter = stored->noise_index;
        wire->frame = stored->sound_frame;
        wire->client = stored->sound_random;
        if (stored->spawnflags & 1u)
            wire->loop_sound = stored->noise_index;
        if (stored->spawnflags & 4u)
            game->source_entities[source_slot].server_flags |= 32u;
        wire->position.base = wire->authored_origin;
    } else if (stored->kind == QA_Q3_MAP_TARGET_PUSH) {
        wire->authored_angles = stored->angles;
        wire->origin2 = stored->launch_velocity;
    }
    if (link && !qa_q3_wire_link(game, actor, NULL, error))
        return q3_rollback_spawn(game, actor, error);
    stored = q3_map_get(game, actor);
    if (!stored) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    qa_linked_body linked;
    stored->linked = qa_world_linked(game->options.services.world, actor, &linked);
    if (!q3_wire_entity_ready(game, actor, error))
        return q3_rollback_spawn(game, actor, error);
    if (stored->kind == QA_Q3_MAP_TARGET_PUSH && q3_map_text(game, stored->target))
        q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_AIM);
    else if (stored->kind == QA_Q3_MAP_TARGET_LASER)
        q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_LASER_START);
    else if (stored->kind == QA_Q3_MAP_TARGET_LOCATION)
        q3_map_schedule(game, stored, 200, QA_Q3_MAP_THINK_LOCATIONS);
    else if (stored->kind == QA_Q3_MAP_TARGET_SPEAKER &&
             ((stored->spawnflags & 1u) || stored->sound_random != 0)) {
        qa_actor_id actor = stored->actor;
        stored->sound_looping = (stored->spawnflags & 1u) != 0;
        if (!q3_map_emit(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_SOUND,
                                                  .actor = actor,
                                                  .text = stored->noise,
                                                  .origin = stored->origin,
                                                  .value = stored->sound_looping ? 1 : 0,
                                                  .sound_interval_tenths =
                                                      stored->sound_frame,
                                                  .sound_random_tenths =
                                                      stored->sound_random,
                                                  .flags = stored->spawnflags},
                         error))
            return q3_rollback_spawn(game, actor, error);
        stored = q3_map_get(game, actor);
        if (!stored) {
            state->actor = (qa_actor_id){0};
            return true;
        }
    }
    *state = *stored;
    return true;
}

static bool give_items(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_actor_id activator, qa_error *error) {
    if (!activator.registry)
        return q3_map_fail(error, "Q3 target_give requires an activator");
    qa_actor_id source = state->actor;
    qa_string_id target = state->target;
    if (!q3_map_is_player(game, activator) || !q3_map_text(game, target))
        return true;
    if (!q3_map_get(game, source))
        return true;
    qa_target_cursor cursor = {0};
    qa_actor_id actor;
    while (qa_targets_next(game->map->options.targets, target, &cursor, &actor)) {
        qa_q3_map_actor_state *item = q3_map_get(game, actor);
        if (!item || item->kind != QA_Q3_MAP_ITEM || !item->item_bound)
            continue;
        if (!q3_item_touch(game, actor, activator, true, NULL, error))
            return false;
        if (!q3_map_get(game, source))
            return true;
        item = q3_map_get(game, actor);
        q3_actor *native = q3_actor_get(game, actor);
        if (!item || !native || native->kind != Q3_ACTOR_ITEM)
            continue;
        item->due_ms = 0;
        native->state.item.respawn_at = 0;
        if (!qa_world_unlink(game->options.services.world, actor, error))
            return false;
        if (!q3_map_get(game, source))
            return true;
        item = q3_map_get(game, actor);
        if (item)
            item->linked = false;
    }
    return true;
}

static bool remove_powerups(qa_q3_game *game, qa_q3_map_actor_state *state,
                            qa_actor_id activator, qa_error *error) {
    if (!activator.registry)
        return q3_map_fail(error, "Q3 target_remove_powerups requires an activator");
    q3_actor *player = q3_actor_get(game, activator);
    if (!player || player->kind != Q3_ACTOR_PLAYER)
        return true;
    int32_t team = -1;
    if (player->state.player.powerups[QA_Q3_P_REDFLAG])
        team = 1;
    else if (player->state.player.powerups[QA_Q3_P_BLUEFLAG])
        team = 2;
    else if (player->state.player.powerups[QA_Q3_P_NEUTRALFLAG])
        team = 0;
    if (team >= 0 &&
        !q3_map_emit(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_RETURN_FLAG,
                                              .actor = activator,
                                              .other = state->actor,
                                              .team = team},
                     error))
        return false;
    player = q3_actor_get(game, activator);
    if (player && player->kind == Q3_ACTOR_PLAYER)
        memset(player->state.player.powerups, 0, sizeof(player->state.player.powerups));
    return true;
}

static bool use_speaker(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_actor_id activator, qa_error *error) {
    if (state->spawnflags & 3u) {
        q3_wire_entity_source *wire = q3_wire_entity(game, state->actor);
        if (!wire)
            return q3_map_fail(error, "Q3 speaker lost its actual source sound fields");
        wire->loop_sound = wire->loop_sound ? 0 : state->noise_index;
        state->sound_looping = wire->loop_sound != 0;
        return q3_map_emit(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_SOUND,
                                                    .actor = state->actor,
                                                    .other = activator,
                                                    .text = state->noise,
                                                    .value = state->sound_looping ? 1 : 0,
                                                    .sound_interval_tenths =
                                                        state->sound_frame,
                                                    .sound_random_tenths =
                                                        state->sound_random,
                                                    .flags = state->spawnflags},
                           error);
    }
    if ((state->spawnflags & 8u) && !activator.registry)
        return q3_map_fail(error, "Q3 target_speaker requires an activator");
    qa_actor_id source = (state->spawnflags & 8u) ? activator : state->actor;
    int32_t event = (state->spawnflags & 8u) ? 45 : (state->spawnflags & 4u) ? 46 : 45;
    if (!q3_wire_add_event(game, source, event, state->noise_index, error))
        return false;
    return q3_map_emit(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_SOUND,
                                                .actor = source,
                                                .other = state->actor,
                                                .text = state->noise,
                                                .origin = state->origin,
                                                .sound_interval_tenths =
                                                    state->sound_frame,
                                                .sound_random_tenths =
                                                    state->sound_random,
                                                .flags = state->spawnflags},
                       error);
}

static bool use_push(qa_q3_game *game, qa_q3_map_actor_state *state,
                     qa_actor_id activator, qa_error *error) {
    if (!activator.registry)
        return q3_map_fail(error, "Q3 target_push requires an activator");
    qa_actor_id source = state->actor;
    qa_vec3 velocity = state->launch_velocity;
    uint32_t spawnflags = state->spawnflags;
    int32_t sound_index = state->noise_index;
    q3_actor *player = NULL;
    if (!q3_map_player_launchable(game, activator, &player))
        return true;
    if (!q3_map_get(game, source))
        return true;
    if (!q3_map_set_velocity(game, activator, velocity, error))
        return false;
    if (!q3_map_get(game, source))
        return true;
    player = q3_actor_get(game, activator);
    bool sound = false;
    if (!player || player->kind != Q3_ACTOR_PLAYER) {
        if (!qa_actors_get(qa_session_actors(game->options.services.session), activator))
            return true;
        sound = true;
    } else if (q3_sub_time(game->now_ms, player->state.player.fly_sound_after) > 0) {
        player->state.player.fly_sound_after = q3_add_time(game->now_ms, 1500);
        sound = true;
    }
    if (sound) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, activator, &body, error))
            return false;
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), activator))
            return true;
        qa_actor_id temporary;
        if (!q3_wire_temp_entity(game, body.origin, 45, &temporary, error))
            return false;
        qa_q3_entity *event = q3_wire_temporary(game, temporary);
        if (!event)
            return q3_map_fail(error, "Q3 target push lost its temporary source sound");
        event->eventParm = sound_index;
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), activator))
            return true;
        if (!q3_sound_report(game, activator, (spawnflags & 1u)
                ? "sound/world/jumppad.wav" : "sound/misc/windfly.wav", 3, error))
            return false;
    }
    return true;
}

static bool relay(qa_q3_game *game, qa_q3_map_actor_state *state,
                  qa_actor_id activator, qa_error *error) {
    uint32_t flags = state->spawnflags;
    qa_string_id target = state->target;
    qa_actor_id source = state->actor;
    if ((flags & 3u) && !activator.registry)
        return q3_map_fail(error, "team-filtered Q3 target_relay requires an activator");
    if (q3_map_is_player(game, activator)) {
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), activator))
            return true;
        int32_t team = q3_map_team(game, activator);
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), activator))
            return true;
        if ((flags & 1u) && team != 1)
            return true;
        if ((flags & 2u) && team != 2)
            return true;
    }
    if (flags & 4u) {
        qa_actor_id selected;
        if (!q3_map_pick(game, target, &selected, error) || !selected.registry)
            return true;
        if (!q3_map_get(game, source))
            return true;
        return qa_targets_invoke(game->map->options.targets, selected, source, activator,
                                 error);
    }
    state = q3_map_get(game, source);
    if (!state)
        return true;
    return q3_map_use_targets(game, state, activator, error);
}

static bool laser(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);

bool q3_map_target_use(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_actor_id other, qa_actor_id activator, qa_error *error) {
    (void)other;
    if (!state->usable)
        return true;
    switch (state->kind) {
    case QA_Q3_MAP_TARGET_GIVE:
        return give_items(game, state, activator, error);
    case QA_Q3_MAP_TARGET_REMOVE_POWERUPS:
        return remove_powerups(game, state, activator, error);
    case QA_Q3_MAP_TARGET_DELAY: {
        state->activator = activator;
        state->due_ms = q3_map_random_schedule(game->now_ms, state->wait,
                                                state->random, q3_crandom(game));
        state->think = QA_Q3_MAP_THINK_DELAY;
        q3_postgame_native_think_assigned(game, state->actor);
        return true;
    }
    case QA_Q3_MAP_TARGET_SCORE: {
        if (!activator.registry)
            return q3_map_fail(error, "Q3 target_score requires an activator");
        qa_actor_id actor = state->actor;
        if (!q3_map_is_player(game, activator))
            return true;
        state = q3_map_get(game, actor);
        return !state || map_event(game, QA_Q3_MAP_SCORE, state, actor, activator, error);
    }
    case QA_Q3_MAP_TARGET_PRINT:
        if (!activator.registry)
            return q3_map_fail(error, "Q3 target_print requires an activator");
        return map_event(game, QA_Q3_MAP_PRINT, state, state->actor, activator, error);
    case QA_Q3_MAP_TARGET_SPEAKER:
        return use_speaker(game, state, activator, error);
    case QA_Q3_MAP_TARGET_PUSH:
        return use_push(game, state, activator, error);
    case QA_Q3_MAP_TARGET_LASER: {
        qa_actor_id actor = state->actor;
        state->activator = activator;
        if (q3_postgame_think_time(game, actor, state->due_ms) > 0) {
            if (!qa_world_unlink(game->options.services.world, actor, error))
                return false;
            state = q3_map_get(game, actor);
            if (state) {
                state->due_ms = 0;
                q3_postgame_nextthink_assigned(game, actor, 0);
                state->linked = false;
            }
            return true;
        }
        if (!state->activator.registry)
            state->activator = state->actor;
        state->think = QA_Q3_MAP_THINK_LASER;
        state->due_ms = 0;
        return laser(game, state, error);
    }
    case QA_Q3_MAP_TARGET_TELEPORTER: {
        if (!activator.registry)
            return q3_map_fail(error, "Q3 target_teleporter requires an activator");
        qa_actor_id actor = state->actor;
        qa_string_id target_name = state->target;
        if (!q3_map_is_player(game, activator))
            return true;
        if (!q3_map_get(game, actor))
            return true;
        qa_actor_id destination;
        if (!q3_map_pick(game, target_name, &destination, error) || !destination.registry)
            return true;
        if (!q3_map_get(game, actor))
            return true;
        qa_vec3 origin, angles;
        if (!q3_map_target_pose(game, destination, &origin, &angles, error))
            return false;
        if (!q3_map_get(game, actor) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), activator) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), destination))
            return true;
        return qa_q3_teleport(game, activator, origin, angles, error);
    }
    case QA_Q3_MAP_TARGET_KILL:
        if (!activator.registry)
            return q3_map_fail(error, "Q3 target_kill requires an activator");
        return q3_damage(game, activator, (qa_actor_id){0}, (qa_actor_id){0}, QA_Q3_W_NONE,
                         18, 8, 100000, qa_v3(0, 0, 0), qa_v3(0, 0, 0), false, NULL,
                         error);
    case QA_Q3_MAP_TARGET_RELAY:
        return relay(game, state, activator, error);
    default:
        return true;
    }
}

bool q3_map_aim(qa_q3_game *game, qa_q3_map_actor_state *state, qa_vec3 origin,
                qa_error *error) {
    qa_actor_id actor = state->actor;
    qa_string_id target_name = state->target;
    qa_actor_id target;
    if (!q3_map_pick(game, target_name, &target, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (!target.registry)
        return qa_session_release(game->options.services.session, actor, error);
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, target, &body, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), target))
        return qa_session_release(game->options.services.session, actor, error);
    q3_wire_entity_source *target_wire = q3_wire_entity(game, target);
    qa_vec3 target_origin = target_wire ? target_wire->authored_origin : body.origin;
    float height = target_origin.z - origin.z;
    float gravity = game->options.rules.gravity;
    float time = sqrtf(height / (0.5f * gravity));
    if (!isfinite(time) || time == 0)
        return qa_session_release(game->options.services.session, actor, error);
    qa_vec3 offset = qa_vec_sub(target_origin, origin);
    qa_vec3 horizontal = qa_v3(offset.x, offset.y, 0);
    float distance = qa_vec_length(horizontal);
    qa_vec3 direction = distance == 0 ? horizontal : qa_vec_scale(horizontal, 1.0f / distance);
    state->launch_velocity = qa_vec_add(qa_vec_scale(direction, distance / time),
                                        qa_v3(0, 0, time * gravity));
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire)
        return q3_map_fail(error, "Q3 target aiming lost its actual source endpoint");
    wire->origin2 = state->launch_velocity;
    state->due_ms = 0;
    return true;
}

static bool start_laser(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_error *error) {
    qa_actor_id actor = state->actor;
    qa_string_id target_name = state->target;
    qa_actor_id enemy = {0};
    qa_vec3 direction = state->direction;
    qa_vec3 angles = state->angles;
    if (q3_map_text(game, target_name)) {
        if (!qa_targets_first(game->map->options.targets, target_name, &enemy)) {
            q3_map_warn(game, actor, "Q3 target_laser has a bad target");
        }
    } else {
        direction = q3_map_direction(angles);
        angles = qa_v3(0, 0, 0);
    }
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->enemy = enemy;
    state->direction = direction;
    state->angles = angles;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire)
        return q3_map_fail(error, "Q3 laser start lost its source entity fields");
    wire->type = 5;
    wire->authored_angles = angles;
    if (!state->damage)
        state->damage = 1;
    state->usable = true;
    state->think = QA_Q3_MAP_THINK_LASER;
    q3_postgame_native_think_assigned(game, actor);
    state->due_ms = 0;
    if (state->spawnflags & 1u) {
        state->activator = actor;
        return laser(game, state, error);
    }
    return true;
}

static bool laser(qa_q3_game *game, qa_q3_map_actor_state *state, qa_error *error) {
    qa_actor_id actor = state->actor;
    qa_actor_id enemy = state->enemy;
    if (enemy.registry) {
        qa_body_state target;
        qa_error local = {0};
        bool read = qa_world_body_read(game->options.services.world, enemy, &target, &local);
        state = q3_map_get(game, actor);
        if (!state)
            return true;
        bool enemy_live = qa_actors_get(
                              qa_session_actors(game->options.services.session), enemy) != NULL;
        if (read && enemy_live && qa_actor_id_equal(state->enemy, enemy)) {
            q3_wire_entity_source *enemy_wire = q3_wire_entity(game, enemy);
            qa_vec3 enemy_origin = enemy_wire ? enemy_wire->authored_origin : target.origin;
            qa_vec3 center = qa_vec_add(
                qa_vec_add(enemy_origin, qa_vec_scale(target.bounds.mins, 0.5f)),
                qa_vec_scale(target.bounds.maxs, 0.5f));
            state->direction = qa_vec_normalize(qa_vec_sub(center, state->origin));
        } else if (!read && local.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = local;
            return false;
        } else {
            if (qa_actor_id_equal(state->enemy, enemy))
                state->enemy = (qa_actor_id){0};
        }
    }
    qa_actor_id activator = state->activator;
    qa_vec3 origin = state->origin;
    qa_vec3 direction = state->direction;
    int32_t damage = state->damage;
    qa_vec3 end = qa_vec_add(origin, qa_vec_scale(direction, 2048));
    qa_trace_result trace;
    if (!q3_trace(game, origin, end, actor, Q3_MASK_SHOT, &trace, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (trace.hit == QA_TRACE_HIT_ACTOR &&
        qa_actors_get(qa_session_actors(game->options.services.session), trace.actor) &&
        q3_entity_number(game, trace.actor) != 0 &&
        !q3_damage(game, trace.actor, activator, actor, QA_Q3_W_NONE, 21, 4,
                   (float)damage, direction, trace.end, false, NULL, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    origin = state->origin;
    direction = state->direction;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire)
        return q3_map_fail(error, "Q3 laser trace lost its source endpoint");
    wire->origin2 = trace.end;
    if (!qa_q3_wire_link(game, actor, NULL, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    qa_linked_body linked;
    state->linked = qa_world_linked(game->options.services.world, actor, &linked);
    state->think = QA_Q3_MAP_THINK_LASER;
    state->due_ms = q3_add_time(game->now_ms, 100);
    q3_postgame_nextthink_assigned(game, actor, state->due_ms);
    return q3_event(game, actor, trace.actor, QA_BUILTIN_BEAM, 0, 0, origin,
                    trace.end, direction, error);
}

static bool link_locations(qa_q3_game *game, qa_error *error) {
    if (game->map->locations_linked)
        return true;
    game->map->locations_linked = true;
    game->map->location_head = (qa_actor_id){0};
    qa_string_id unknown;
    if (!q3_map_intern_cstr(game, "unknown", &unknown, error) ||
        !q3_configstring_event(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_CONFIGSTRING,
                                              .index = 608,
                                              .text = unknown},
                     error))
        return false;
    int32_t number = 1;
    for (uint32_t i = 0; i < game->source_count; ++i) {
        qa_q3_map_actor_state *location = q3_map_get(game, game->source_entities[i].actor);
        if (!location || location->kind != QA_Q3_MAP_TARGET_LOCATION)
            continue;
        location->health = number;
        qa_q3_map_event event = {.kind = QA_Q3_MAP_CONFIGSTRING,
                                 .actor = location->actor,
                                 .index = 608 + number,
                                 .text = location->message};
        if (!q3_configstring_event(game, &event, error))
            return false;
        ++number;
        location = q3_map_get(game, event.actor);
        if (location && location->kind == QA_Q3_MAP_TARGET_LOCATION) {
            location->path_next = game->map->location_head;
            game->map->location_head = location->actor;
        }
    }
    return true;
}

bool q3_map_target_think(qa_q3_game *game, qa_q3_map_actor_state *state,
                         qa_error *error) {
    switch (state->think) {
    case QA_Q3_MAP_THINK_AIM:
        return q3_map_aim(game, state, state->origin, error);
    case QA_Q3_MAP_THINK_LASER_START:
        return start_laser(game, state, error);
    case QA_Q3_MAP_THINK_LASER:
        return laser(game, state, error);
    case QA_Q3_MAP_THINK_DELAY: {
        qa_actor_id activator = state->activator;
        state->due_ms = 0;
        return q3_map_use_targets(game, state, activator, error);
    }
    case QA_Q3_MAP_THINK_LOCATIONS:
        state->due_ms = 0;
        return link_locations(game, error);
    default:
        return true;
    }
}
