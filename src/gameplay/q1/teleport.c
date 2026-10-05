#include "internal.h"
#include <stdio.h>

bool qa_q1_spawn_teleport_fog(qa_q1_game *g, qa_vec3 origin, qa_actor_id *out, qa_error *error) {
    if (!g || !qa_vec_finite(origin)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 teleport fog origin");
        return false;
    }
    q1_actor *fog;
    if (!q1_create(g, "teleport_fog", Q1_TIMER, (qa_actor_id){0}, &fog, error))
        return false;
    fog->classname = 0;
    qa_body_state body = {.origin = origin};
    if (!qa_world_body_write(g->services.world, fog->id, &body, error) ||
        !q1_schedule(g, fog, 0.2, Q1_THINK_TELEPORT_FOG, error) ||
        !q1_effect(g, QA_BUILTIN_TELEPORT, fog->id, origin, 0, 0, error))
        return false;
    if (out)
        *out = fog->id;
    return true;
}
bool q1_teleport_fog_think(qa_q1_game *g, q1_actor *fog, qa_error *error) {
    unsigned index = (unsigned)fminf(4, floorf(q1_random(g) * 5));
    char sound[24];
    snprintf(sound, sizeof(sound), "misc/r_tele%u.wav", index + 1);
    return q1_sound(g, fog->id, sound, 2, 1, error) && q1_remove(g, fog, error);
}
bool qa_q1_spawn_teledeath(qa_q1_game *g, qa_vec3 origin, qa_actor_id owner, qa_actor_id *out,
                           qa_error *error) {
    return q1_spawn_teledeath(g, origin, owner, 0.2, true, out, error);
}
bool q1_spawn_teledeath(qa_q1_game *g, qa_vec3 origin, qa_actor_id owner, double duration,
                        bool retouch, qa_actor_id *out, qa_error *error) {
    if (!g || !qa_vec_finite(origin) || !q1_alive(g, owner) ||
        (retouch && !g->host.force_retouch)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, owner.slot,
                     "Q1 teledeath requires a live owner and source retouch service");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner, &body, error))
        return false;
    body = (qa_body_state){.origin = origin,
                           .bounds = {qa_vec_sub(body.bounds.mins, qa_v3(1, 1, 1)),
                                      qa_vec_add(body.bounds.maxs, qa_v3(1, 1, 1))}};
    q1_actor *death;
    if (!q1_create(g, "teledeath", Q1_TIMER, owner, &death, error))
        return false;
    death->physics.solid = QA_PHYSICS_TRIGGER;
    if (!qa_world_body_write(g->services.world, death->id, &body, error) ||
        !q1_schedule(g, death, duration, Q1_THINK_REMOVE, error) || !q1_link(g, death, error) ||
        (retouch && !g->host.force_retouch(g->host.context, 2, error)))
        return false;
    if (out)
        *out = death->id;
    return true;
}
bool q1_teledeath_touch(qa_q1_game *g, q1_actor *death, qa_actor_id victim, qa_error *error) {
    if (!q1_ref_present(death->owner) || q1_ref_equal(q1_ref_from(g, victim), death->owner))
        return true;
    qa_q1_target target, owner;
    if (q1_target(g, victim, &target) && target.player) {
        if (qa_q1_game_invulnerable(g, victim) &&
            !qa_builtin_resource(&g->services, "teledeath2", &death->classname, error))
            return false;
        if (!q1_target(g, q1_ref_actor(g, death->owner), &owner) || !owner.player)
            return q1_damage_typed(g, q1_ref_actor(g, death->owner), death->id, death->id, 50000, QA_Q1_WEAPON_COUNT,
                                   QA_Q1_ARMOR_NORMAL, death->classname, error);
    }
    return q1_health(g, victim) == 0 ||
           q1_damage_typed(g, victim, death->id, death->id, 50000, QA_Q1_WEAPON_COUNT,
                           QA_Q1_ARMOR_NORMAL, death->classname, error);
}
