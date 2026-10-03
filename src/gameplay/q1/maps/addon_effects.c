#include "internal.h"
#include <float.h>
#include <limits.h>

static q1_actor *effect(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_addon_effect(e->map->kind) ? e : NULL;
}
static bool emit(qa_q1_game *g, qa_builtin_event *event, const char *name, qa_error *error) {
    event->family = QA_GAME_Q1;
    event->provider = g->options.provider;
    event->time_ns = g->time_ns;
    return (!name || qa_builtin_resource(&g->services, name, &event->resource, error)) &&
           qa_builtin_emit(&g->services, event, error);
}
static bool particles(qa_q1_game *g, qa_actor_id actor, qa_vec3 origin, qa_vec3 direction,
                       int32_t color, int32_t count, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_PARTICLES, .actor = actor, .origin = origin,
                              .direction = direction, .code = color, .count = count};
    return emit(g, &event, NULL, error);
}
static float signed_random(qa_q1_game *g) { return q1_random(g) * 2 - 1; }
static bool effect_float(double value, float *out, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        q1_map_fail(error, "Q1 authored effect exceeds finite float range");
        return false;
    }
    *out = fabs(value) > FLT_MAX ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}
static bool particle_step(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    qa_body_state body;
    qa_vec3 origin, direction;
    int32_t color, count;
    if (kind == Q1_MAP_ADDON_EMBERS || kind == Q1_MAP_ADDON_EMBERS_TALL) {
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        e = effect(g, id);
        if (!e) return true;
        qa_vec3 velocity = body.velocity, size = e->map->particle_size;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!effect(g, id)) return true;
        origin = body.origin;
        float up = q1_random(g) * 2 + 2;
        float x = signed_random(g), y = signed_random(g);
        float origin_x = signed_random(g), origin_y = signed_random(g);
        if (!effect_float((double)x * velocity.x, &direction.x, error) ||
            !effect_float((double)y * velocity.y, &direction.y, error) ||
            !effect_float((double)up * velocity.z, &direction.z, error)) return false;
        if (!effect_float((double)origin.x + (double)size.x * origin_x, &origin.x, error) ||
            !effect_float((double)origin.y + (double)size.y * origin_y, &origin.y, error))
            return false;
        color = 234;
        count = 2;
    } else if (kind == Q1_MAP_ADDON_PARTICLE_TELE) {
        double x = (double)signed_random(g) * 10;
        double y = (double)signed_random(g) * 10;
        double z = (double)signed_random(g) * 5;
        float x_squared = (float)(x * x), y_squared = (float)(y * y),
              z_squared = (float)(z * z);
        float xy_squared = x_squared + y_squared;
        float squared = xy_squared + z_squared;
        float magnitude = (float)sqrt((double)squared);
        double inverse = magnitude == 0 ? 0 : 1.0 / magnitude;
        direction = qa_v3((float)(x * inverse), (float)(y * inverse), (float)(z * inverse));
        float distance = g->options.program == QA_Q1_MG3 ? e->map->distance : 64;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!effect(g, id)) return true;
        qa_vec3 offset;
        if (!effect_float((double)direction.x * distance, &offset.x, error) ||
            !effect_float((double)direction.y * distance, &offset.y, error) ||
            !effect_float((double)direction.z * distance, &offset.z, error) ||
            !effect_float((double)body.origin.x + offset.x, &origin.x, error) ||
            !effect_float((double)body.origin.y + offset.y, &origin.y, error) ||
            !effect_float((double)body.origin.z + offset.z, &origin.z, error)) return false;
        double speed = (double)distance * -.125;
        if (!effect_float((double)direction.x * speed, &direction.x, error) ||
            !effect_float((double)direction.y * speed, &direction.y, error) ||
            !effect_float((double)direction.z * speed, &direction.z, error)) return false;
        color = 3;
        count = 3;
    } else {
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!effect(g, id)) return true;
        qa_vec3 velocity = body.velocity;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        if (!effect(g, id)) return true;
        origin = body.origin;
        float x = signed_random(g), y = signed_random(g);
        if (!effect_float((double)x * velocity.x, &direction.x, error) ||
            !effect_float((double)y * velocity.y, &direction.y, error)) return false;
        direction.z = velocity.z;
        color = 13;
        count = 2;
    }
    if (!particles(g, id, origin, direction, color, count, error))
        return false;
    e = effect(g, id);
    return !e || q1_map_schedule(g, e, (double)e->wait + (double)e->delay * q1_random(g),
                                  Q1_MAP_ADDON_PARTICLE_TICK, error);
}
static bool shake_step(qa_q1_game *g, q1_actor *e, qa_error *error) {
    const qa_q1_level_state *level = qa_q1_level_read(g->maps->options.level);
    if (level && level->intermission)
        return true;
    qa_actor_id id = e->id;
    float wait = e->wait, damage = e->damage, ramp_end = e->delay;
    double end = e->map->active_until, start = end - wait;
    bool finished = g->time > end;
    if (finished && !(e->spawnflags & 1) &&
        !q1_sound_resource(g, id, e->map->noise[1], 0, 1, 1, error))
        return false;
    q1_actor_snapshot *players;
    if (!q1_snapshot_players(g, &players, error))
        return false;
    float intensity = damage;
    if (!finished && g->time < ramp_end &&
        (wait == 0 || !effect_float((double)damage * ((g->time - start) / ((double)wait / 3)),
                                    &intensity, error))) {
        players->borrowed = false;
        return wait == 0 ? q1_map_fail(error, "Q1 screenshake has zero ramp duration") : false;
    }
    bool ok = true;
    for (size_t i = 0; ok && i < players->count && effect(g, id); ++i) {
        qa_actor_id player = players->actors[i];
        if (!q1_alive(g, player))
            continue;
        qa_vec3 angles = qa_v3(0, 0, 0);
        if (!finished) {
            float x = q1_random(g), y = signed_random(g), z = q1_random(g);
            angles = qa_v3(x * intensity, y * intensity, z * intensity);
        }
        qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT, .actor = player,
                                  .direction = angles, .value = 0};
        ok = emit(g, &event, finished ? "view-roll" : "punch-angle", error);
    }
    players->borrowed = false;
    e = effect(g, id);
    return ok && (finished || !e || q1_map_schedule(g, e, .05, Q1_MAP_ADDON_SHAKE_TICK, error));
}
static bool lightning_damage(qa_q1_game *g, qa_actor_id id, qa_vec3 start, qa_vec3 end,
                               float damage, qa_error *error) {
    qa_vec3 delta = qa_vec_sub(end, start);
    qa_vec3 side = qa_v3(-delta.y * 16, -delta.y * 16, 0);
    qa_actor_id hits[3] = {0};
    unsigned count = 0;
    for (unsigned i = 0; i < 3 && effect(g, id); ++i) {
        qa_vec3 offset = i == 0 ? qa_v3(0, 0, 0) : i == 1 ? side : qa_vec_scale(side, -1);
        qa_trace_result trace;
        if (!q1_trace(g, qa_vec_add(start, offset), qa_vec_add(end, offset), id, true,
                       &trace, error))
            return false;
        if (!effect(g, id))
            return true;
        if (trace.hit != QA_TRACE_HIT_ACTOR)
            continue;
        bool duplicate = false;
        for (unsigned j = 0; j < count; ++j)
            duplicate |= qa_actor_id_equal(hits[j], trace.actor);
        if (duplicate)
            continue;
        hits[count++] = trace.actor;
        qa_combat_state combat;
        qa_error observed = {0};
        bool damageable = qa_combat_read(g->services.combat, trace.actor, &combat, &observed) &&
                          combat.can_take_damage;
        if (!effect(g, id))
            return true;
        if (damageable && q1_alive(g, trace.actor)) {
            if (!isfinite(damage * 4) || damage * 4 < (float)INT32_MIN || damage * 4 >= (float)INT32_MAX)
                return q1_map_fail(error, "Q1 addon lightning particle count exceeds native range");
            if (!particles(g, id, trace.end, qa_v3(0, 0, 100), 225, (int32_t)(damage * 4), error))
                return false;
            if (effect(g, id) && q1_alive(g, trace.actor) &&
                !q1_damage(g, trace.actor, id, id, damage, QA_Q1_WEAPON_COUNT, error))
                return false;
        }
    }
    return true;
}
static bool lightning(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = e->id;
    uint32_t flags = e->spawnflags;
    float damage = e->damage, volume = e->map->volume;
    int32_t style = e->map->style;
    qa_string_id noise = e->map->noise[0];
    q1_actor_snapshot *targets;
    if (!q1_snapshot_targets(g, g->maps->options.targets, e->target, &targets, error))
        return false;
    size_t chosen = flags & 1 ? (size_t)floor((double)targets->count * q1_random(g)) : SIZE_MAX;
    bool ok = true;
    for (size_t i = 0; ok && i < targets->count && effect(g, id); ++i) {
        qa_actor_id target = targets->actors[i];
        if ((chosen != SIZE_MAX && chosen != i) || !q1_alive(g, target))
            continue;
        qa_body_state own_body, target_body;
        bool reverse = (flags & 2) != 0;
        ok = qa_world_body_read(g->services.world, reverse ? target : id,
                               reverse ? &target_body : &own_body, error);
        if (ok && effect(g, id) && q1_alive(g, target))
            ok = qa_world_body_read(g->services.world, reverse ? id : target,
                                   reverse ? &own_body : &target_body, error);
        if (!ok || !effect(g, id) || !q1_alive(g, target))
            continue;
        qa_vec3 start = flags & 2 ? target_body.origin : own_body.origin;
        qa_vec3 end = flags & 2 ? own_body.origin : target_body.origin;
        qa_trace_result trace;
        ok = q1_trace(g, start, end, id, false, &trace, error);
        if (!ok || !effect(g, id) || !q1_alive(g, target))
            continue;
        if (g->options.program != QA_Q1_MG3 || !(flags & 16))
            ok = q1_sound_resource(g, target, noise, 0, 1, volume, error);
        if (!ok || !effect(g, id) || !q1_alive(g, target))
            continue;
        qa_builtin_event event = {.kind = QA_BUILTIN_BEAM, .actor = target,
                                  .origin = start, .end = trace.end,
                                  .code = style == 1 ? 1 : style == 2 ? 2 : 3};
        ok = emit(g, &event, "lightning", error);
        if (ok && damage != 0 && effect(g, id))
            ok = lightning_damage(g, id, start, trace.end, damage, error);
        qa_authored_target fields;
        if (!ok || !effect(g, id) || !q1_alive(g, target) || (flags & 4) ||
            !qa_targets_read(g->maps->options.targets, target, &fields) ||
            !q1_map_text(g, fields.target))
            continue;
        ok = qa_targets_use(g->maps->options.targets, target, activator, g->time_ns, error);
        if (ok && !(flags & 8) && effect(g, id) && q1_alive(g, target) &&
            qa_targets_read(g->maps->options.targets, target, &fields)) {
            ok = qa_targets_set_delay(g->maps->options.targets, target,
                                        (float)((double)fields.delay_seconds + .2),
                                        error) &&
                 qa_targets_use(g->maps->options.targets, target, activator, g->time_ns, error);
            if (q1_alive(g, target)) {
                qa_error restore = {0};
                bool restored = qa_targets_set_delay(g->maps->options.targets, target,
                                                   fields.delay_seconds,
                                                   ok ? error : &restore);
                ok = ok && restored;
            }
        }
    }
    targets->borrowed = false;
    return ok;
}
static bool alpha(qa_q1_game *g, qa_actor_id id, float value, qa_error *error) {
    if (g->maps->options.alpha_write)
        return g->maps->options.alpha_write(g->maps->options.context, id, value, error);
    q1_actor *target = q1_entity(g, id);
    if (!target)
        return q1_map_fail(error, "Q1 fade requires selected appearance owner");
    target->alpha = value;
    return true;
}
static bool fade_step(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    bool initialized = e->map->effect_active;
    float delay = e->delay;
    q1_actor_snapshot *targets;
    if (!q1_snapshot_targets(g, g->maps->options.targets, e->target, &targets, error))
        return false;
    bool ok = true;
    size_t remaining = 0;
    if (!initialized) {
        for (size_t i = 0; ok && i < targets->count && effect(g, id); ++i)
            if (q1_alive(g, targets->actors[i]))
                ok = alpha(g, targets->actors[i], 1, error);
        e = effect(g, id);
        if (e)
            e->map->effect_active = true;
    }
    for (size_t i = 0; ok && i < targets->count && effect(g, id); ++i) {
        qa_actor_id target = targets->actors[i];
        float health = q1_health(g, target);
        if (!effect(g, id) || !q1_alive(g, target) || health > 0)
            continue;
        float value;
        if (g->maps->options.alpha_read)
            ok = g->maps->options.alpha_read(g->maps->options.context, target, &value, error);
        else {
            const q1_actor *native = q1_entity_const(g, target);
            if (!native)
                ok = q1_map_fail(error, "Q1 fade requires selected appearance read");
            else
                value = native->alpha;
        }
        if (!ok || !effect(g, id) || !q1_alive(g, target))
            continue;
        if (value > 0) {
            if (delay == 0) {
                ok = q1_map_fail(error,"Q1 target fade requires a nonzero period");
                continue;
            }
            double faded = (double)value - g->elapsed / delay;
            if (!isfinite(faded) || fabs(faded) >= 0x1.ffffffp127) {
                ok = q1_map_fail(error,"Q1 target alpha exceeds native range");
                continue;
            }
            float next_alpha = fabs(faded) > FLT_MAX
                                   ? (faded < 0 ? -FLT_MAX : FLT_MAX)
                                   : (float)faded;
            ok = alpha(g, target, next_alpha, error);
            ++remaining;
        } else if (g->maps->options.retire_actor)
            ok = g->maps->options.retire_actor(g->maps->options.context, target, error);
        else {
            q1_actor *native = q1_entity(g, target);
            ok = native ? q1_remove(g, native, error)
                        : q1_map_fail(error, "Q1 fade requires selected actor retirement");
        }
    }
    targets->borrowed = false;
    e = effect(g, id);
    return ok && (!e || (remaining ? q1_map_schedule(g, e, 0, Q1_MAP_ADDON_FADE_TICK, error)
                                   : q1_remove(g, e, error)));
}
bool q1_map_addon_effect_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    bool coop_filter = kind == Q1_MAP_ADDON_SHAKE || kind == Q1_MAP_ADDON_FADE_TRIGGER ||
                       kind == Q1_MAP_ADDON_FREEZE;
    if (coop_filter && !g->options.coop &&
        (e->spawnflags & (g->options.program == QA_Q1_MG3 ? 131072u : 32768u)))
        return q1_remove(g, e, error);
    e->map->use_enabled = true;
    if (kind == Q1_MAP_ADDON_SHAKE) {
        e->wait = e->wait != 0 ? e->wait : 2;
        e->damage = e->damage != 0 ? e->damage : 3;
        return qa_builtin_resource(&g->services, "misc/quake.wav", &e->map->noise[0], error) &&
               qa_builtin_resource(&g->services, "misc/quakeend.wav", &e->map->noise[1], error);
    }
    if (kind == Q1_MAP_ADDON_SOUND)
        return q1_map_text(g, e->map->noise[0]) || q1_remove(g, e, error);
    if (kind == Q1_MAP_ADDON_LIGHTNING) {
        if (!q1_map_text(g, e->map->noise[0]) &&
            !qa_builtin_resource(&g->services, "misc/power.wav", &e->map->noise[0], error))
            return false;
        e->map->volume = e->map->volume != 0 ? e->map->volume : 1;
        if (g->options.program == QA_Q1_MG3 && (e->spawnflags & 16))
            e->map->volume = 0;
        return true;
    }
    if (kind == Q1_MAP_ADDON_FADE_TRIGGER) {
        e->delay = e->delay != 0 ? e->delay : 1;
        return true;
    }
    if (kind == Q1_MAP_ADDON_FREEZE || kind == Q1_MAP_ADDON_FADE_MANAGER)
        return true;
    e->map->use_enabled = kind == Q1_MAP_ADDON_FOUNTAIN && (e->spawnflags & 1);
    e->delay = e->delay != 0 ? e->delay : .1f;
    if (kind == Q1_MAP_ADDON_PARTICLE_TELE)
        e->map->distance = e->map->distance != 0 ? e->map->distance : 64;
    else {
        e->wait = e->wait != 0 ? e->wait : .05f;
        bool tall = kind == Q1_MAP_ADDON_EMBERS_TALL;
        if (qa_vec_dot(e->map->particle_size, e->map->particle_size) == 0)
            e->map->particle_size = tall ? qa_v3(40, 40, 0) : qa_v3(128, 128, 0);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        e = effect(g, id);
        if (!e)
            return true;
        if (qa_vec_dot(body.velocity, body.velocity) == 0) {
            body.velocity = qa_v3(1, 1, kind == Q1_MAP_ADDON_FOUNTAIN ? 6 : tall ? 2 : 1);
            if (!qa_world_body_write(g->services.world, id, &body, error))
                return false;
            e = effect(g, id);
            if (!e)
                return true;
        }
    }
    return e->map->use_enabled || q1_map_schedule(g, e, (double)e->wait + (double)e->delay * q1_random(g),
                                                  Q1_MAP_ADDON_PARTICLE_TICK, error);
}
bool q1_map_addon_effect_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = e->id;
    switch (e->map->kind) {
    case Q1_MAP_ADDON_SHAKE: {
        float until;
        if (!effect_float(g->time + e->wait, &until, error)) return false;
        e->map->active_until = until;
        if (!effect_float(g->time + (double)e->wait / 3, &e->delay, error)) return false;
        if (!(e->spawnflags & 1) && !q1_sound_resource(g, id, e->map->noise[0], 0, 1, 1, error))
            return false;
        e = effect(g, id);
        return !e || q1_map_schedule(g, e, .05, Q1_MAP_ADDON_SHAKE_TICK, error);
    }
    case Q1_MAP_ADDON_SOUND:
        return !q1_map_text(g, e->map->noise[0]) ||
               q1_sound_resource(g, id, e->map->noise[0], 0, 1, 1, error);
    case Q1_MAP_ADDON_LIGHTNING:
        return lightning(g, e, activator, error);
    case Q1_MAP_ADDON_FOUNTAIN:
        return q1_map_schedule(g, e, .1, Q1_MAP_ADDON_PARTICLE_TICK, error);
    case Q1_MAP_ADDON_FADE_TRIGGER: {
        qa_string_id name = e->target;
        float delay = e->delay;
        q1_actor *manager;
        if (!q1_create(g, "fade_manager", Q1_MAP, id, &manager, error))
            return false;
        qa_actor_id child = manager->id;
        if (!q1_map_allocate(g, manager, error)) {
            qa_session_release(g->services.session, child, NULL);
            return false;
        }
        manager->map->kind = Q1_MAP_ADDON_FADE_MANAGER;
        manager->target = name;
        manager->delay = delay;
        manager->physics.solid = QA_PHYSICS_NOT_SOLID;
        manager->physics.motion = QA_PHYSICS_STATIONARY;
        if (q1_map_schedule(g, manager, 0, Q1_MAP_ADDON_FADE_TICK, error))
            return true;
        qa_session_release(g->services.session, child, NULL);
        return false;
    }
    case Q1_MAP_ADDON_FREEZE: {
        qa_actor_id source = e->id;
        q1_actor_snapshot *targets;
        if (!q1_snapshot_targets(g, g->maps->options.targets, e->target, &targets, error))
            return false;
        bool ok = true;
        for (size_t i = 0; ok && i < targets->count && effect(g, source); ++i) {
            qa_actor_id target = targets->actors[i];
            if (!q1_alive(g, target))
                continue;
            bool handled;
            ok = qa_q1_game_freeze(g, target, &handled, error);
            if (!ok || handled || !effect(g, source) || !q1_alive(g, target))
                continue;
            ok = g->maps->options.freeze_actor
                ? g->maps->options.freeze_actor(g->maps->options.context, target, error)
                : q1_map_fail(error, "Q1 freeze requires selected foreign continuation owner");
        }
        targets->borrowed = false;
        return ok;
    }
    default:
        return true;
    }
}
bool q1_map_addon_effect_think(qa_q1_game *g, q1_actor *e, q1_map_action action, qa_error *error) {
    if (action == Q1_MAP_ADDON_SHAKE_TICK)
        return shake_step(g, e, error);
    if (action == Q1_MAP_ADDON_FADE_TICK)
        return fade_step(g, e, error);
    return particle_step(g, e, error);
}
