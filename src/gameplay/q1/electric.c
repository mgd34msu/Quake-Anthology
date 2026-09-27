#include "internal.h"

bool q1_lightning_rays(qa_q1_game *g, qa_actor_id attacker, qa_actor_id inflictor, qa_vec3 start,
                       qa_vec3 end, float damage, float blood, int32_t color, qa_q1_weapon weapon,
                       const char *cause, qa_error *error) {
    qa_vec3 delta = qa_vec_sub(end, start), side = qa_v3(-delta.y * 16, -delta.y * 16, 0);
    qa_vec3 offsets[] = {{0, 0, 0}, side, {-side.x, -side.y, 0}};
    qa_actor_id hit[3];
    size_t count = 0;
    qa_string_id death_type = 0;
    if (cause && !qa_builtin_resource(&g->services, cause, &death_type, error))
        return false;
    for (size_t i = 0; i < 3; ++i) {
        qa_trace_result trace;
        if (!q1_trace(g, qa_vec_add(start, offsets[i]), qa_vec_add(end, offsets[i]), inflictor,
                      true, &trace, error))
            return false;
        if (trace.hit != QA_TRACE_HIT_ACTOR)
            continue;
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j)
            duplicate |= qa_actor_id_equal(hit[j], trace.actor);
        if (duplicate)
            continue;
        hit[count++] = trace.actor;
        if (q1_damageable(g, trace.actor)) {
            if (!q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, trace.end, blood, color, error))
                return false;
            bool ok = cause ? q1_damage_typed(g, trace.actor, inflictor, attacker, damage, weapon,
                                              QA_Q1_ARMOR_NORMAL, death_type, error)
                            : q1_damage(g, trace.actor, inflictor, attacker, damage, weapon, error);
            if (!ok)
                return false;
        }
        if (!q1_alive(g, inflictor))
            return true;
    }
    return true;
}
