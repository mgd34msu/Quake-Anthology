#include "internal.h"

static q1_actor *platform(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && q1_map_is_rogue_plat(entity->map->kind) ? entity : NULL;
}
static bool platform_sound(qa_q1_game *g, q1_actor *entity, bool stopped, qa_error *error) {
    return q1_sound_resource(g, entity->id, entity->map->noise[stopped], 2, 1, 1, error);
}
static bool move(qa_q1_game *g, q1_actor *entity, bool up, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!platform_sound(g, entity, false, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    q1_map_movement *motion = &entity->map->pending.mover;
    motion->position = up ? Q1_MAP_UP : Q1_MAP_DOWN;
    return q1_map_move(g, entity, up ? motion->pos1 : motion->pos2,
                       up ? Q1_MAP_ROGUE_PLAT_TOP : Q1_MAP_ROGUE_PLAT_BOTTOM, error);
}
static bool elevator_go(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!platform_sound(g, entity, false, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    q1_map_movement *motion = &entity->map->pending.mover;
    motion->position = Q1_MAP_UP;
    motion->rogue.last_use = g->time;
    qa_vec3 destination = motion->pos2;
    destination.z += entity->map->height * motion->rogue.target_floor;
    return q1_map_move(g, entity, destination, Q1_MAP_ELEVATOR_STOP, error);
}
static bool button_fire(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_position position = entity->map->pending.mover.position;
    if (position == Q1_MAP_UP || position == Q1_MAP_TOP)
        return true;
    qa_actor_id id = entity->id;
    if (!platform_sound(g, entity, false, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    entity->map->pending.mover.position = Q1_MAP_UP;
    return q1_map_move(g, entity, entity->map->pending.mover.pos2, Q1_MAP_ELEVATOR_BUTTON_WAIT,
                       error);
}
bool q1_map_rogue_plat_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!entity->map->has_inline_model)
        return q1_map_fail(error, "Rogue platform has no inline brush model");
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    q1_map_state *state = entity->map;
    bool button = state->kind == Q1_MAP_ELEVATOR_BUTTON;
    if (button)
        state->movedir = q1_map_direction(body.angles);
    else
        state->mangle = body.angles;
    entity->physics.motion = QA_PHYSICS_PUSH;
    entity->physics.solid = QA_PHYSICS_BRUSH;
    body.angles = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    state = entity->map;
    q1_map_movement *motion = &state->pending.mover;
    if (button) {
        static const char *const sounds[] = {"buttons/airbut1.wav", "buttons/switch21.wav",
                                             "buttons/switch02.wav", "buttons/switch04.wav"};
        if ((unsigned)state->sounds < sizeof(sounds) / sizeof(*sounds) &&
            !qa_builtin_resource(&g->services, sounds[state->sounds], &state->noise[0], error))
            return false;
        state->use_enabled = true;
        float health = q1_health(g, id);
        entity = platform(g, id);
        if (!entity)
            return true;
        if (health != 0) {
            entity->max_health = health;
            if (!q1_map_damageable(g, entity, true, error))
                return false;
            entity = platform(g, id);
            if (!entity)
                return true;
        } else
            entity->map->touch_enabled = true;
        state = entity->map;
        motion = &state->pending.mover;
        entity->speed = entity->speed ? entity->speed : 40;
        entity->wait = entity->wait ? entity->wait : 1;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = platform(g, id);
        if (!entity)
            return true;
        state = entity->map;
        motion = &state->pending.mover;
        motion->position = Q1_MAP_BOTTOM;
        motion->pos1 = body.origin;
        motion->pos2 = qa_vec_add(
            motion->pos1,
            qa_vec_scale(
                state->movedir,
                fabsf(qa_vec_dot(state->movedir, qa_vec_sub(body.bounds.maxs, body.bounds.mins))) -
                    (state->lip ? state->lip : 4)));
    } else {
        entity->speed = entity->speed ? entity->speed : 150;
        state->sounds = state->sounds ? state->sounds : 2;
        if (state->sounds == 1 || state->sounds == 2) {
            if (!qa_builtin_resource(&g->services,
                                     state->sounds == 1 ? "plats/plat1.wav" : "plats/medplat1.wav",
                                     &state->noise[0], error) ||
                !qa_builtin_resource(&g->services,
                                     state->sounds == 1 ? "plats/plat2.wav" : "plats/medplat2.wav",
                                     &state->noise[1], error))
                return false;
        }
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = platform(g, id);
        if (!entity)
            return true;
        state = entity->map;
        motion = &state->pending.mover;
        motion->pos1 = body.origin;
        bool negative = state->height < 0;
        state->height = fabsf(state->height);
        if (state->height == 0) {
            negative = true;
            state->height = body.bounds.maxs.z - body.bounds.mins.z - 8;
        }
        motion->pos2 = qa_vec_sub(body.origin, qa_v3(0, 0, state->height));
        if (entity->spawnflags & 3) {
            state->use_enabled = true;
            motion->position = negative ? Q1_MAP_BOTTOM : Q1_MAP_TOP;
            if (negative) {
                body.origin = motion->pos2;
                if (!qa_world_body_write(g->services.world, id, &body, error))
                    return false;
                entity = platform(g, id);
                if (!entity || !q1_link(g, entity, error))
                    return entity == NULL;
                entity = platform(g, id);
                if (!entity)
                    return true;
            }
            if (entity->spawnflags & 1) {
                float health = q1_health(g, id);
                if (!platform(g, id))
                    return true;
                if (health == 0 && !qa_combat_set_health(g->services.combat, id, 5, error))
                    return false;
            }
        } else if (entity->spawnflags & 4) {
            float floors = state->counter_value;
            motion->rogue.floor = entity->spawnflags & 8 ? floors - 1 : 0;
            motion->rogue.target_floor = 0;
            motion->rogue.last_use = 0;
            if (entity->spawnflags & 8)
                motion->pos2.z = body.origin.z - state->height * (floors - 1);
            else {
                motion->pos1.z = body.origin.z + state->height * (floors - 1);
                motion->pos2 = body.origin;
            }
            state->use_enabled = true;
        } else if (entity->spawnflags & 16) {
            if (!q1_map_plat_trigger(g, entity, Q1_MAP_ROGUE_PLAT_TRIGGER, body.bounds,
                                     entity->map->height, error))
                return false;
            entity = platform(g, id);
            if (!entity)
                return true;
            state = entity->map;
            motion = &state->pending.mover;
            motion->rogue = (q1_map_rogue_platform){0};
            entity->delay = entity->delay ? entity->delay : 3;
            if (negative) {
                motion->position = Q1_MAP_BOTTOM;
                entity->spawnflags = 16;
                body.origin = motion->pos2;
                if (!qa_world_body_write(g->services.world, id, &body, error))
                    return false;
                entity = platform(g, id);
                if (!entity || !q1_link(g, entity, error))
                    return entity == NULL;
                entity = platform(g, id);
                if (!entity)
                    return true;
                state = entity->map;
                motion = &state->pending.mover;
            } else {
                entity->spawnflags |= 8;
                motion->position = Q1_MAP_TOP;
            }
            if (q1_map_text(g, entity->targetname)) {
                motion->rogue.disabled = true;
                state->use_enabled = true;
            }
        }
    }
    entity = platform(g, id);
    return !entity || q1_link(g, entity, error);
}
bool q1_map_rogue_plat_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                           qa_actor_id activator, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_movement *motion = &entity->map->pending.mover;
    if (entity->map->kind == Q1_MAP_ELEVATOR_BUTTON) {
        entity->activator = activator;
        return button_fire(g, entity, error);
    }
    if (entity->spawnflags & 3) {
        if (motion->position == Q1_MAP_TOP)
            return move(g, entity, false, error);
        return (entity->spawnflags & 1) || motion->position != Q1_MAP_BOTTOM ||
               move(g, entity, true, error);
    }
    if (!(entity->spawnflags & 4)) {
        motion->rogue.disabled = false;
        entity->map->use_enabled = false;
        return true;
    }
    if (motion->rogue.last_use + 2 > g->time)
        return true;
    motion->rogue.last_use = g->time;
    float direction = q1_alive(g, g->maps->world_actor) ? g->maps->elevator_direction : 0;
    if (direction == 0)
        return true;
    qa_actor_id button = q1_alive(g, other) ? other : g->maps->world_actor;
    if (!q1_alive(g, button))
        return true;
    qa_body_state body, button_body;
    if (!qa_world_body_read(g->services.world, id, &body, error) ||
        !qa_world_body_read(g->services.world, button, &button_body, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    motion = &entity->map->pending.mover;
    float position = body.origin.z + (body.bounds.mins.z + body.bounds.maxs.z) * .5f;
    float button_position =
        button_body.origin.z + (button_body.bounds.mins.z + button_body.bounds.maxs.z) * .5f;
    float height = entity->map->height, floor = motion->rogue.floor;
    if (position > button_position)
        motion->rogue.target_floor = floor - ceilf((position - button_position) / height);
    else if (button_position - position > height)
        motion->rogue.target_floor = floor + floorf((button_position - position) / height);
    else if (direction == -1 && floor > 0)
        motion->rogue.target_floor = floor - 1;
    else if (direction == 1 && floor < entity->map->counter_value - 1)
        motion->rogue.target_floor = floor + 1;
    else
        return true;
    if (!isfinite(motion->rogue.target_floor))
        motion->rogue.target_floor = 0;
    return elevator_go(g, entity, error);
}
bool q1_map_rogue_plat_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!q1_map_player(g, other))
        return true;
    entity = platform(g, id);
    if (!entity)
        return true;
    if (entity->map->kind == Q1_MAP_ELEVATOR_BUTTON) {
        entity->activator = other;
        return button_fire(g, entity, error);
    }
    if (q1_health(g, other) <= 0)
        return true;
    entity = platform(g, id);
    if (!entity)
        return true;
    qa_actor_id owner = entity->owner;
    qa_body_state player;
    qa_error read_error = {0};
    if (!qa_world_body_read(g->services.world, other, &player, &read_error)) {
        if (read_error.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = read_error;
        return false;
    }
    entity = platform(g, owner);
    if (!entity)
        return true;
    q1_map_movement *motion = &entity->map->pending.mover;
    if (motion->rogue.last_move + 2 > g->time || motion->rogue.disabled)
        return true;
    if (motion->rogue.go_to) {
        if (motion->rogue.go_time < g->time) {
            if (!move(g, entity, motion->rogue.go_to == 1, error))
                return false;
            entity = platform(g, owner);
            if (entity)
                entity->map->pending.mover.rogue.go_to = 0;
        }
        return true;
    }
    if (motion->position == Q1_MAP_UP || motion->position == Q1_MAP_DOWN)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner, &body, error))
        return false;
    entity = platform(g, owner);
    if (!entity)
        return true;
    motion = &entity->map->pending.mover;
    float center = body.origin.z + (body.bounds.mins.z + body.bounds.maxs.z) * .5f;
    bool same_level = motion->position == Q1_MAP_TOP
                          ? center <= player.origin.z
                          : player.origin.z - center <= entity->map->height;
    motion->rogue.called = !same_level;
    motion->rogue.go_time = g->time + (same_level ? .5 : .1);
    motion->rogue.go_to = motion->position == Q1_MAP_BOTTOM ? 1 : 2;
    return true;
}
bool q1_map_rogue_plat_blocked(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                               qa_error *error) {
    if (entity->map->kind != Q1_MAP_ROGUE_PLAT)
        return true;
    if (!(entity->spawnflags & 3) && (entity->spawnflags & 4)) {
        entity->map->pending.mover.rogue.target_floor = entity->map->pending.mover.rogue.floor;
        return elevator_go(g, entity, error);
    }
    if (!(entity->spawnflags & (3 | 16)))
        return true;
    qa_actor_id id = entity->id;
    if (!q1_damage(g, other, id, id, 1, QA_Q1_WEAPON_COUNT, error))
        return false;
    entity = platform(g, id);
    if (!entity)
        return true;
    q1_map_position position = entity->map->pending.mover.position;
    if (position != Q1_MAP_UP && position != Q1_MAP_DOWN)
        return q1_map_fail(error, "plat_new_crush: bad self.state");
    return move(g, entity, position == Q1_MAP_DOWN, error);
}
bool q1_map_rogue_plat_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                                qa_error *error) {
    if (entity->map->kind != Q1_MAP_ELEVATOR_BUTTON ||
        outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    qa_actor_id id = entity->id;
    entity->activator = outcome->request.attack.attacker;
    if (!qa_combat_set_health(g->services.combat, id, entity->max_health, error))
        return false;
    entity = platform(g, id);
    if (!entity || !q1_map_damageable(g, entity, false, error))
        return entity == NULL;
    entity = platform(g, id);
    return !entity || button_fire(g, entity, error);
}
bool q1_map_rogue_plat_think(qa_q1_game *g, q1_actor *entity, q1_map_action action,
                             qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_movement *motion = &entity->map->pending.mover;
    switch (action) {
    case Q1_MAP_ROGUE_PLAT_UP:
    case Q1_MAP_ROGUE_PLAT_DOWN:
        return move(g, entity, action == Q1_MAP_ROGUE_PLAT_UP, error);
    case Q1_MAP_ROGUE_PLAT_TOP:
    case Q1_MAP_ROGUE_PLAT_BOTTOM: {
        bool up = action == Q1_MAP_ROGUE_PLAT_TOP;
        if (!platform_sound(g, entity, true, error))
            return false;
        entity = platform(g, id);
        if (!entity)
            return true;
        motion = &entity->map->pending.mover;
        motion->position = up ? Q1_MAP_TOP : Q1_MAP_BOTTOM;
        if ((entity->spawnflags & 1) && !up) {
            float delay = q1_health(g, id);
            entity = platform(g, id);
            return !entity || q1_map_schedule(g, entity, delay, Q1_MAP_ROGUE_PLAT_UP, error);
        }
        if (!(entity->spawnflags & 16))
            return true;
        motion->rogue.last_move = g->time;
        q1_map_action next = up ? Q1_MAP_ROGUE_PLAT_DOWN : Q1_MAP_ROGUE_PLAT_UP;
        if (motion->rogue.called) {
            motion->rogue.called = false;
            motion->rogue.last_move = 0;
            return q1_map_schedule(g, entity, 1.5, next, error);
        }
        if (up != ((entity->spawnflags & 8) != 0)) {
            motion->rogue.called = false;
            return q1_map_schedule(g, entity, entity->delay, next, error);
        }
        return true;
    }
    case Q1_MAP_ELEVATOR_STOP:
        motion->rogue.floor = motion->rogue.target_floor;
        if (!platform_sound(g, entity, true, error))
            return false;
        entity = platform(g, id);
        if (entity) {
            entity->map->pending.mover.position = Q1_MAP_BOTTOM;
            entity->map->pending.mover.rogue.last_use = g->time;
        }
        return true;
    case Q1_MAP_ELEVATOR_BUTTON_WAIT:
        if (q1_alive(g, g->maps->world_actor))
            g->maps->elevator_direction = entity->spawnflags & 1 ? -1 : 1;
        motion->position = Q1_MAP_TOP;
        if (!q1_map_schedule(g, entity, entity->wait, Q1_MAP_ELEVATOR_BUTTON_RETURN, error) ||
            !q1_map_targets(g, entity, entity->activator, error))
            return false;
        entity = platform(g, id);
        if (entity)
            entity->frame = 1;
        return true;
    case Q1_MAP_ELEVATOR_BUTTON_RETURN: {
        motion->position = Q1_MAP_DOWN;
        entity->frame = 0;
        float health = q1_health(g, id);
        entity = platform(g, id);
        if (!entity)
            return true;
        if (health != 0 && !q1_map_damageable(g, entity, true, error))
            return false;
        entity = platform(g, id);
        return !entity || q1_map_move(g, entity, entity->map->pending.mover.pos1,
                                      Q1_MAP_ELEVATOR_BUTTON_DONE, error);
    }
    case Q1_MAP_ELEVATOR_BUTTON_DONE:
        motion->position = Q1_MAP_BOTTOM;
        return true;
    default:
        return q1_map_fail(error, "invalid Rogue platform continuation");
    }
}
