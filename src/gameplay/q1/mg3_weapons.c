#include "internal.h"

static bool attack_delay(qa_q1_game *g, q1_player *player, float delay, qa_error *error) {
    qa_q1_weapon_parameters parameters = {.interval = delay, .nail_speed = 1000};
    if (!q1_weapon_parameters(g, player->id, QA_Q1_MG3_MJOLNIR, &parameters, error))
        return false;
    if (!q1_weapon_attack_delay(g, player, &parameters.interval, error))
        return false;
    player->attack_finished = g->time + parameters.interval;
    return true;
}
bool q1_mg3_hammer_fire(qa_q1_game *g, q1_player *player, qa_error *error) {
    q1_actor *strike;
    if (!q1_create(g, "mg3_hammer_strike", Q1_TIMER, player->id, &strike, error))
        return false;
    player->mg3_hammer_body = q1_ammo_count(g, player->id, QA_Q1_CELLS) < 30 ? 31 : 37;
    if (!q1_schedule(g, strike, q1_weapon_shape(player->weapon)->launch_delay,
                     Q1_THINK_MG3_HAMMER, error))
        return false;
    player->continuous = false;
    player->animation_at = g->time;
    player->animation_base = player->weapon_frame = 1;
    player->hostile_until = g->time + 1;
    return attack_delay(g, player, q1_weapon_interval(player->weapon), error) &&
           q1_weapon_event(g, player, 0, 0, error);
}
bool q1_mg3_hammer_strike(qa_q1_game *g, q1_actor *strike, qa_error *error) {
    q1_player *player = q1_player_get(g, q1_ref_actor(g, strike->owner));
    if (!player)
        return q1_remove(g, strike, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player->id, &body, error))
        return false;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(player->input.view_angles, &forward, &right, &up);
    qa_vec3 source = qa_vec_add(body.origin, qa_v3(0, 0, 16));
    qa_trace_result trace;
    if (!q1_trace(g, source, qa_vec_add(source, qa_vec_scale(forward, 64)), player->id, true,
                  &trace, error))
        return false;
    qa_vec3 origin = qa_vec_sub(trace.end, qa_vec_scale(forward, 4));
    if (!attack_delay(g, player, 0.4f, error))
        return false;
    if (trace.hit == QA_TRACE_HIT_ACTOR && q1_damageable(g, trace.actor)) {
        if (!q1_sound(g, player->id, "hipweap/mjolslap.wav", 1, 1, error) ||
            !q1_effect(g, QA_BUILTIN_IMPACT, trace.actor, origin, 40, 1, error))
            return false;
        if (!q1_alive(g, player->id) || !q1_alive(g, strike->id))
            return true;
        q1_actor *victim = q1_entity(g, trace.actor);
        if (victim)
            victim->axe_hit = true;
        float damage = q1_classnamed(g, trace.actor, "monster_zombie") ||
                               q1_classnamed(g, trace.actor, "monster_szombie")
                           ? 120
                       : q1_health(g, trace.actor) < 40 ? 80
                                                        : 40;
        if (player->mg3_hammer_until > g->time) {
            if (q1_ref_equal(player->mg3_hammer_target, q1_ref_from(g, trace.actor)) &&
                player->input.water_level < 2 && q1_ammo_count(g, player->id, QA_Q1_CELLS) >= 15) {
                qa_trace_result floor;
                if (!q1_trace(g, source,
                              qa_vec_sub(source,
                                         qa_vec_scale(up, player->input.water_level < 1 ? 30 : 15)),
                              player->id, true, &floor, error) ||
                    !q1_consume(g, player->id, QA_Q1_CELLS, 15, error) ||
                    !q1_hipnotic_hammer_base(g, player, floor.end, QA_Q1_MG3_MJOLNIR, error))
                    return false;
            } else if (!q1_damage(g, trace.actor, player->id, player->id, damage, QA_Q1_MG3_MJOLNIR,
                                  error))
                return false;
            if (!q1_alive(g, player->id))
                return !q1_alive(g, strike->id) || q1_remove(g, strike, error);
            player->mg3_hammer_until = g->time;
            if (!attack_delay(g, player, 0.5f, error))
                return false;
        } else {
            player->mg3_hammer_until = g->time + 0.5;
            player->mg3_hammer_target = q1_ref_from(g, trace.actor);
            if (!attack_delay(g, player, 0.2f, error))
                return false;
            if (q1_ammo_count(g, player->id, QA_Q1_CELLS) >= 15)
                player->mg3_hammer_glow = true;
            if (!q1_damage(g, trace.actor, player->id, player->id, damage, QA_Q1_MG3_MJOLNIR,
                           error))
                return false;
        }
    } else {
        if (trace.fraction != 1) {
            if (!q1_sound(g, player->id, "hipweap/mjoltink.wav", 1, 1, error) ||
                !q1_effect(g, QA_BUILTIN_IMPACT, strike->id, origin, 1, 2, error))
                return false;
        } else if (!q1_sound(g, player->id, "weapons/ax1.wav", 1, 1, error))
            return false;
        player->mg3_hammer_target = (q1_ref){0};
    }
    if (q1_alive(g, player->id) && !q1_weapon_event(g, player, 0, 0, error))
        return false;
    return !q1_alive(g, strike->id) || q1_remove(g, strike, error);
}
bool qa_q1_mg3_hammer_body_frame(const qa_q1_game *g, qa_actor_id actor, int32_t *frame) {
    if (!g || !frame || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->active || !qa_actor_id_equal(player->id, actor) ||
        player->weapon != QA_Q1_MG3_MJOLNIR || player->animation_at < 0 ||
        player->weapon_frame < 1 || player->weapon_frame > 4)
        return false;
    *frame = player->mg3_hammer_body + player->weapon_frame;
    return true;
}
bool q1_mg3_weapon_frame(qa_q1_game *g, q1_player *player, qa_error *error) {
    if (!player->arsenal || q1_health(g, player->id) <= 0)
        return true;
    if (!q1_alive(g, player->id))
        return true;
    if (player->weapon == QA_Q1_MG3_MJOLNIR && player->mg3_hammer_glow &&
        player->mg3_hammer_until <= g->time) {
        player->mg3_hammer_glow = false;
        if (!q1_weapon_event(g, player, 0, 0, error))
            return false;
        if (!q1_alive(g, player->id))
            return true;
    }
    if (g->time <= player->attack_finished || player->weapon == QA_Q1_AXE ||
        player->weapon == QA_Q1_MG3_MJOLNIR)
        return true;
    int ammo = q1_weapon_ammo(player->weapon);
    if (ammo < 0 || q1_ammo_count(g, player->id, (qa_q1_ammo)ammo) != 0)
        return true;
    if (!q1_alive(g, player->id))
        return true;
    static const char *const names[] = {"item_shells", "item_spikes", "item_rockets", "item_cells"};
    if (ammo < 4) {
        for (uint32_t i = 0; i < g->capacity; ++i) {
            q1_actor *item = g->actors[i];
            if (!item || item->kind != Q1_PICKUP || item->state.pickup.external ||
                !(item->spawnflags & 8) || item->physics.solid != QA_PHYSICS_NOT_SOLID ||
                !q1_classnamed(g, item->id, names[ammo]))
                continue;
            if (!q1_schedule(g, item, 0.5 * q1_random(g), Q1_THINK_RESPAWN, error))
                return false;
            if (!q1_alive(g, player->id))
                return true;
        }
    }
    return qa_q1_player_select(g, player->id, q1_best_weapon(g, player), error);
}
