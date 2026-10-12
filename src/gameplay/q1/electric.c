#include "maps/internal.h"

static bool electric_current(qa_q1_game *g, qa_actor_id actor, uint32_t flags) {
    if (!(flags & Q1_LIGHTNING_ADDON_OWNER)) return q1_alive(g, actor);
    q1_actor *entity = q1_entity(g, actor);
    return entity && entity->map && q1_map_is_addon_effect(entity->map->kind);
}

bool q1_electric_rays(qa_q1_game *g, qa_actor_id attacker, qa_actor_id inflictor,
                       qa_actor_id ignore, qa_vec3 start,
                       qa_vec3 end, float damage, float blood, int32_t color, qa_vec3 direction,
                       uint32_t flags, qa_q1_weapon weapon, const char *cause, qa_error *error) {
    qa_vec3 delta = qa_vec_sub(end, start), side = qa_v3(-delta.y * 16, -delta.y * 16, 0);
    qa_vec3 offsets[] = {{0, 0, 0}, side, {-side.x, -side.y, 0}};
    if (flags & Q1_LIGHTNING_ADDON_OWNER) offsets[2] = qa_vec_scale(side, -1);
    qa_actor_id hit[3];
    size_t count = 0;
    qa_string_id death_type = 0;
    if (cause && !qa_builtin_resource(&g->services, cause, &death_type, error))
        return false;
    for (size_t i = 0; i < 3; ++i) {
        if ((flags & Q1_LIGHTNING_ADDON_OWNER) && !electric_current(g, inflictor, flags))
            return true;
        qa_trace_result trace;
        if (!q1_trace(g, qa_vec_add(start, offsets[i]), qa_vec_add(end, offsets[i]), ignore,
                      true, &trace, error))
            return false;
        if ((flags & Q1_LIGHTNING_ADDON_OWNER) && !electric_current(g, inflictor, flags))
            return true;
        if (trace.hit != QA_TRACE_HIT_ACTOR)
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j)
            duplicate |= qa_actor_id_equal(hit[j], trace.actor);
        if (duplicate)
            continue;
        if (flags & Q1_LIGHTNING_REMEMBER_BEFORE_READ)
            hit[count++] = trace.actor;
        qa_combat_state combat;
        qa_error observed = {0};
        bool present = qa_combat_read(g->services.combat, trace.actor, &combat, &observed);
        if (!electric_current(g, inflictor, flags))
            return true;
        if (!q1_alive(g, trace.actor))
            continue;
        if (!(flags & Q1_LIGHTNING_IGNORE_READ_FAILURE) &&
            !present && observed.code != QA_OK && observed.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = observed;
            return false;
        }
        bool damageable = present && combat.can_take_damage;
        if (!(flags & Q1_LIGHTNING_REMEMBER_BEFORE_READ) &&
            (damageable || (flags & Q1_LIGHTNING_REMEMBER_ALL)))
            hit[count++] = trace.actor;
        if ((flags & Q1_LIGHTNING_WETSUIT) &&
            qa_q1_game_power_expires(g, trace.actor, QA_Q1_WETSUIT) != 0)
            continue;
        if (damageable) {
            int32_t particles;
            if (flags & Q1_LIGHTNING_TRUNC_DAMAGE_COUNT) {
                float scaled = damage * 4;
                if (!isfinite(scaled) || scaled < (float)INT32_MIN || scaled >= (float)INT32_MAX)
                    return q1_map_fail(error, "Q1 addon lightning particle count exceeds native range");
                particles = (int32_t)scaled;
            } else {
                double emission_count = blood > 0 ? ceilf(blood) : 0;
                if (!isfinite(blood) || emission_count > INT32_MAX) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, trace.actor.slot,
                                 "Q1 lightning particle count is outside integer range");
                    return false;
                }
                particles = (int32_t)emission_count;
            }
            qa_builtin_event event = {.kind = flags & Q1_LIGHTNING_PARTICLES ? QA_BUILTIN_PARTICLES
                                                                             : QA_BUILTIN_IMPACT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = flags & Q1_LIGHTNING_SOURCE_EVENT ? inflictor : trace.actor,
                                      .time_ns = g->time_ns,
                                      .origin = trace.end,
                                      .direction = direction,
                                      .value = blood,
                                      .count = particles,
                                      .code = color};
            if (!(flags & Q1_LIGHTNING_DAMAGE_FIRST) &&
                !qa_builtin_emit(&g->services, &event, error))
                return false;
            if (!electric_current(g, inflictor, flags))
                return true;
            if (!q1_alive(g, trace.actor))
                continue;
            bool ok = cause ? q1_damage_typed(g, trace.actor, inflictor, attacker, damage, weapon,
                                              QA_Q1_ARMOR_NORMAL, death_type, error)
                            : q1_damage(g, trace.actor, inflictor, attacker, damage, weapon, error);
            if (!ok)
                return false;
            if ((flags & Q1_LIGHTNING_DAMAGE_FIRST) &&
                !qa_builtin_emit(&g->services, &event, error))
                return false;
        }
        if (!electric_current(g, inflictor, flags))
            return true;
    }
    return true;
}
bool q1_lightning_rays(qa_q1_game *g, qa_actor_id attacker, qa_actor_id inflictor, qa_vec3 start,
                       qa_vec3 end, float damage, float blood, int32_t color, qa_vec3 direction,
                       uint32_t flags, qa_q1_weapon weapon, const char *cause, qa_error *error) {
    return q1_electric_rays(g, attacker, inflictor, inflictor, start, end, damage, blood, color,
                            direction, flags, weapon, cause, error);
}
