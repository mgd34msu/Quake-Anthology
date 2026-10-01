#include "internal.h"

static bool brush_trigger(qa_q3_game *game, qa_q3_map_actor_state *state,
                          qa_error *error) {
    if (!state->has_inline_model)
        return q3_map_fail(error, "Q3 brush trigger has no inline model");
    if (!qa_collision_model_bounds(qa_world_geometry(game->options.services.world),
                                   state->inline_model, &state->bounds, error))
        return false;
    if (state->angles.x != 0 || state->angles.y != 0 || state->angles.z != 0) {
        state->direction = q3_map_direction(state->angles);
        state->angles = qa_v3(0, 0, 0);
    }
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .inline_model = true,
                                    .model = state->inline_model,
                                    .contents = -1,
                                    .role = QA_COLLISION_SOLID};
    q3_wire_entity_source *wire = q3_wire_entity(game, state->actor);
    if (!wire)
        return q3_map_fail(error, "Q3 brush trigger has no source entity row");
    wire->model = (int32_t)state->inline_model;
    wire->authored_angles = state->angles;
    state->touchable = true;
    if (!q3_map_allocate(game, state, &collision, true, error))
        return false;
    if (!q3_map_get(game, state->actor)) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    collision.contents = Q3_CONTENTS_TRIGGER;
    collision.role = QA_COLLISION_TRIGGER;
    return qa_world_set_collision(game->options.services.world, state->actor, &collision, error);
}

bool q3_map_spawn_trigger(qa_q3_game *game, const qa_q3_map_fields *fields,
                          qa_q3_map_actor_state *state, qa_error *error) {
    const char *name = q3_map_cstr(game, state->classname);
    if (!name)
        return q3_map_fail(error, "missing Q3 trigger classname");
    bool brush = true, link = true;
    if (!strcmp(name, "trigger_multiple")) {
        state->kind = QA_Q3_MAP_TRIGGER_MULTIPLE;
        double wait, random;
        if (!q3_map_number(fields, "wait", 0.5, &wait, error) ||
            !q3_map_number(fields, "random", 0, &random, error))
            return false;
        state->wait = (float)wait;
        state->random = (float)random;
        if (state->random >= state->wait && state->wait >= 0) {
            state->random = state->wait - 100.0f;
            q3_map_warn(game, (qa_actor_id){0}, "Q3 trigger_multiple has random >= wait");
        }
    } else if (!strcmp(name, "trigger_always")) {
        state->kind = QA_Q3_MAP_TRIGGER_ALWAYS;
        brush = link = false;
    } else if (!strcmp(name, "trigger_push"))
        state->kind = QA_Q3_MAP_TRIGGER_PUSH;
    else if (!strcmp(name, "trigger_teleport"))
        state->kind = QA_Q3_MAP_TRIGGER_TELEPORT;
    else if (!strcmp(name, "trigger_hurt")) {
        state->kind = QA_Q3_MAP_TRIGGER_HURT;
        if (!state->damage)
            state->damage = 5;
        link = (state->spawnflags & 1u) == 0;
        state->usable = (state->spawnflags & 2u) != 0;
    } else if (!strcmp(name, "func_timer")) {
        state->kind = QA_Q3_MAP_TIMER;
        brush = link = false;
        state->usable = true;
        double wait, random;
        if (!q3_map_number(fields, "random", 1, &random, error) ||
            !q3_map_number(fields, "wait", 1, &wait, error))
            return false;
        state->random = (float)random;
        state->wait = (float)wait;
        if (state->random >= state->wait) {
            state->random = state->wait - 100.0f;
            q3_map_warn(game, (qa_actor_id){0}, "Q3 func_timer has random >= wait");
        }
    } else
        return q3_map_fail(error, "unsupported Q3 trigger classname");
    bool okay = brush ? brush_trigger(game, state, error)
                      : q3_map_allocate(game, state, NULL, false, error);
    if (!okay)
        return false;
    if (!state->actor.registry)
        return true;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return q3_map_fail(error, "missing Q3 trigger state");
    qa_actor_id actor = stored->actor;
    uint32_t source_slot;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire || !qa_q3_source_actor_slot(game, actor, &source_slot, error))
        return q3_rollback_spawn(game, actor, error);
    if (brush) {
        wire->model = (int32_t)stored->inline_model;
        wire->authored_angles = stored->angles;
        game->source_entities[source_slot].server_flags = 1u;
    }
    if (stored->kind == QA_Q3_MAP_TRIGGER_PUSH ||
        stored->kind == QA_Q3_MAP_TRIGGER_TELEPORT ||
        stored->kind == QA_Q3_MAP_TRIGGER_HURT) {
        const char *path = stored->kind == QA_Q3_MAP_TRIGGER_HURT
            ? "sound/world/electro.wav" : "sound/world/jumppad.wav";
        int32_t sound_index;
        if (!qa_q3_sound_index(game, path, &sound_index, error))
            return q3_rollback_spawn(game, actor, error);
        stored = q3_map_get(game, actor);
        wire = q3_wire_entity(game, actor);
        if (!stored || !wire) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        if (stored->kind == QA_Q3_MAP_TRIGGER_HURT)
            stored->noise_index = sound_index;
    }
    if (stored->kind == QA_Q3_MAP_TRIGGER_PUSH) {
        wire->type = 8;
        game->source_entities[source_slot].server_flags &= ~1u;
    } else if (stored->kind == QA_Q3_MAP_TRIGGER_TELEPORT) {
        wire->type = 9;
        if (!(stored->spawnflags & 1u))
            game->source_entities[source_slot].server_flags &= ~1u;
    } else if (stored->kind == QA_Q3_MAP_TIMER)
        game->source_entities[source_slot].server_flags = 1u;
    if (stored->kind == QA_Q3_MAP_TRIGGER_ALWAYS)
        q3_map_schedule(game, stored, 300, QA_Q3_MAP_THINK_ALWAYS);
    else if (stored->kind == QA_Q3_MAP_TRIGGER_PUSH)
        q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_AIM);
    else if (stored->kind == QA_Q3_MAP_TIMER && (stored->spawnflags & 1u)) {
        stored->activator = stored->actor;
        q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_TIMER);
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
    *state = *stored;
    return true;
}

static bool trigger_multiple(qa_q3_game *game, qa_q3_map_actor_state *state,
                             qa_actor_id activator, qa_error *error) {
    if (!activator.registry)
        return q3_map_fail(error, "Q3 trigger_multiple requires an activator");
    qa_actor_id actor = state->actor;
    state->activator = activator;
    if (q3_postgame_think_time(game, actor, state->due_ms) != 0)
        return true;
    bool player = q3_map_is_player(game, activator);
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (player) {
        int32_t team = q3_map_team(game, activator);
        state = q3_map_get(game, actor);
        if (!state)
            return true;
        if ((state->spawnflags & 1u) && team != 1)
            return true;
        if ((state->spawnflags & 2u) && team != 2)
            return true;
    }
    if (!q3_map_use_targets(game, state, activator, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (state->wait > 0) {
        state->due_ms = q3_map_random_schedule(game->now_ms, state->wait,
                                                state->random, q3_crandom(game));
        state->think = QA_Q3_MAP_THINK_MULTI_READY;
        q3_postgame_native_think_assigned(game, actor);
    } else {
        state->touchable = false;
        q3_map_schedule(game, state, 100, QA_Q3_MAP_THINK_FREE);
    }
    return true;
}

static bool timer(qa_q3_game *game, qa_q3_map_actor_state *state, qa_error *error) {
    qa_actor_id actor = state->actor, activator = state->activator;
    if (!q3_map_use_targets(game, state, activator, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->due_ms = q3_map_random_schedule(game->now_ms, state->wait,
                                            state->random, q3_crandom(game));
    state->think = QA_Q3_MAP_THINK_TIMER;
    q3_postgame_nextthink_assigned(game, actor, state->due_ms);
    return true;
}

bool q3_map_trigger_use(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_actor_id other, qa_actor_id activator, qa_error *error) {
    (void)other;
    switch (state->kind) {
    case QA_Q3_MAP_TRIGGER_MULTIPLE:
        return trigger_multiple(game, state, activator, error);
    case QA_Q3_MAP_TRIGGER_HURT:
        if (!state->usable)
            return true;
        if (state->linked) {
            qa_actor_id actor = state->actor;
            if (!qa_world_unlink(game->options.services.world, actor, error))
                return false;
            state = q3_map_get(game, actor);
            if (state)
                state->linked = false;
            return true;
        }
        qa_actor_id actor = state->actor;
        if (!qa_q3_wire_link(game, actor, NULL, error))
            return false;
        state = q3_map_get(game, actor);
        if (state)
            state->linked = true;
        return true;
    case QA_Q3_MAP_TIMER:
        state->activator = activator;
        if (q3_postgame_think_time(game, state->actor, state->due_ms) != 0) {
            state->due_ms = 0;
            q3_postgame_nextthink_assigned(game, state->actor, 0);
            return true;
        }
        return timer(game, state, error);
    default:
        return true;
    }
}

static double jump_pad_pitch(qa_vec3 velocity) {
    float pitch;
    if (velocity.x == 0 && velocity.y == 0)
        pitch = velocity.z > 0 ? 90.0f : 270.0f;
    else {
        float squared = q3_source_float_add(
            q3_source_float_multiply(velocity.x, velocity.x),
            q3_source_float_multiply(velocity.y, velocity.y));
        float horizontal = (float)sqrt((double)squared);
        float radians = (float)atan2((double)velocity.z, (double)horizontal);
        pitch = q3_source_float_multiply(radians, 180.0f) / Q3_PI;
        if (pitch < 0)
            pitch = q3_source_float_add(pitch, 360.0f);
    }
    int32_t turns = isfinite(pitch) ? (int32_t)(-(double)pitch * (65536.0 / 360.0)) : 0;
    double normalized = (360.0 / 65536.0) * ((uint32_t)turns & 65535u);
    return fabs(normalized > 180 ? normalized - 360 : normalized);
}

static bool jump_pad(qa_q3_game *game, qa_q3_map_actor_state *state,
                     qa_actor_id actor, qa_error *error) {
    qa_actor_id source = state->actor;
    qa_vec3 velocity = state->launch_velocity;
    int32_t number = q3_entity_number(game, source);
    q3_actor *player = NULL;
    if (!q3_map_player_launchable(game, actor, &player))
        return true;
    if (!q3_map_get(game, source))
        return true;
    if (!player || player->state.player.jumppad_entity != number) {
        double pitch = jump_pad_pitch(velocity);
        if (!q3_player_event(game, actor, 13, pitch < 45 ? 0 : 1, error))
            return false;
        if (!q3_map_get(game, source))
            return true;
        player = q3_actor_get(game, actor);
        if (player && player->kind != Q3_ACTOR_PLAYER)
            return true;
        if (!player && !qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
    }
    if (player) {
        player->state.player.jumppad_entity = number;
        player->state.player.jumppad_frame = player->state.player.pmove_frame_count;
    }
    return q3_map_set_velocity(game, actor, velocity, error);
}

static bool teleport_touch(qa_q3_game *game, qa_q3_map_actor_state *state,
                           qa_actor_id actor, qa_error *error) {
    qa_actor_id source = state->actor;
    uint32_t spawnflags = state->spawnflags;
    qa_string_id target = state->target;
    q3_actor *player = q3_actor_get(game, actor);
    bool spectator = false;
    if (player && player->kind == Q3_ACTOR_PLAYER) {
        if (player->state.player.dead)
            return true;
        spectator = player->state.player.spectator;
    } else {
        qa_builtin_actor_traits traits = {0};
        if (!game->options.services.actor_traits ||
            !game->options.services.actor_traits(game->options.services.context, actor,
                                                  &traits) ||
            !traits.player)
            return true;
        spectator = traits.spectator;
        if (!spectator) {
            qa_combat_state combat;
            qa_error ignored = {0};
            if (!qa_combat_read(game->options.services.combat, actor, &combat, &ignored) ||
                combat.health <= 0)
                return true;
        }
    }
    if (!q3_map_get(game, source))
        return true;
    if ((spawnflags & 1u) && !spectator)
        return true;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    qa_actor_id destination;
    if (!q3_map_pick(game, target, &destination, error) || !destination.registry)
        return true;
    if (!q3_map_get(game, source))
        return true;
    qa_vec3 origin, angles;
    if (!q3_map_target_pose(game, destination, &origin, &angles, error))
        return false;
    if (!q3_map_get(game, source) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), destination))
        return true;
    return qa_q3_teleport(game, actor, origin, angles, error);
}

static bool hurt_touch(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_actor_id actor, qa_error *error) {
    qa_actor_id source = state->actor;
    qa_combat_state combat;
    qa_error local = {0};
    if (!qa_combat_read(game->options.services.combat, actor, &combat, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = local;
        return false;
    }
    state = q3_map_get(game, source);
    if (!state || state->kind != QA_Q3_MAP_TRIGGER_HURT)
        return true;
    if (!combat.can_take_damage)
        return true;
    if (q3_sub_time(state->cooldown_ms, game->now_ms) > 0)
        return true;
    uint32_t flags = state->spawnflags;
    int32_t damage = state->damage;
    state->cooldown_ms = q3_add_time(game->now_ms, (flags & 16u) ? 1000 : 100);
    if (!(flags & 4u)) {
        qa_body_state body;
        int32_t sound_index = state->noise_index;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
        qa_actor_id temporary;
        if (!q3_wire_temp_entity(game, body.origin, 45, &temporary, error))
            return false;
        qa_q3_entity *event = q3_wire_temporary(game, temporary);
        if (!event)
            return q3_map_fail(error, "Q3 hurt sound lost its temporary entity");
        event->eventParm = sound_index;
        if (!q3_map_get(game, source) ||
            !qa_actors_get(qa_session_actors(game->options.services.session), actor))
            return true;
        if (!q3_sound_report(game, actor, "sound/world/electro.wav", 3, error))
            return false;
    }
    if (!q3_map_get(game, source) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    return q3_damage(game, actor, source, source, QA_Q3_W_NONE, 22,
                     (flags & 8u) ? 8u : 0u, (float)damage, qa_v3(0, 0, 0),
                     qa_v3(0, 0, 0), false, NULL, error);
}

bool q3_map_trigger_touch(qa_q3_game *game, qa_q3_map_actor_state *state,
                          const qa_touch_contact *contact, qa_error *error) {
    switch (state->kind) {
    case QA_Q3_MAP_TRIGGER_MULTIPLE: {
        qa_actor_id source = state->actor;
        if (!q3_map_is_player(game, contact->other))
            return true;
        state = q3_map_get(game, source);
        return !state || state->kind != QA_Q3_MAP_TRIGGER_MULTIPLE
                   ? true
                   : trigger_multiple(game, state, contact->other, error);
    }
    case QA_Q3_MAP_TRIGGER_PUSH:
        return jump_pad(game, state, contact->other, error);
    case QA_Q3_MAP_TRIGGER_TELEPORT:
        return teleport_touch(game, state, contact->other, error);
    case QA_Q3_MAP_TRIGGER_HURT:
        return hurt_touch(game, state, contact->other, error);
    default:
        return true;
    }
}

bool q3_map_trigger_think(qa_q3_game *game, qa_q3_map_actor_state *state,
                          qa_error *error) {
    switch (state->think) {
    case QA_Q3_MAP_THINK_FREE:
        return qa_session_release(game->options.services.session, state->actor, error);
    case QA_Q3_MAP_THINK_MULTI_READY:
        state->due_ms = 0;
        return true;
    case QA_Q3_MAP_THINK_ALWAYS: {
        qa_actor_id actor = state->actor;
        if (!q3_map_use_targets(game, state, actor, error))
            return false;
        return !q3_map_get(game, actor) ||
               qa_session_release(game->options.services.session, actor, error);
    }
    case QA_Q3_MAP_THINK_TIMER:
        return timer(game, state, error);
    case QA_Q3_MAP_THINK_AIM: {
        qa_linked_body linked;
        if (!qa_world_linked(game->options.services.world, state->actor, &linked))
            return q3_map_fail(error, "Q3 trigger_push is not linked");
        qa_vec3 origin = qa_vec_scale(
            qa_vec_add(linked.absolute_bounds.mins, linked.absolute_bounds.maxs), 0.5f);
        return q3_map_aim(game, state, origin, error);
    }
    default:
        return true;
    }
}
