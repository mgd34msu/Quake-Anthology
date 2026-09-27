#include "internal.h"
#include <stdio.h>

static q1_actor *trigger(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && q1_map_is_hip_trigger(entity->map->kind) ? entity : NULL;
}
static void counter_off(qa_q1_game *g, q1_actor *entity) {
    q1_map_state *state = entity->map;
    if (state->pending.counter.ticks != 0 && (entity->spawnflags & 32)) {
        state->pending.counter.stop_after_cycle = true;
        return;
    }
    state->use_enabled = true;
    state->pending.counter.running = false;
    state->pending.counter.stop_after_cycle = false;
    q1_map_cancel(g, entity);
}
static bool counter_tick(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    float count = entity->map->pending.counter.ticks + 1;
    entity->map->pending.counter.ticks = count;
    entity->map->counter_value =
        entity->spawnflags & 16 ? floorf(q1_random(g) * entity->count) + 1 : count;
    if (!q1_map_targets(g, entity, entity->activator, error))
        return false;
    entity = trigger(g, id);
    if (!entity)
        return true;
    if (!q1_map_schedule(g, entity, entity->wait, Q1_MAP_COUNTER_TICK, error))
        return false;
    if (entity->spawnflags & 4)
        counter_off(g, entity);
    if (count >= entity->count) {
        entity->map->pending.counter.ticks = 0;
        if (entity->map->pending.counter.stop_after_cycle || !(entity->spawnflags & 2)) {
            if (entity->spawnflags & 1)
                counter_off(g, entity);
            else
                return q1_remove(g, entity, error);
        }
    }
    return true;
}
static bool counter_start(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    entity->activator = activator;
    entity->map->pending.counter.stop_after_cycle = false;
    entity->map->pending.counter.running = true;
    entity->map->use_enabled = (entity->spawnflags & 1) != 0;
    if (entity->spawnflags & 8) {
        entity->map->pending.counter.ticks = 0;
        entity->map->counter_value = 0;
    }
    return entity->delay != 0
               ? q1_map_schedule(g, entity, entity->delay, Q1_MAP_COUNTER_TICK, error)
               : counter_tick(g, entity, error);
}
static bool use_key(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = entity->id;
    if (!q1_map_player(g, activator))
        return true;
    entity = trigger(g, id);
    if (!entity || entity->map->cooldown > g->time)
        return true;
    entity->map->cooldown = g->time + 2;
    bool gold = (entity->spawnflags & 1) != 0;
    int32_t world_type = g->options.world_type;
    qa_item_id key;
    if (!qa_builtin_resource(&g->services, gold ? "q1:key/gold" : "q1:key/silver", &key, error))
        return false;
    bool consumed = false;
    if (qa_inventory_has(g->services.inventory, activator) &&
        !qa_inventory_consume(g->services.inventory, activator, key, 1, &consumed, error))
        return false;
    entity = trigger(g, id);
    if (!entity)
        return true;
    if (!consumed) {
        const char *message =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->message);
        char fallback[64];
        if (!message || !*message) {
            (void)snprintf(fallback, sizeof(fallback), "$qc_need_%s_%s", gold ? "gold" : "silver",
                           world_type == 2   ? "keycard"
                           : world_type == 1 ? "runekey"
                                             : "key");
            message = fallback;
        }
        if (!q1_message(g, activator, message, error))
            return false;
        return !trigger(g, id) || q1_sound(g, id,
                                           world_type == 2   ? "doors/basetry.wav"
                                           : world_type == 1 ? "doors/runetry.wav"
                                                             : "doors/medtry.wav",
                                           0, 1, error);
    }
    entity->map->touch_enabled = entity->map->use_enabled = false;
    entity->message = QA_STRING_NONE;
    if (!q1_map_schedule(g, entity, .1, Q1_MAP_REMOVE, error) ||
        !q1_sound(g, id,
                  world_type == 2   ? "doors/baseuse.wav"
                  : world_type == 1 ? "doors/runeuse.wav"
                                    : "doors/meduse.wav",
                  0, 1, error))
        return false;
    entity = trigger(g, id);
    return !entity || q1_map_targets(g, entity, activator, error);
}
bool q1_map_hip_trigger_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_kind kind = entity->map->kind;
    if (kind == Q1_MAP_HIP_COUNTER) {
        entity->wait = entity->wait ? entity->wait : 1;
        entity->count = floorf(entity->count);
        if (entity->count <= 0)
            entity->count = 10;
        entity->map->pending.counter.ticks = entity->map->counter_value = 0;
        entity->map->use_enabled = true;
        return !(entity->spawnflags & 64) ||
               q1_map_schedule(g, entity, .1, Q1_MAP_COUNTER_START, error);
    }
    if (kind == Q1_MAP_ONCOUNT) {
        if (q1_classnamed(g, id, "func_oncount")) {
            entity->count = floorf(entity->count);
            if (entity->count <= 0)
                entity->count = 1;
        }
        entity->map->use_enabled = true;
        return true;
    }
    if (kind == Q1_MAP_DECOY_TRIGGER && g->options.deathmatch)
        return q1_remove(g, entity, error);
    if (kind == Q1_MAP_THRESHOLD || kind == Q1_MAP_BREAKAWAY) {
        if (!entity->map->has_inline_model)
            return q1_map_fail(error, "Hipnotic brush trigger has no inline model");
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = trigger(g, id);
        if (!entity)
            return true;
        entity->map->mangle = body.angles;
        entity->physics.solid = QA_PHYSICS_BRUSH;
        entity->physics.motion = QA_PHYSICS_PUSH;
        body.angles = qa_v3(0, 0, 0);
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = trigger(g, id);
        if (!entity)
            return true;
        if (kind == Q1_MAP_BREAKAWAY)
            entity->map->use_enabled = true;
        else {
            if (entity->spawnflags & 2)
                entity->model = QA_STRING_NONE;
            if (!entity->max_health)
                entity->max_health = 60;
            float health = entity->max_health;
            if (!qa_combat_set_health(g->services.combat, id, health, error))
                return false;
            entity = trigger(g, id);
            if (!entity)
                return true;
            if (!q1_map_damageable(g, entity, true, error))
                return false;
        }
    } else {
        if (!q1_map_trigger_init(g, entity, false, error))
            return false;
        entity = trigger(g, id);
        if (!entity)
            return true;
        entity->map->touch_enabled = true;
        if (kind == Q1_MAP_USE_KEY)
            entity->map->use_enabled = true;
        else if (kind == Q1_MAP_GRAVITY_TRIGGER)
            entity->map->gravity =
                entity->map->gravity == 0 ? -1 : (entity->map->gravity - 1) / 100;
        else if (kind == Q1_MAP_WATERFALL) {
            entity->count = entity->count ? entity->count : 100;
            entity->map->movedir =
                qa_vec_scale(entity->map->movedir, entity->speed ? entity->speed : 50);
        }
    }
    entity = trigger(g, id);
    return !entity || q1_link(g, entity, error);
}
bool q1_map_hip_trigger_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                            qa_actor_id activator, qa_error *error) {
    switch (entity->map->kind) {
    case Q1_MAP_HIP_COUNTER:
        if (!entity->map->pending.counter.running)
            return counter_start(g, entity, activator, error);
        counter_off(g, entity);
        return true;
    case Q1_MAP_ONCOUNT: {
        qa_actor_id id = entity->id;
        double value = 0;
        if (q1_classnamed(g, other, "func_counter"))
            (void)qa_targets_number(g->maps->options.targets, other, "counter_state", &value);
        entity = trigger(g, id);
        return !entity || value != entity->count || q1_map_targets(g, entity, other, error);
    }
    case Q1_MAP_USE_KEY:
        return use_key(g, entity, activator, error);
    case Q1_MAP_BREAKAWAY:
        return q1_remove(g, entity, error);
    default:
        return true;
    }
}
bool q1_map_hip_trigger_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id id = entity->id;
    switch (entity->map->kind) {
    case Q1_MAP_USE_KEY:
        return use_key(g, entity, other, error);
    case Q1_MAP_REMOVE_TRIGGER: {
        bool player = q1_map_player(g, other);
        q1_actor *native = q1_entity(g, other);
        qa_string_id classname = native ? native->classname : QA_STRING_NONE;
        qa_builtin_actor_traits traits;
        if (!native && g->services.actor_traits &&
            g->services.actor_traits(g->services.context, other, &traits))
            classname = traits.classname;
        qa_bytes name = qa_strings_text(qa_session_strings(g->services.session), classname);
        bool monster = name.size >= 8 && !memcmp(name.data, "monster_", 8);
        entity = trigger(g, id);
        if (!entity || (player && !(entity->spawnflags & 2)) ||
            (monster && !(entity->spawnflags & 1)))
            return true;
        q1_actor *victim = q1_entity(g, other);
        if (victim) {
            victim->touch_disabled = true;
            victim->model = QA_STRING_NONE;
        }
        return q1_remove(g, entity, error);
    }
    case Q1_MAP_GRAVITY_TRIGGER:
        if (!q1_map_player(g, other))
            return true;
        entity = trigger(g, id);
        if (!entity)
            return true;
        if (!g->host.set_gravity)
            return q1_map_fail(error, "Q1 source gravity mutation requires selected movement");
        return g->host.set_gravity(g->host.context, other,
                                   entity->map->gravity == -1 ? 1 : entity->map->gravity, error);
    case Q1_MAP_DECOY_TRIGGER:
        if (!q1_classnamed(g, other, "monster_decoy"))
            return true;
        entity = trigger(g, id);
        if (!entity)
            return true;
        entity->map->touch_enabled = false;
        return q1_map_schedule(g, entity, .1, Q1_MAP_REMOVE, error) &&
               q1_map_targets(g, entity, other, error);
    case Q1_MAP_WATERFALL: {
        if (!q1_map_player(g, other))
            return true;
        if (!q1_alive(g, other))
            return true;
        qa_body_state body;
        qa_error body_error = {0};
        if (!qa_world_body_read(g->services.world, other, &body, &body_error)) {
            if (body_error.code == QA_ERROR_NOT_FOUND)
                return true;
            if (error)
                *error = body_error;
            return false;
        }
        entity = trigger(g, id);
        if (!entity || !q1_alive(g, other))
            return true;
        body.velocity = qa_vec_add(body.velocity, entity->map->movedir);
        body.velocity.x += entity->count * (q1_random(g) - .5f);
        body.velocity.y += entity->count * (q1_random(g) - .5f);
        return qa_world_body_write(g->services.world, other, &body, error);
    }
    default:
        return true;
    }
}
bool q1_map_hip_trigger_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                                 qa_error *error) {
    if (outcome->result.reaction == QA_REACTION_NONE)
        return true;
    qa_actor_id id = entity->id;
    if (!qa_combat_set_health(g->services.combat, id, entity->max_health, error))
        return false;
    entity = trigger(g, id);
    if (!entity || outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    if (!q1_map_damageable(g, entity, false, error))
        return false;
    entity = trigger(g, id);
    if (!entity || !q1_map_targets(g, entity, outcome->request.attack.attacker, error))
        return entity == NULL;
    entity = trigger(g, id);
    if (!entity || !q1_map_damageable(g, entity, true, error))
        return entity == NULL;
    entity = trigger(g, id);
    return !entity || (entity->spawnflags & 1) || q1_remove(g, entity, error);
}
bool q1_map_hip_trigger_think(qa_q1_game *g, q1_actor *entity, q1_map_action action,
                              qa_error *error) {
    return action == Q1_MAP_COUNTER_START ? counter_start(g, entity, (qa_actor_id){0}, error)
                                          : counter_tick(g, entity, error);
}
