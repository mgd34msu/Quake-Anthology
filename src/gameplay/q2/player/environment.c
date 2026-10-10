#include "qa/q2_sound.h"
#include "internal.h"

bool q2_player_environment_damage(qa_q2_game *g, q2_actor *a, float amount, int means,
                                  uint32_t flags, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_attack attack = {
        .time_ns = g->now_ns,
        .inflictor = g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0},
        .combat_provider = g->options.owner,
        .cause = qa_q2_damage_cause(g->options.edition, g->options.product, means, flags)};
    attack.attacker = attack.inflictor;
    return q2_damage(g, &attack, a->id, amount, 0, means == 22 ? qa_v3(0, 0, 1) : qa_v3(0, 0, 0),
                     body.origin, qa_v3(0, 0, 0), false, e);
}
bool q2_player_environment(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m,
                           qa_error *e) {
    q2_client_state *s = a->client;
    uint64_t now = g->now_ns;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (s->info.noclip || s->info.spectator) {
        s->rule.air_ns = q2_deadline(now, 12 * Q2_NS);
        return true;
    }
    qa_q2_powerups powers;
    if (!qa_q2_powerups_read(g, a->id, &powers, e))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    int level = m->water_level, old = s->rule.old_water;
    s->rule.old_water = level;
    bool breather = powers.breather_until_ns > now, suit = powers.enviro_until_ns > now;
    if ((old == 0 && level != 0) || (old != 0 && level == 0)) {
        if (!q2_player_noise(g, a->id, body.origin, false, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_player_sound(g, a->id,
                             level == 0            ? QA_Q2_SOUND_PLAYER_WATR_OUT
                             : (m->water_type & 8) ? QA_Q2_SOUND_PLAYER_LAVA_IN
                                                   : QA_Q2_SOUND_PLAYER_WATR_IN,
                             4, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (old != 3 && level == 3 && !q2_player_sound(g, a->id, QA_Q2_SOUND_PLAYER_WATR_UN, 4, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e))
        return false;
    if (old == 3 && level != 3 && (!rr || combat.health > 0)) {
        if (s->rule.air_ns < now) {
            if (!q2_player_sound(g, a->id, QA_Q2_SOUND_PLAYER_GASP1, 2, e))
                return false;
            if (q2_actor_live(g, a->id) && !q2_player_noise(g, a->id, body.origin, false, e))
                return false;
        } else if (s->rule.air_ns < q2_deadline(now, 11 * Q2_NS) &&
                   !q2_player_sound(g, a->id, QA_Q2_SOUND_PLAYER_GASP2, 2, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (level == 3) {
        if (breather || suit) {
            s->rule.air_ns = q2_deadline(now, 10 * Q2_NS);
            uint64_t unit = rr ? Q2_MS : 100 * Q2_MS;
            bool positive = powers.breather_until_ns >= now;
            uint64_t delta =
                positive ? powers.breather_until_ns - now : now - powers.breather_until_ns;
            uint64_t remainder = delta / unit;
            if (positive ? delta % unit >= unit / 2 : delta % unit > unit / 2)
                remainder++;
            if (remainder % (rr ? 2500u : 25u) == 0) {
                if (!q2_player_sound(
                        g, a->id,
                        s->rule.breather_sound ? QA_Q2_SOUND_PLAYER_U_BREATH2 : QA_Q2_SOUND_PLAYER_U_BREATH1, 0, e))
                    return false;
                s->rule.breather_sound ^= 1;
                if (!q2_actor_live(g, a->id))
                    return true;
                if (!q2_player_noise(g, a->id, body.origin, false, e))
                    return false;
            }
        }
        if (!q2_actor_live(g, a->id))
            return true;
        if (s->rule.air_ns < now && s->rule.drown_ns < now && combat.health > 0) {
            s->rule.drown_ns = q2_deadline(now, Q2_NS);
            s->rule.drown_damage = s->rule.drown_damage + 2 > 15 ? 15 : s->rule.drown_damage + 2;
            const char *sound = combat.health <= (float)s->rule.drown_damage
                                    ? (rr ? QA_Q2_SOUND__DROWN1 : QA_Q2_SOUND_PLAYER_DROWN1)
                                : q2_random(g) < .5f ? QA_Q2_SOUND__GURP2
                                                     : QA_Q2_SOUND__GURP1;
            if (!q2_player_sound(g, a->id, sound, 2, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            s->rule.pain_ns = now;
            if (!q2_player_environment_damage(g, a, (float)s->rule.drown_damage, 17, 2, e))
                return false;
        } else if (rr && s->rule.air_ns <= q2_deadline(now, 3 * Q2_NS) && s->rule.drown_ns < now) {
            char sound[32];
            snprintf(sound, sizeof(sound), "player/wade%u.wav", 1 + (unsigned)((now / Q2_NS) % 3));
            s->rule.drown_ns = q2_deadline(now, Q2_NS);
            if (!q2_player_sound(g, a->id, sound, 2, e))
                return false;
        }
    } else {
        s->rule.air_ns = q2_deadline(now, 12 * Q2_NS);
        s->rule.drown_damage = 2;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (level && (m->water_type & 24) && (!rr || s->rule.slime_ns <= now)) {
        if (m->water_type & 8) {
            if (!qa_combat_read(g->services.combat, a->id, &combat, e))
                return false;
            if (combat.health > 0 && s->rule.pain_ns <= now && powers.invulnerability_until_ns < now) {
                s->rule.pain_ns = q2_deadline(now, Q2_NS);
                if (!q2_player_sound(g, a->id,
                                     q2_random(g) < .5f ? QA_Q2_SOUND_PLAYER_BURN2 : QA_Q2_SOUND_PLAYER_BURN1,
                                     2, e))
                    return false;
            }
            if (!q2_actor_live(g, a->id))
                return true;
            if (!q2_player_environment_damage(g, a, (float)((suit ? 1 : 3) * level), 19, 0, e))
                return false;
        }
        if (!q2_actor_live(g, a->id))
            return true;
        if ((m->water_type & 16) && !suit &&
            !q2_player_environment_damage(g, a, (float)level, 18, 0, e))
            return false;
        s->rule.slime_ns = q2_deadline(now, 100 * Q2_MS);
    }
    return true;
}
bool q2_player_falling(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m, qa_error *e) {
    q2_client_state *s = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (s->info.noclip || s->info.spectator || (!rr && !m->animate_q2) || m->water_level == 3)
        return true;
    float delta;
    if (rr) {
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, a->id, &combat, e))
            return false;
        if (s->info.dead || combat.health <= 0 || m->grapple_attached ||
            m->grapple_released_until_ns >= g->now_ns)
            return true;
        delta = m->impact_delta;
    } else {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        if (s->rule.old_velocity.z < 0 && body.velocity.z > s->rule.old_velocity.z && !m->grounded)
            delta = s->rule.old_velocity.z;
        else {
            if (!m->grounded)
                return true;
            delta = body.velocity.z - s->rule.old_velocity.z;
        }
    }
    delta *= delta * .0001f;
    if (m->water_level == 2)
        delta *= .25f;
    else if (m->water_level == 1)
        delta *= .5f;
    if (delta < 1)
        return true;
    if (rr)
        s->rule.bob_time = 0;
    if (s->rule.landmark_free_fall) {
        delta = fminf(30, delta);
        s->rule.landmark_free_fall = false;
        s->rule.landmark_noise_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    }
    if (delta < 15) {
        if (!rr || !m->on_ladder)
            s->rule.event = 2;
        return true;
    }
    s->rule.fall_value = fminf(delta * .5f, 40);
    uint64_t fall = rr ? g->frame_ns < 400 * Q2_MS ? 400 * Q2_MS - g->frame_ns : 0 : 300 * Q2_MS;
    s->rule.fall_ns = q2_deadline(g->now_ns, fall);
    if (delta > 30) {
        s->rule.event = delta >= 55 ? 5 : 4;
        s->rule.pain_ns = q2_deadline(g->now_ns, rr ? g->frame_ns : 0);
        bool no_damage = g->options.deathmatch && (rr ? g->player_runtime->rules.no_fall_damage
                                                      : (g->options.deathmatch_flags & 8) != 0);
        if (!no_damage &&
            !q2_player_environment_damage(g, a, fmaxf(1, truncf((delta - 30) / 2)), 22, 0, e))
            return false;
    } else
        s->rule.event = 3;
    if (rr && q2_actor_live(g, a->id)) {
        qa_body_state body;
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, a->id, &combat, e) ||
            !qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        if (combat.health != 0)
            return q2_player_noise(g, a->id, body.origin, false, e);
    }
    return true;
}
