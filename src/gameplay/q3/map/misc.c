#include "internal.h"

static bool portal_event(qa_q3_game *game, qa_q3_map_event_kind kind,
                         qa_q3_map_actor_state *state, qa_actor_id other,
                         qa_vec3 direction, qa_vec3 destination, qa_error *error) {
    return q3_map_emit(game, &(qa_q3_map_event){.kind = kind,
                                                .actor = state->actor,
                                                .other = other,
                                                .origin = state->origin,
                                                .angles = state->angles,
                                                .direction = direction,
                                                .destination = destination,
                                                .value = state->roll,
                                                .index = state->count,
                                                .flags = state->spawnflags},
                       error);
}

bool q3_map_spawn_misc(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_error *error) {
    const char *name = q3_map_cstr(game, state->classname);
    if (!name)
        return q3_map_fail(error, "missing Q3 misc classname");
    if (!strcmp(name, "misc_portal_surface"))
        state->kind = QA_Q3_MAP_PORTAL_SURFACE;
    else if (!strcmp(name, "misc_portal_camera")) {
        state->kind = QA_Q3_MAP_PORTAL_CAMERA;
        state->count = q3_map_float_to_int(state->roll / 360.0f * 256.0f);
    } else if (!strncmp(name, "shooter_", 8)) {
        state->kind = QA_Q3_MAP_SHOOTER;
        state->usable = true;
        if (!strcmp(name, "shooter_rocket"))
            state->count = QA_Q3_W_ROCKET;
        else if (!strcmp(name, "shooter_plasma"))
            state->count = QA_Q3_W_PLASMA;
        else if (!strcmp(name, "shooter_grenade"))
            state->count = QA_Q3_W_GRENADE;
        else
            return q3_map_fail(error, "unsupported Q3 map shooter");
        size_t item_count;
        const qa_q3_item *items = qa_q3_items(game->options.product, &item_count);
        uint32_t item_index = 0;
        while (++item_index < item_count)
            if (items[item_index].kind == QA_Q3_ITEM_WEAPON &&
                items[item_index].tag == state->count)
                break;
        if (item_index == item_count || !q3_map_register_item(game, item_index, error))
            return false;
        state->direction = q3_map_direction(state->angles);
        state->angles = qa_v3(0, 0, 0);
        if (state->random == 0)
            state->random = 1;
        float radians = q3_source_float_divide(q3_source_float_multiply(Q3_PI, state->random), 180.0f);
        state->random = (float)sin((double)radians);
    } else
        return q3_map_fail(error, "unsupported Q3 misc classname");
    if (!q3_map_allocate(game, state, NULL, true, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return q3_map_fail(error, "missing Q3 misc state");
    qa_actor_id actor = stored->actor;
    stored->linked = true;
    if (stored->kind == QA_Q3_MAP_PORTAL_SURFACE) {
        if (q3_map_text(game, stored->target))
            q3_map_schedule(game, stored, 100, QA_Q3_MAP_THINK_PORTAL);
        else if (!portal_event(game, QA_Q3_MAP_PORTAL_SURFACE, stored,
                               (qa_actor_id){0}, qa_v3(0, 0, 0), stored->origin, error))
            return q3_rollback_spawn(game, actor, error);
    } else if (stored->kind == QA_Q3_MAP_PORTAL_CAMERA) {
        if (!portal_event(game, QA_Q3_MAP_PORTAL_CAMERA, stored,
                          (qa_actor_id){0}, q3_map_direction(stored->angles), stored->origin,
                          error))
            return q3_rollback_spawn(game, actor, error);
    } else if (q3_map_text(game, stored->target))
        q3_map_schedule(game, stored, 500, QA_Q3_MAP_THINK_SHOOTER);
    stored = q3_map_get(game, actor);
    if (!stored) {
        state->actor = (qa_actor_id){0};
        return true;
    }
    *state = *stored;
    return true;
}

static qa_vec3 shooter_normalize(qa_vec3 value) {
    float length = (float)sqrt((double)qa_vec_dot(value, value));
    return length == 0 ? value : qa_vec_scale(value, q3_source_float_divide(1, length));
}
static qa_vec3 perpendicular(qa_vec3 direction) {
    qa_vec3 axis = qa_v3(1, 0, 0);
    float minimum = 1;
    if (fabsf(direction.x) < minimum) minimum = fabsf(direction.x);
    if (fabsf(direction.y) < minimum) {
        minimum = fabsf(direction.y);
        axis = qa_v3(0, 1, 0);
    }
    if (fabsf(direction.z) < minimum) axis = qa_v3(0, 0, 1);
    float inverse = q3_source_float_divide(1, qa_vec_dot(direction, direction));
    float distance = q3_source_float_multiply(qa_vec_dot(axis, direction), inverse);
    qa_vec3 normal = qa_vec_scale(direction, inverse);
    return shooter_normalize(qa_vec_sub(axis, qa_vec_scale(normal, distance)));
}

static bool use_shooter(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_error *error) {
    qa_actor_id actor = state->actor;
    qa_vec3 origin = state->origin;
    qa_vec3 direction = state->direction;
    qa_q3_weapon weapon = (qa_q3_weapon)state->count;
    float spread = state->random;
    if (state->enemy.registry) {
        qa_body_state enemy;
        qa_error local = {0};
        if (qa_world_body_read(game->options.services.world, state->enemy, &enemy, &local))
            direction = shooter_normalize(qa_vec_sub(enemy.origin, origin));
        else if (local.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = local;
            return false;
        }
        if (!q3_map_get(game, actor))
            return true;
    }
    qa_vec3 up = perpendicular(direction);
    qa_vec3 right = qa_vec_cross(up, direction);
    direction = qa_vec_add(direction, qa_vec_scale(up, q3_source_float_multiply(q3_crandom(game), spread)));
    direction = shooter_normalize(
        qa_vec_add(direction, qa_vec_scale(right, q3_source_float_multiply(q3_crandom(game), spread))));
    if (!q3_launch(game, actor, weapon, origin, direction, right, up, 1, NULL, error))
        return false;
    return !q3_map_get(game, actor) ||
           q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_SHOT, 23, 0, origin,
                    direction, qa_v3(0, 0, 0), error);
}

bool q3_map_misc_use(qa_q3_game *game, qa_q3_map_actor_state *state,
                     qa_actor_id other, qa_actor_id activator, qa_error *error) {
    (void)other;
    (void)activator;
    return state->kind == QA_Q3_MAP_SHOOTER ? use_shooter(game, state, error) : true;
}

static bool locate_portal(qa_q3_game *game, qa_q3_map_actor_state *state,
                          qa_error *error) {
    qa_actor_id surface = state->actor;
    qa_vec3 surface_origin = state->origin;
    qa_actor_id camera;
    if (!q3_map_pick(game, state->target, &camera, error))
        return false;
    if (!camera.registry)
        return qa_session_release(game->options.services.session, surface, error);
    qa_vec3 camera_origin, camera_angles;
    if (!q3_map_target_pose(game, camera, &camera_origin, &camera_angles, error))
        return false;
    if (!q3_map_get(game, surface))
        return true;
    const qa_q3_map_actor_state *camera_state = q3_map_const(game, camera);
    qa_vec3 direction = q3_map_direction(camera_angles);
    uint32_t camera_flags = camera_state ? camera_state->spawnflags : 0;
    float roll = camera_state ? camera_state->roll : 0;
    qa_string_id camera_target = camera_state ? camera_state->target : 0;
    qa_target_field field;
    if (!camera_state) {
        if (qa_targets_field(game->map->options.targets, camera, "spawnflags", &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && isfinite(field.value.number) &&
            field.value.number >= (double)INT32_MIN &&
            field.value.number < 2147483648.0)
            camera_flags = (uint32_t)(int32_t)field.value.number;
        if (qa_targets_field(game->map->options.targets, camera, "roll", &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && isfinite(field.value.number) &&
            field.value.number >= -FLT_MAX && field.value.number <= FLT_MAX)
            roll = (float)field.value.number;
        if (qa_targets_field(game->map->options.targets, camera, "target", &field) &&
            field.kind == QA_TARGET_FIELD_TEXT)
            camera_target = field.value.text;
    }
    int32_t camera_roll = q3_map_float_to_int(roll / 360.0f * 256.0f);
    qa_actor_id look = {0};
    if (q3_map_text(game, camera_target)) {
        if (!q3_map_pick(game, camera_target, &look, error))
            return false;
        qa_body_state look_body;
        if (look.registry) {
            qa_error local = {0};
            if (qa_world_body_read(game->options.services.world, look, &look_body, &local))
                direction = qa_vec_normalize(qa_vec_sub(look_body.origin, camera_origin));
            else if (local.code != QA_ERROR_NOT_FOUND) {
                if (error)
                    *error = local;
                return false;
            }
        }
    }
    state = q3_map_get(game, surface);
    if (!state)
        return true;
    state->origin = surface_origin;
    state->spawnflags = camera_flags;
    state->count = camera_roll;
    state->roll = roll;
    state->think = QA_Q3_MAP_THINK_NONE;
    state->due_ms = 0;
    return portal_event(game, QA_Q3_MAP_PORTAL_SURFACE, state, camera, direction,
                        camera_origin, error);
}

bool q3_map_misc_think(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_error *error) {
    switch (state->think) {
    case QA_Q3_MAP_THINK_PORTAL:
        return locate_portal(game, state, error);
    case QA_Q3_MAP_THINK_SHOOTER: {
        qa_actor_id actor = state->actor;
        qa_string_id target = state->target;
        qa_actor_id enemy;
        if (!q3_map_pick(game, target, &enemy, error))
            return false;
        state = q3_map_get(game, actor);
        if (!state)
            return true;
        state->enemy = enemy;
        state->think = QA_Q3_MAP_THINK_NONE;
        state->due_ms = 0;
        return true;
    }
    default:
        return true;
    }
}
