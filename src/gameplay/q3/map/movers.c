#include "internal.h"
#include "qa/game_q3_save.h"
#include "qa/persistence_gameplay.h"

static float component(qa_vec3 value, unsigned axis) {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}

static qa_vec3 with_component(qa_vec3 value, unsigned axis, float amount) {
    if (axis == 0)
        value.x = amount;
    else if (axis == 1)
        value.y = amount;
    else
        value.z = amount;
    return value;
}

static int32_t source_milliseconds(float seconds) {
    return q3_map_float_to_int(seconds * 1000.0f);
}

static qa_vec3 vector_angles(qa_vec3 direction) {
    float horizontal = sqrtf(direction.x * direction.x + direction.y * direction.y);
    if (horizontal == 0)
        return qa_v3(direction.z > 0 ? -90.0f : 90.0f, 0, 0);
    return qa_v3(-atan2f(direction.z, horizontal) * (180.0f / Q3_PI),
                 atan2f(direction.y, direction.x) * (180.0f / Q3_PI), 0);
}

static bool player_spectator(qa_q3_game *game, qa_actor_id actor) {
    q3_actor *player = q3_actor_get(game, actor);
    if (player && player->kind == Q3_ACTOR_PLAYER)
        return player->state.player.spectator;
    qa_builtin_actor_traits traits = {0};
    return game->options.services.actor_traits &&
           game->options.services.actor_traits(game->options.services.context, actor,
                                                &traits) &&
           traits.player && traits.spectator;
}

static bool living_player(qa_q3_game *game, qa_actor_id actor) {
    q3_actor *player = q3_actor_get(game, actor);
    if (player && player->kind == Q3_ACTOR_PLAYER)
        return !player->state.player.dead;
    qa_builtin_actor_traits traits = {0};
    if (!game->options.services.actor_traits ||
        !game->options.services.actor_traits(game->options.services.context, actor,
                                              &traits) ||
        !traits.player)
        return false;
    qa_combat_state combat;
    qa_error ignored = {0};
    return qa_combat_read(game->options.services.combat, actor, &combat, &ignored) &&
           combat.health > 0;
}

static bool number(qa_q3_game *game, const qa_q3_map_fields *fields, const char *key,
                   double fallback, float *out, qa_error *error) {
    (void)game;
    double value;
    if (!q3_map_number(fields, key, fallback, &value, error))
        return false;
    if (!isfinite(value) || value < -FLT_MAX || value > FLT_MAX) {
        q3_map_fail(error, "Q3 mover numeric field is outside float range");
        return false;
    }
    *out = (float)value;
    return true;
}

static int32_t integer(const qa_q3_map_fields *fields, const char *key, int32_t fallback) {
    int32_t value;
    q3_map_integer(fields, key, fallback, &value);
    return value;
}

static bool mover_bounds(qa_q3_game *game, qa_q3_map_actor_state *state,
                         qa_error *error) {
    if (!state->has_inline_model)
        return q3_map_fail(error, "Q3 brush mover has no inline model");
    return qa_collision_model_bounds(qa_world_geometry(game->options.services.world),
                                     state->inline_model, &state->bounds, error);
}

static bool mover_damage_admit(void *context, const qa_damage_request *request,
                               bool *handled, qa_error *error) {
    qa_q3_game *game = context;
    *handled = false;
    qa_q3_map_actor_state *state = q3_map_get(game, request->target);
    if (!state || !state->damageable ||
        (state->kind != QA_Q3_MAP_MOVER_DOOR &&
         state->kind != QA_Q3_MAP_MOVER_BUTTON))
        return true;
    q3_actor *native = q3_actor_get(game, request->target);
    if (!native || native->kind != Q3_ACTOR_MOVER)
        return true;
    *handled = true;
    if (game->options.rules.intermission || native->state.mover.state_index != 0)
        return true;
    return qa_q3_use_mover(game, request->target, request->attack.attacker, error);
}

bool q3_map_mover_sync_admission(qa_q3_game *game,
                                  qa_q3_map_actor_state *state,
                                  qa_error *error) {
    if (!state->damageable)
        return true;
    qa_combat_admission admission = {.context = game, .admit = mover_damage_admit};
    qa_combat_admission existing;
    if (qa_persistence_combat_admission(game->options.services.combat, state->actor, &existing) &&
        existing.context == admission.context && existing.admit == admission.admit)
        return true;
    return qa_combat_set_admission(game->options.services.combat, state->actor,
                                   &admission, error);
}
bool qa_q3_game_damage_admission(qa_q3_game *game, qa_actor_id actor,
                                 qa_combat_admission *out, qa_error *error) {
    qa_q3_map_actor_state *state = game ? q3_map_get(game, actor) : NULL;
    q3_actor *native = game ? q3_actor_get(game, actor) : NULL;
    if (!state || !state->damageable || !native || native->kind != Q3_ACTOR_MOVER || !out ||
        (state->kind != QA_Q3_MAP_MOVER_DOOR && state->kind != QA_Q3_MAP_MOVER_BUTTON))
        return q3_map_fail(error, "Q3 saved damage admission has no authored mover");
    *out = (qa_combat_admission){.context = game, .admit = mover_damage_admit};
    return true;
}

static bool ensure_damageable(qa_q3_game *game, qa_q3_map_actor_state *state,
                              qa_error *error) {
    qa_actor_id actor = state->actor;
    int32_t health = state->health;
    qa_combat_state combat;
    qa_error local = {0};
    if (qa_combat_read_traits(game->options.services.combat, actor, &combat, &local)) {
        if (!qa_combat_set_health(game->options.services.combat, actor, (float)health,
                                  error))
            return false;
        if (!q3_map_get(game, actor))
            return true;
        combat.can_take_damage = true;
        if (!qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
            return false;
    } else {
        if (local.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = local;
            return false;
        }
        combat = (qa_combat_state){.health = (float)health,
                                   .mass = 100,
                                   .can_take_damage = true};
        if (!qa_combat_create_actor(game->options.services.combat, actor, &combat, error))
            return false;
    }
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->damageable = true;
    if (q3_map_mover_sync_admission(game, state, error))
        return true;
    state = q3_map_get(game, actor);
    if (state)
        state->damageable = false;
    return false;
}

static bool bind_mover(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_q3_mover_definition *definition, qa_error *error) {
    definition->loop_sound = state->noise;
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .inline_model = true,
                                    .model = state->inline_model,
                                    .contents = -1,
                                    .role = QA_COLLISION_SOLID};
    q3_wire_entity_source *wire = q3_wire_entity(game, state->actor);
    if (!wire)
        return q3_map_fail(error, "Q3 mover constructor has no source entity row");
    wire->model = (int32_t)state->inline_model;
    if (!q3_map_allocate(game, state, &collision, true, error))
        return false;
    qa_actor_id actor = state->actor;
    if (state->model2) {
        int32_t model_index;
        if (!qa_q3_model_index(game, q3_map_cstr(game, state->model2), &model_index, error))
            return q3_rollback_spawn(game, actor, error);
        wire = q3_wire_entity(game, actor);
        if (!wire) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        wire->model2 = model_index;
    }
    if (state->noise) {
        int32_t sound_index;
        if (!qa_q3_sound_index(game, q3_map_cstr(game, state->noise), &sound_index, error))
            return q3_rollback_spawn(game, actor, error);
        wire = q3_wire_entity(game, actor);
        if (!wire) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        wire->loop_sound = sound_index;
    }
    wire = q3_wire_entity(game, actor);
    uint32_t source_slot;
    if (!wire || !qa_q3_source_actor_slot(game, actor, &source_slot, error))
        return q3_rollback_spawn(game, actor, error);
    if (state->has_light || state->has_color) {
        int32_t red = q3_source_float_to_int(q3_source_float_multiply(state->color.x, 255.0f));
        int32_t green = q3_source_float_to_int(q3_source_float_multiply(state->color.y, 255.0f));
        int32_t blue = q3_source_float_to_int(q3_source_float_multiply(state->color.z, 255.0f));
        int32_t intensity = q3_source_float_to_int(q3_source_float_divide(state->light, 4.0f));
        if (red > 255) red = 255;
        if (green > 255) green = 255;
        if (blue > 255) blue = 255;
        if (intensity > 255) intensity = 255;
        uint32_t packed = (uint32_t)red | ((uint32_t)green << 8) |
            ((uint32_t)blue << 16) | ((uint32_t)intensity << 24);
        memcpy(&wire->constant_light, &packed, sizeof(packed));
    }
    wire->type = 4;
    wire->authored_angles = state->angles;
    game->source_entities[source_slot].server_flags = 128u;
    definition->team_leader = actor;
    if (!qa_q3_bind_mover(game, actor, definition, error))
        return q3_rollback_spawn(game, actor, error);
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return q3_rollback_spawn(game, actor, error);
    qa_q3_map_actor_state *stored = q3_map_get(game, actor);
    q3_actor *native = q3_actor_get(game, actor);
    if (!stored || !native || native->kind != Q3_ACTOR_MOVER) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    body.origin = definition->state.position.base;
    bool continuous = stored->kind >= QA_Q3_MAP_MOVER_STATIC &&
                      stored->kind <= QA_Q3_MAP_MOVER_PENDULUM;
    if (continuous)
        body.origin = qa_v3(0, 0, 0);
    body.bounds = stored->bounds;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return q3_rollback_spawn(game, actor, error);
    if (!q3_map_get(game, actor)) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    if (!qa_q3_wire_link(game, actor, NULL, error))
        return q3_rollback_spawn(game, actor, error);
    if (continuous) {
        stored = q3_map_get(game, actor);
        if (!stored) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return q3_rollback_spawn(game, actor, error);
        body.origin = stored->origin;
        if (stored->kind == QA_Q3_MAP_MOVER_ROTATING)
            body.angles = definition->state.angular.base;
        if (!qa_world_body_write(game->options.services.world, actor, &body, error))
            return q3_rollback_spawn(game, actor, error);
        stored = q3_map_get(game, actor);
        if (!stored) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        if (stored->kind == QA_Q3_MAP_MOVER_ROTATING &&
            !qa_q3_wire_link(game, actor, NULL, error))
            return q3_rollback_spawn(game, actor, error);
    }
    stored = q3_map_get(game, actor);
    native = q3_actor_get(game, actor);
    if (!stored || !native || native->kind != Q3_ACTOR_MOVER) {
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

static qa_q3_mover_definition binary_definition(const qa_q3_map_actor_state *state,
                                                 float wait_ms, int32_t damage,
                                                 bool crusher) {
    qa_vec3 move = qa_vec_sub(state->second, state->first);
    float distance = qa_vec_length(move);
    int32_t duration = q3_map_float_to_int(q3_source_float_divide(
        q3_source_float_multiply(distance, 1000.0f), state->speed));
    if (duration < 1)
        duration = 1;
    return (qa_q3_mover_definition){
        .state = {.kind = QA_Q3_MOVER_NATIVE_FIXED,
                  .position = {.type = QA_TRAJECTORY_STATIONARY,
                               .base = state->first,
                               .delta = qa_vec_scale(move, state->speed),
                               .duration_ms = duration},
                  .angular = {.type = QA_TRAJECTORY_STATIONARY,
                              .base = state->angles}},
        .first = state->first,
        .second = state->second,
        .wait_ms = wait_ms,
        .damage = damage,
        .target = state->target,
        .crusher = crusher};
}

static bool spawn_trigger(qa_q3_game *game, qa_q3_map_kind kind, qa_actor_id parent,
                          qa_bounds bounds, int32_t axis, qa_error *error) {
    qa_string_id classname;
    const char *name = kind == QA_Q3_MAP_MOVER_DOOR_TRIGGER ? "door_trigger" : "plat_trigger";
    if (!q3_map_intern_cstr(game, name, &classname, error))
        return false;
    qa_q3_map_actor_state trigger = {.kind = kind,
                                     .classname = classname,
                                     .parent = parent,
                                     .bounds = bounds,
                                     .count = axis,
                                     .active = true,
                                     .touchable = true};
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q3_CONTENTS_TRIGGER,
                                    .role = QA_COLLISION_TRIGGER};
    if (!q3_map_allocate(game, &trigger, &collision, true, error))
        return false;
    return q3_wire_entity_ready(game, trigger.actor, error);
}

static bool spawn_door(qa_q3_game *game, const qa_q3_map_fields *fields,
                       qa_q3_map_actor_state *state, qa_error *error) {
    float lip;
    int32_t damage = integer(fields, "dmg", 2);
    bool damageable = integer(fields, "health", 0) != 0;
    if (!number(game, fields, "lip", 8, &lip, error) || !mover_bounds(game, state, error))
        return false;
    if (state->speed == 0)
        state->speed = 400;
    if (state->wait == 0)
        state->wait = 2;
    state->wait = q3_source_float_multiply(state->wait, 1000.0f);
    state->kind = QA_Q3_MAP_MOVER_DOOR;
    state->usable = true;
    state->direction = q3_map_direction(state->angles);
    state->angles = qa_v3(0, 0, 0);
    qa_vec3 size = qa_vec_sub(state->bounds.maxs, state->bounds.mins);
    qa_vec3 absolute = qa_v3(fabsf(state->direction.x), fabsf(state->direction.y),
                             fabsf(state->direction.z));
    float distance = qa_vec_dot(absolute, size) - lip;
    state->first = state->origin;
    state->second = qa_vec_add(state->origin, qa_vec_scale(state->direction, distance));
    if (state->spawnflags & 1u) {
        qa_vec3 swap = state->first;
        state->first = state->second;
        state->second = swap;
    }
    qa_q3_mover_definition mover = binary_definition(
        state, state->wait, damage,
        (state->spawnflags & 4u) != 0);
    mover.blocked = QA_Q3_MOVER_BLOCKED_DOOR;
    if (!bind_mover(game, state, &mover, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return true;
    qa_actor_id actor = stored->actor;
    q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_MOVER_DOOR_SETUP);
    if (damageable && !ensure_damageable(game, stored, error))
        return q3_rollback_spawn(game, actor, error);
    stored = q3_map_get(game, actor);
    if (!stored) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    *state = *stored;
    return true;
}

static bool spawn_plat(qa_q3_game *game, const qa_q3_map_fields *fields,
                       qa_q3_map_actor_state *state, qa_error *error) {
    float speed, lip, height;
    int32_t damage = integer(fields, "dmg", 2);
    qa_bytes ignored;
    bool has_height = q3_map_property(fields, "height", &ignored);
    if (!number(game, fields, "speed", 200, &speed, error) ||
        !number(game, fields, "lip", 8, &lip, error) ||
        !number(game, fields, "height", 0, &height, error) || !mover_bounds(game, state, error))
        return false;
    if (speed == 0)
        speed = 100;
    state->kind = QA_Q3_MAP_MOVER_PLAT;
    state->usable = true;
    state->touchable = true;
    state->speed = speed;
    state->damage = damage;
    state->wait = 1000.0f;
    state->angles = qa_v3(0, 0, 0);
    float distance = has_height ? height : state->bounds.maxs.z - state->bounds.mins.z - lip;
    state->second = state->origin;
    state->first = qa_v3(state->origin.x, state->origin.y, state->origin.z - distance);
    qa_q3_mover_definition mover = binary_definition(state, state->wait, state->damage,
                                                      (state->spawnflags & 4u) != 0);
    mover.blocked = QA_Q3_MOVER_BLOCKED_DOOR;
    if (!bind_mover(game, state, &mover, error))
        return false;
    if (!state->actor.registry || !q3_map_get(game, state->actor))
        return true;
    if (!q3_map_text(game, state->targetname)) {
        qa_bounds bounds = {
            .mins = qa_vec_add(qa_vec_add(state->first, state->bounds.mins), qa_v3(33, 33, 0)),
            .maxs = qa_vec_add(qa_vec_add(state->first, state->bounds.maxs), qa_v3(-33, -33, 8))};
        for (unsigned axis = 0; axis < 2; ++axis) {
            if (component(bounds.maxs, axis) > component(bounds.mins, axis))
                continue;
            float center = component(state->first, axis) +
                           (component(state->bounds.mins, axis) +
                            component(state->bounds.maxs, axis)) * 0.5f;
            bounds.mins = with_component(bounds.mins, axis, center);
            bounds.maxs = with_component(bounds.maxs, axis, center + 1);
        }
        if (!spawn_trigger(game, QA_Q3_MAP_MOVER_PLAT_TRIGGER, state->actor, bounds, 0,
                           error))
            return q3_rollback_spawn(game, state->actor, error);
    }
    return true;
}

static bool spawn_button(qa_q3_game *game, const qa_q3_map_fields *fields,
                         qa_q3_map_actor_state *state, qa_error *error) {
    float lip;
    if (!number(game, fields, "lip", 4, &lip, error) || !mover_bounds(game, state, error))
        return false;
    if (state->speed == 0)
        state->speed = 40;
    if (state->wait == 0)
        state->wait = 1;
    state->wait = q3_source_float_multiply(state->wait, 1000.0f);
    state->kind = QA_Q3_MAP_MOVER_BUTTON;
    state->usable = true;
    state->touchable = state->health == 0;
    state->direction = q3_map_direction(state->angles);
    state->angles = qa_v3(0, 0, 0);
    qa_vec3 size = qa_vec_sub(state->bounds.maxs, state->bounds.mins);
    qa_vec3 absolute = qa_v3(fabsf(state->direction.x), fabsf(state->direction.y),
                             fabsf(state->direction.z));
    float distance = qa_vec_dot(absolute, size) - lip;
    state->first = state->origin;
    state->second = qa_vec_add(state->origin, qa_vec_scale(state->direction, distance));
    qa_q3_mover_definition mover = binary_definition(
        state, state->wait, state->damage,
        (state->spawnflags & 4u) != 0);
    if (!bind_mover(game, state, &mover, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return true;
    qa_actor_id actor = stored->actor;
    if (stored->health && !ensure_damageable(game, stored, error))
        return q3_rollback_spawn(game, actor, error);
    stored = q3_map_get(game, actor);
    if (!stored) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    *state = *stored;
    return true;
}

static bool spawn_train(qa_q3_game *game, const qa_q3_map_fields *fields,
                        qa_q3_map_actor_state *state, qa_error *error) {
    (void)fields;
    if (!q3_map_text(game, state->target)) {
        q3_map_warn(game, (qa_actor_id){0}, "Q3 func_train has no target");
        if (!qa_session_release(game->options.services.session, state->actor, error))
            return false;
        state->actor = (qa_actor_id){0};
        return true;
    }
    if (!mover_bounds(game, state, error))
        return false;
    if (state->speed == 0)
        state->speed = 100;
    state->kind = QA_Q3_MAP_MOVER_TRAIN;
    state->usable = true;
    state->angles = qa_v3(0, 0, 0);
    state->damage = (state->spawnflags & 4u) ? 0 : state->damage ? state->damage : 2;
    state->first = state->second = qa_v3(0, 0, 0);
    qa_q3_mover_definition mover = binary_definition(state, state->wait, state->damage,
                                                      (state->spawnflags & 4u) != 0);
    mover.map_controlled = true;
    if (!bind_mover(game, state, &mover, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (stored)
        q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_MOVER_TRAIN_SETUP);
    return true;
}

static bool spawn_path_corner(qa_q3_game *game, qa_q3_map_actor_state *state,
                              qa_error *error) {
    if (!q3_map_text(game, state->targetname)) {
        q3_map_warn(game, (qa_actor_id){0}, "Q3 path_corner has no targetname");
        if (!qa_session_release(game->options.services.session, state->actor, error))
            return false;
        state->actor = (qa_actor_id){0};
        return true;
    }
    state->kind = QA_Q3_MAP_PATH_CORNER;
    if (!q3_map_allocate(game, state, NULL, false, error))
        return false;
    return q3_wire_entity_ready(game, state->actor, error);
}

static bool spawn_continuous(qa_q3_game *game, const qa_q3_map_fields *fields,
                             qa_q3_map_actor_state *state, qa_q3_map_kind kind,
                             qa_error *error) {
    if (!mover_bounds(game, state, error))
        return false;
    state->kind = kind;
    state->usable = true;
    state->first = state->second = qa_v3(0, 0, 0);
    if (state->speed == 0)
        state->speed = 100;
    qa_q3_mover_definition mover = binary_definition(state, state->wait, state->damage, false);
    mover.state.position.base = state->origin;
    mover.state.angular = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY,
                                          .base = qa_v3(0, 0, 0)};
    if (kind == QA_Q3_MAP_MOVER_ROTATING) {
        unsigned axis = (state->spawnflags & 4u) ? 2 : (state->spawnflags & 8u) ? 0 : 1;
        mover.state.angular.type = QA_TRAJECTORY_LINEAR;
        mover.state.angular.delta = with_component(qa_v3(0, 0, 0), axis, state->speed);
        if (!state->damage)
            state->damage = 2;
        mover.damage = state->damage;
    } else if (kind == QA_Q3_MAP_MOVER_BOBBING) {
        float speed, height, phase;
        int32_t damage = integer(fields, "dmg", 2);
        if (!number(game, fields, "speed", 4, &speed, error) ||
            !number(game, fields, "height", 32, &height, error) ||
            !number(game, fields, "phase", 0, &phase, error))
            return false;
        if (speed == 0)
            speed = 100;
        int32_t duration = source_milliseconds(speed);
        unsigned axis = (state->spawnflags & 1u) ? 0 : (state->spawnflags & 2u) ? 1 : 2;
        mover.state.position = (qa_trajectory){
            .type = QA_TRAJECTORY_SINE,
            .base = state->origin,
            .delta = with_component(qa_v3(0, 0, 0), axis, height),
            .time_ms = q3_map_float_to_int((float)duration * phase),
            .duration_ms = duration};
        state->speed = speed;
        state->damage = mover.damage = damage;
    } else if (kind == QA_Q3_MAP_MOVER_PENDULUM) {
        float speed, phase;
        int32_t damage = integer(fields, "dmg", 2);
        if (!number(game, fields, "speed", 30, &speed, error) ||
            !number(game, fields, "phase", 0, &phase, error))
            return false;
        float length = fmaxf(8, fabsf(state->bounds.mins.z));
        float frequency = (1.0f / (Q3_PI * 2.0f)) *
                          sqrtf(game->options.rules.gravity / (3.0f * length));
        int32_t duration = q3_map_float_to_int(1000.0f / frequency);
        mover.state.angular = (qa_trajectory){
            .type = QA_TRAJECTORY_SINE,
            .base = state->angles,
            .delta = qa_v3(0, 0, speed),
            .time_ms = q3_map_float_to_int((float)duration * phase),
            .duration_ms = duration};
        state->damage = mover.damage = damage;
    }
    return bind_mover(game, state, &mover, error);
}

static bool presentation_fields(qa_q3_game *game, const qa_q3_map_fields *fields,
                                qa_q3_map_actor_state *state, qa_error *error) {
    qa_bytes value;
    state->has_light = q3_map_property(fields, "light", &value);
    state->has_color = q3_map_property(fields, "color", &value);
    double light;
    if (!q3_map_number(fields, "light", 100, &light, error) ||
        !q3_map_vector(fields, "color", qa_v3(1, 1, 1), &state->color, error))
        return false;
    if (!isfinite(light) || light < -FLT_MAX || light > FLT_MAX)
        return q3_map_fail(error, "Q3 mover light is outside float range");
    state->light = (float)light;
    (void)game;
    return true;
}

bool q3_map_spawn_mover(qa_q3_game *game, const qa_q3_map_fields *fields,
                        qa_q3_map_actor_state *state, qa_error *error) {
    const char *name = q3_map_cstr(game, state->classname);
    if (!name)
        return q3_map_fail(error, "missing Q3 mover classname");
    if (!presentation_fields(game, fields, state, error))
        return false;
    const char *start = NULL, *end = NULL;
    if (!strcmp(name, "func_door")) {
        start = "sound/movers/doors/dr1_strt.wav";
        end = "sound/movers/doors/dr1_end.wav";
    } else if (!strcmp(name, "func_plat")) {
        start = "sound/movers/plats/pt1_strt.wav";
        end = "sound/movers/plats/pt1_end.wav";
    } else if (!strcmp(name, "func_button"))
        start = "sound/movers/switches/butn2.wav";
    if (start && !qa_q3_sound_index(game, start, &state->sound_1_to_2, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), state->actor)) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    if (end) {
        state->sound_2_to_1 = state->sound_1_to_2;
        if (!qa_q3_sound_index(game, end, &state->sound_pos_1, error))
            return false;
        if (!qa_actors_get(qa_session_actors(game->options.services.session), state->actor)) {
            state->actor = (qa_actor_id){0};
            return true;
        }
        state->sound_pos_2 = state->sound_pos_1;
    }
    if (!strcmp(name, "func_door"))
        return spawn_door(game, fields, state, error);
    if (!strcmp(name, "func_plat"))
        return spawn_plat(game, fields, state, error);
    if (!strcmp(name, "func_button"))
        return spawn_button(game, fields, state, error);
    if (!strcmp(name, "func_train"))
        return spawn_train(game, fields, state, error);
    if (!strcmp(name, "path_corner"))
        return spawn_path_corner(game, state, error);
    if (!strcmp(name, "func_static"))
        return spawn_continuous(game, fields, state, QA_Q3_MAP_MOVER_STATIC, error);
    if (!strcmp(name, "func_rotating"))
        return spawn_continuous(game, fields, state, QA_Q3_MAP_MOVER_ROTATING, error);
    if (!strcmp(name, "func_bobbing"))
        return spawn_continuous(game, fields, state, QA_Q3_MAP_MOVER_BOBBING, error);
    if (!strcmp(name, "func_pendulum"))
        return spawn_continuous(game, fields, state, QA_Q3_MAP_MOVER_PENDULUM, error);
    return q3_map_fail(error, "unsupported Q3 mover classname");
}

bool q3_map_mover_sync_state(qa_q3_game *game, qa_q3_map_actor_state *state,
                             qa_error *error) {
    q3_actor *native = q3_actor_get(game, state->actor);
    if (!native || native->kind != Q3_ACTOR_MOVER)
        return q3_map_fail(error, "Q3 authored mover lost its native state");
    native->state.mover.state.team_next = state->team_next;
    native->state.mover.state.team_slave = state->team_slave;
    native->state.mover.team_leader = state->team_master.registry
                                          ? state->team_master
                                          : state->actor;
    native->state.mover.target = state->target;
    return true;
}

bool q3_map_mover_post_spawn(qa_q3_game *game, qa_error *error) {
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        qa_q3_map_actor_state *state = &game->map->actors[i];
        if (!state->active || state->kind < QA_Q3_MAP_MOVER_DOOR ||
            state->kind > QA_Q3_MAP_MOVER_PENDULUM)
            continue;
        if (!q3_map_mover_sync_state(game, state, error))
            return false;
    }
    return true;
}

static bool mover_sound(qa_q3_game *game, qa_q3_map_actor_state *state,
                        int32_t before, int32_t after, qa_error *error) {
    bool start = after == 2 || after == 3;
    int32_t sound = after == 2 ? state->sound_1_to_2 : after == 3 ? state->sound_2_to_1
        : after == 1 ? state->sound_pos_2 : state->sound_pos_1;
    q3_wire_entity_source *wire = q3_wire_entity(game, state->actor);
    if (!wire)
        return q3_map_fail(error, "Q3 mover transition lost its source sound fields");
    if (!start || before == 0 || before == 1)
        wire->loop_sound = state->sound_loop;
    q3_actor *native = q3_actor_get(game, state->actor);
    if (native && native->kind == Q3_ACTOR_MOVER && !wire->loop_sound)
        native->state.mover.loop_sound = 0;
    if (!sound)
        return true;
    if (!q3_wire_add_event(game, state->actor, 45, sound, error))
        return false;
    const char *path = NULL;
    if (state->kind == QA_Q3_MAP_MOVER_DOOR)
        path = start ? "sound/movers/doors/dr1_strt.wav" : "sound/movers/doors/dr1_end.wav";
    else if (state->kind == QA_Q3_MAP_MOVER_PLAT)
        path = start ? "sound/movers/plats/pt1_strt.wav" : "sound/movers/plats/pt1_end.wav";
    else if (state->kind == QA_Q3_MAP_MOVER_BUTTON && start)
        path = "sound/movers/switches/butn2.wav";
    return !path || q3_sound_report(game, state->actor, path, 3, error);
}

static bool area_portal(qa_q3_game *game, qa_q3_map_actor_state *state,
                        bool open, qa_error *error) {
    if (state->team_master.registry &&
        !qa_actor_id_equal(state->team_master, state->actor))
        return true;
    return q3_map_emit(game, &(qa_q3_map_event){.kind = QA_Q3_MAP_AREA_PORTAL,
                                                .actor = state->actor,
                                                .value = open ? 1 : 0},
                       error);
}

bool q3_map_mover_used(qa_q3_game *game, qa_actor_id actor, int32_t before,
                       int32_t after, qa_error *error) {
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state || before == after)
        return true;
    if (!mover_sound(game, state, before, after, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (before == 0 && after == 2)
        return area_portal(game, state, true, error);
    return after != 0 || area_portal(game, state, false, error);
}

static bool door_setup(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_error *error) {
    qa_actor_id leader = state->actor;
    state->due_ms = 0;
    if (state->team_slave)
        return true;
    q3_actor *native = q3_actor_get(game, leader);
    if (!native || native->kind != Q3_ACTOR_MOVER)
        return true;
    int32_t mover_state = native->state.mover.state_index;
    if (q3_map_text(game, state->targetname) || state->health)
        return q3_mover_match_team(game, leader, mover_state, game->now_ms, error);
    qa_bounds bounds = {0};
    bool first = true;
    qa_actor_id part = leader;
    for (uint32_t visited = 0; part.registry; ++visited) {
        if (visited >= game->map->capacity)
            return q3_map_fail(error, "cyclic Q3 door team");
        qa_q3_map_actor_state *member = q3_map_get(game, part);
        q3_actor *mover = q3_actor_get(game, part);
        if (!member || !mover || mover->kind != Q3_ACTOR_MOVER)
            return q3_map_fail(error, "broken Q3 door team");
        qa_actor_id next = mover->state.mover.state.team_next;
        qa_linked_body linked;
        if (!qa_world_linked(game->options.services.world, part, &linked))
            return q3_map_fail(error, "unlinked Q3 door team member");
        if (first) {
            bounds = linked.absolute_bounds;
            first = false;
        } else {
            bounds.mins = qa_v3(fminf(bounds.mins.x, linked.absolute_bounds.mins.x),
                                fminf(bounds.mins.y, linked.absolute_bounds.mins.y),
                                fminf(bounds.mins.z, linked.absolute_bounds.mins.z));
            bounds.maxs = qa_v3(fmaxf(bounds.maxs.x, linked.absolute_bounds.maxs.x),
                                fmaxf(bounds.maxs.y, linked.absolute_bounds.maxs.y),
                                fmaxf(bounds.maxs.z, linked.absolute_bounds.maxs.z));
        }
        if (!ensure_damageable(game, member, error))
            return false;
        part = next;
    }
    if (!q3_map_get(game, leader))
        return true;
    qa_vec3 size = qa_vec_sub(bounds.maxs, bounds.mins);
    unsigned axis = 0;
    for (unsigned i = 1; i < 3; ++i)
        if (component(size, i) < component(size, axis))
            axis = i;
    bounds.mins = with_component(bounds.mins, axis, component(bounds.mins, axis) - 120);
    bounds.maxs = with_component(bounds.maxs, axis, component(bounds.maxs, axis) + 120);
    if (!spawn_trigger(game, QA_Q3_MAP_MOVER_DOOR_TRIGGER, leader, bounds,
                       (int32_t)axis, error))
        return false;
    return q3_mover_match_team(game, leader, mover_state, game->now_ms, error);
}

static bool path_for_target(qa_q3_game *game, qa_string_id target, qa_actor_id *out) {
    qa_target_cursor cursor = {0};
    qa_actor_id candidate;
    while (qa_targets_next(game->map->options.targets, target, &cursor, &candidate)) {
        qa_q3_map_actor_state *state = q3_map_get(game, candidate);
        if (state && state->kind == QA_Q3_MAP_PATH_CORNER) {
            *out = candidate;
            return true;
        }
    }
    *out = (qa_actor_id){0};
    return false;
}

static bool train_reached(qa_q3_game *game, qa_q3_map_actor_state *train,
                          qa_error *error) {
    qa_actor_id actor = train->actor;
    qa_q3_map_actor_state *next = q3_map_get(game, train->path_next);
    if (!next || !next->path_next.registry)
        return true;
    qa_actor_id next_actor = next->actor;
    if (!q3_map_use_targets(game, next, (qa_actor_id){0}, error))
        return false;
    train = q3_map_get(game, actor);
    next = q3_map_get(game, next_actor);
    if (!train || !next)
        return true;
    qa_q3_map_actor_state *destination = q3_map_get(game, next->path_next);
    if (!destination || destination->kind != QA_Q3_MAP_PATH_CORNER)
        return q3_map_fail(error, "Q3 train path was removed during target dispatch");
    q3_actor *native = q3_actor_get(game, actor);
    if (!native || native->kind != Q3_ACTOR_MOVER)
        return true;
    qa_actor_id destination_actor = destination->actor;
    int32_t loop_sound = next->sound_loop;
    train = q3_map_get(game, actor);
    next = q3_map_get(game, next_actor);
    destination = q3_map_get(game, destination_actor);
    native = q3_actor_get(game, actor);
    if (!train || !next || !destination ||
        destination->kind != QA_Q3_MAP_PATH_CORNER ||
        !qa_actor_id_equal(next->path_next, destination_actor) || next->sound_loop != loop_sound ||
        !native || native->kind != Q3_ACTOR_MOVER)
        return true;
    float speed = next->speed != 0 ? next->speed : train->speed;
    float wait = next->wait;
    if (speed < 1)
        speed = 1;
    qa_vec3 first = next->origin, second = destination->origin;
    int32_t duration = q3_map_float_to_int(qa_vec_length(qa_vec_sub(second, first)) *
                                           1000.0f / speed);
    native->state.mover.first = first;
    native->state.mover.second = second;
    native->state.mover.state.position.duration_ms = duration;
    q3_wire_entity_source *wire = q3_wire_entity(game, actor);
    if (!wire)
        return q3_map_fail(error, "Q3 train path lost its actual source loop sound");
    wire->loop_sound = loop_sound;
    if (!loop_sound)
        native->state.mover.loop_sound = 0;
    train->first = first;
    train->second = second;
    train->path_next = destination->actor;
    if (!q3_mover_set_state(game, actor, 2, game->now_ms, error))
        return false;
    train = q3_map_get(game, actor);
    native = q3_actor_get(game, actor);
    if (!train || !native || native->kind != Q3_ACTOR_MOVER)
        return true;
    if (wait != 0) {
        q3_postgame_native_think_assigned(game, actor);
        native->state.mover.state.position.type = QA_TRAJECTORY_STATIONARY;
        train->due_ms = q3_source_float_schedule(game->now_ms, wait);
        train->think = QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME;
    }
    return true;
}

static bool train_setup(qa_q3_game *game, qa_q3_map_actor_state *train,
                        qa_error *error) {
    qa_actor_id actor = train->actor;
    train->due_ms = 0;
    qa_actor_id start;
    if (!path_for_target(game, train->target, &start)) {
        q3_map_warn(game, actor, "Q3 func_train target was not found");
        return true;
    }
    train->path_next = start;
    qa_actor_id path = start;
    for (uint32_t visited = 0;; ++visited) {
        if (visited >= game->map->capacity)
            return q3_map_fail(error, "Q3 train path cycle does not return to its first corner");
        qa_q3_map_actor_state *corner = q3_map_get(game, path);
        if (!corner || corner->kind != QA_Q3_MAP_PATH_CORNER)
            return q3_map_fail(error, "Q3 train path contains a missing corner");
        if (!q3_map_text(game, corner->target)) {
            q3_map_warn(game, corner->actor, "Q3 train corner has no target");
            return true;
        }
        qa_actor_id next;
        if (!path_for_target(game, corner->target, &next)) {
            q3_map_warn(game, corner->actor, "Q3 train corner target has no path_corner");
            return true;
        }
        corner->path_next = next;
        path = next;
        if (qa_actor_id_equal(path, start))
            break;
    }
    train = q3_map_get(game, actor);
    return !train || train_reached(game, train, error);
}

bool q3_map_mover_think(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_error *error) {
    switch (state->think) {
    case QA_Q3_MAP_THINK_MOVER_DOOR_SETUP:
        return door_setup(game, state, error);
    case QA_Q3_MAP_THINK_MOVER_TRAIN_SETUP:
        return train_setup(game, state, error);
    case QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME: {
        qa_actor_id actor = state->actor;
        state->due_ms = 0;
        q3_actor *native = q3_actor_get(game, actor);
        if (!native || native->kind != Q3_ACTOR_MOVER)
            return q3_map_fail(error, "Q3 train resume lost its native trajectory");
        native->state.mover.state.position.time_ms = game->now_ms;
        native->state.mover.state.position.type = QA_TRAJECTORY_LINEAR_STOP;
        return true;
    }
    default:
        return true;
    }
}

static bool door_trigger_touch(qa_q3_game *game, qa_q3_map_actor_state *trigger,
                               qa_actor_id other, qa_error *error) {
    qa_actor_id trigger_actor = trigger->actor;
    qa_actor_id parent_actor = trigger->parent;
    bool spectator = player_spectator(game, other);
    trigger = q3_map_get(game, trigger_actor);
    qa_q3_map_actor_state *parent = q3_map_get(game, parent_actor);
    q3_actor *mover = parent ? q3_actor_get(game, parent->actor) : NULL;
    if (!trigger || !parent || !mover || mover->kind != Q3_ACTOR_MOVER)
        return true;
    if (spectator) {
        if (mover->state.mover.state_index == 2 || mover->state.mover.state_index == 1)
            return true;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, other, &body, error))
            return false;
        trigger = q3_map_get(game, trigger_actor);
        parent = q3_map_get(game, parent_actor);
        mover = parent ? q3_actor_get(game, parent->actor) : NULL;
        if (!trigger || !parent || !mover || mover->kind != Q3_ACTOR_MOVER)
            return true;
        unsigned axis = trigger->count >= 0 && trigger->count < 3 ? (unsigned)trigger->count : 0;
        float minimum = component(trigger->bounds.mins, axis);
        float maximum = component(trigger->bounds.maxs, axis);
        float position = component(body.origin, axis);
        bool toward_minimum = fabsf(position - maximum) < fabsf(position - minimum);
        qa_vec3 direction = with_component(qa_v3(0, 0, 0), axis,
                                           toward_minimum ? -1 : 1);
        qa_vec3 origin = qa_vec_scale(qa_vec_add(trigger->bounds.mins,
                                                 trigger->bounds.maxs), 0.5f);
        origin = with_component(origin, axis,
                                toward_minimum ? minimum - 10 : maximum + 10);
        return qa_q3_teleport(game, other, origin, vector_angles(direction), error);
    }
    return mover->state.mover.state_index == 2 ||
           qa_q3_use_mover(game, parent->actor, other, error);
}

bool q3_map_mover_touch(qa_q3_game *game, qa_q3_map_actor_state *state,
                        const qa_touch_contact *contact, qa_error *error) {
    if (state->kind == QA_Q3_MAP_MOVER_DOOR_TRIGGER)
        return door_trigger_touch(game, state, contact->other, error);
    qa_actor_id actor = state->actor;
    if (state->kind == QA_Q3_MAP_MOVER_PLAT_TRIGGER) {
        if (!q3_map_is_player(game, contact->other))
            return true;
        state = q3_map_get(game, actor);
        if (!state || state->kind != QA_Q3_MAP_MOVER_PLAT_TRIGGER)
            return true;
        qa_q3_map_actor_state *parent = q3_map_get(game, state->parent);
        q3_actor *mover = parent ? q3_actor_get(game, parent->actor) : NULL;
        return !parent || !mover || mover->kind != Q3_ACTOR_MOVER ||
               mover->state.mover.state_index != 0 ||
               qa_q3_use_mover(game, parent->actor, contact->other, error);
    }
    if (!q3_map_is_player(game, contact->other))
        return true;
    state = q3_map_get(game, actor);
    q3_actor *mover = q3_actor_get(game, actor);
    if (!state || !mover || mover->kind != Q3_ACTOR_MOVER)
        return true;
    if (state->kind == QA_Q3_MAP_MOVER_BUTTON && mover->state.mover.state_index == 0)
        return qa_q3_use_mover(game, actor, contact->other, error);
    if (state->kind == QA_Q3_MAP_MOVER_PLAT && mover->state.mover.state_index == 1 &&
        living_player(game, contact->other)) {
        mover = q3_actor_get(game, actor);
        state = q3_map_get(game, actor);
        if (state && state->kind == QA_Q3_MAP_MOVER_PLAT && mover &&
            mover->kind == Q3_ACTOR_MOVER && mover->state.mover.state_index == 1) {
            mover->state.mover.next_think_ms = q3_add_time(game->now_ms, 1000);
            q3_postgame_nextthink_assigned(game, actor, mover->state.mover.next_think_ms);
        }
    }
    return true;
}

bool q3_map_mover_action(qa_q3_game *game, qa_q3_mover_action action,
                         qa_actor_id actor, qa_actor_id other, int32_t now,
                         bool *handled, qa_error *error) {
    (void)other;
    (void)now;
    if (handled)
        *handled = false;
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state || state->kind != QA_Q3_MAP_MOVER_TRAIN)
        return true;
    if (handled)
        *handled = true;
    if (action == QA_Q3_MOVER_REACHED)
        return train_reached(game, state, error);
    return true;
}
