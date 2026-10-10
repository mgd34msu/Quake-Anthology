#include "qa/q2_sound.h"
#include "internal.h"

typedef enum melee_side { MELEE_CENTER, MELEE_LEFT, MELEE_RIGHT } melee_side;
typedef struct melee_action {
    q2m_callback_id callback;
    melee_side side;
    float height, damage, random_damage, kick;
    const char *before, *connected, *missed;
    int channel;
    float rerelease_miss_seconds;
} melee_action;

static const melee_action actions[Q2M_CALLBACK_COUNT] = {
    [Q2M_CALLBACK_berserk_attack_spike]={Q2M_CALLBACK_berserk_attack_spike, MELEE_CENTER, -24, 15, 6, 400, NULL, NULL, NULL, 0, 0},
    [Q2M_CALLBACK_berserk_attack_club]={Q2M_CALLBACK_berserk_attack_club, MELEE_LEFT, -4, 5, 6, 400, NULL, NULL, NULL, 0, 0},
    [Q2M_CALLBACK_brain_hit_right]={Q2M_CALLBACK_brain_hit_right, MELEE_RIGHT, 8, 15, 5, 40, NULL, QA_Q2_SOUND_BRAIN_MELEE3, NULL, 1, 3},
    [Q2M_CALLBACK_brain_hit_left]={Q2M_CALLBACK_brain_hit_left, MELEE_LEFT, 8, 15, 5, 40, NULL, QA_Q2_SOUND_BRAIN_MELEE3, NULL, 1, 3},
    [Q2M_CALLBACK_ChickSlash]={Q2M_CALLBACK_ChickSlash, MELEE_LEFT, 10, 10, 6, 100, QA_Q2_SOUND_CHICK_CHKATCK3, NULL, NULL, 0, 0},
    [Q2M_CALLBACK_flipper_bite]={Q2M_CALLBACK_flipper_bite, MELEE_CENTER, 0, 5, 0, 0, NULL, NULL, NULL, 0, 0},
    [Q2M_CALLBACK_floater_wham]={Q2M_CALLBACK_floater_wham, MELEE_CENTER, 0, 5, 6, -50, QA_Q2_SOUND_FLOATER_FLTATCK3, NULL, NULL, 1, 3},
    [Q2M_CALLBACK_flyer_slash_left]={Q2M_CALLBACK_flyer_slash_left, MELEE_LEFT, 0, 5, 0, 0, NULL, QA_Q2_SOUND_FLYER_FLYATCK2, QA_Q2_SOUND_FLYER_FLYATCK2, 1, 1.5f},
    [Q2M_CALLBACK_flyer_slash_right]={Q2M_CALLBACK_flyer_slash_right, MELEE_RIGHT, 0, 5, 0, 0, NULL, QA_Q2_SOUND_FLYER_FLYATCK2, QA_Q2_SOUND_FLYER_FLYATCK2, 1, 1.5f},
    [Q2M_CALLBACK_GaldiatorMelee]={Q2M_CALLBACK_GaldiatorMelee, MELEE_LEFT, -4, 20, 5, 300, NULL, QA_Q2_SOUND_GLADIATOR_MELEE2, QA_Q2_SOUND_GLADIATOR_MELEE3, 0, 1.5f},
    [Q2M_CALLBACK_GladiatorMelee]={Q2M_CALLBACK_GladiatorMelee, MELEE_LEFT, -4, 20, 5, 300, NULL, QA_Q2_SOUND_GLADIATOR_MELEE2, QA_Q2_SOUND_GLADIATOR_MELEE3, 0, 1.5f},
    [Q2M_CALLBACK_GladbMelee]={Q2M_CALLBACK_GladbMelee, MELEE_LEFT, -4, 20, 5, 300, NULL, QA_Q2_SOUND_GLADIATOR_MELEE2, QA_Q2_SOUND_GLADIATOR_MELEE3, 0, 0},
    [Q2M_CALLBACK_infantry_smack]={Q2M_CALLBACK_infantry_smack, MELEE_CENTER, 0, 5, 5, 50, NULL, QA_Q2_SOUND_INFANTRY_MELEE2, NULL, 1, 1.5f},
    [Q2M_CALLBACK_mutant_hit_left]={Q2M_CALLBACK_mutant_hit_left, MELEE_LEFT, 8, 10, 5, 100, NULL, QA_Q2_SOUND_MUTANT_MUTATCK2, QA_Q2_SOUND_MUTANT_MUTATCK1, 1, 1.5f},
    [Q2M_CALLBACK_mutant_hit_right]={Q2M_CALLBACK_mutant_hit_right, MELEE_RIGHT, 8, 10, 5, 100, NULL, QA_Q2_SOUND_MUTANT_MUTATCK3, QA_Q2_SOUND_MUTANT_MUTATCK1, 1, 1.5f},
    [Q2M_CALLBACK_gekk_hit_left]={Q2M_CALLBACK_gekk_hit_left, MELEE_LEFT, 8, 15, 5, 100, NULL, QA_Q2_SOUND_GEK_GK_ATCK2, QA_Q2_SOUND_GEK_GK_ATCK1, 1, 0},
    [Q2M_CALLBACK_gekk_hit_right]={Q2M_CALLBACK_gekk_hit_right, MELEE_RIGHT, 8, 15, 5, 100, NULL, QA_Q2_SOUND_GEK_GK_ATCK3, QA_Q2_SOUND_GEK_GK_ATCK1, 1, 0},
    [Q2M_CALLBACK_gekk_bite]={Q2M_CALLBACK_gekk_bite, MELEE_CENTER, 0, 5, 0, 0, NULL, NULL, NULL, 0, 0},
    [Q2M_CALLBACK_stalker_swing_attack]={Q2M_CALLBACK_stalker_swing_attack, MELEE_CENTER, 0, 5, 5, 50, NULL, NULL, NULL, 1, .8f},
    [Q2M_CALLBACK_arachnid_melee_hit]={Q2M_CALLBACK_arachnid_melee_hit, MELEE_CENTER, 0, 15, 0, 50, NULL, NULL, NULL, 0, 1},
    [Q2M_CALLBACK_guardian_kick]={Q2M_CALLBACK_guardian_kick, MELEE_CENTER, -80, 85, 0, 700, NULL, NULL, NULL, 0, 1},
};

bool q2m_species_melee(q2m_context *context, q2m_callback_id callback, bool *handled,
                       qa_error *error) {
    *handled = false;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    struct qa_q2_monster *monster = context->monster;
    if (callback == Q2M_CALLBACK_guncmdr_kick) {
        *handled = true;
        qa_actor_id enemy = monster->enemy;
        bool hit;
        if (!q2m_hit(context, qa_v3(80, 0, -32), 15, 400, &hit, error))
            return false;
        if (!q2m_alive(context) || !hit || !q2_actor_live(context->game, enemy))
            return true;
        qa_builtin_actor_traits traits = {0};
        if (!context->game->services.actor_traits ||
            !context->game->services.actor_traits(context->game->services.context,
                                                   enemy, &traits) || !traits.player)
            return true;
        if (!q2m_alive(context) || !q2_actor_live(context->game, enemy))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(context->game->services.world, enemy, &body, error))
            return false;
        if (body.velocity.z < 270) {
            body.velocity.z = 270;
            return qa_world_body_write(context->game->services.world, enemy,
                                         &body, error);
        }
        return true;
    }
    if (callback == Q2M_CALLBACK_brain_tounge_attack) {
        *handled = true;
        qa_actor_id enemy = monster->enemy;
        if (!q2_actor_live(context->game, enemy))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(context->game->services.world, enemy, &body, error))
            return false;
        qa_vec3 start = q2m_project_offset(context, qa_v3(24, 0, 16));
        qa_vec3 endpoints[] = {body.origin, body.origin, body.origin};
        endpoints[1].z += body.bounds.maxs.z - 8;
        endpoints[2].z += body.bounds.mins.z + 8;
        bool reachable = false;
        for (size_t i = 0; i < sizeof(endpoints) / sizeof(*endpoints); ++i) {
            qa_vec3 delta = qa_vec_sub(start, endpoints[i]);
            if (qa_vec_length(delta) <= 512 &&
                fabsf(q2m_vector_angles(delta).x) <= 30) {
                reachable = true;
                break;
            }
        }
        if (!reachable)
            return true;
        qa_trace_query query = {.start = start, .end = body.origin,
            .pass_actor = context->actor->id,
            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = qa_collision_contents_mask(Q2M_ATTACK_MASK, QA_COLLISION_Q2);
        qa_trace_result trace;
        if (!qa_world_trace(context->game->services.world, &query, &trace, error))
            return false;
        if (!q2m_alive(context) || trace.hit != QA_TRACE_HIT_ACTOR ||
            !qa_actor_id_equal(trace.actor, enemy))
            return true;
        if (!q2m_sound(context, QA_Q2_SOUND_BRAIN_BRNATCK3, 1, 1, error) ||
            !q2m_emit(context, QA_BUILTIN_BEAM, "q2:parasite", 0, start,
                        body.origin, 0, error))
            return false;
        if (!q2m_alive(context) || !q2_actor_live(context->game, enemy))
            return true;
        qa_attack attack = {.attacker = context->actor->id,
            .inflictor = context->actor->id,
            .combat_provider = context->game->options.owner,
            .cause = qa_q2_damage_cause(context->game->options.edition,
                context->game->options.product, 36, 8u)};
        if (!q2_damage(context->game, &attack, enemy, 5, 0,
                         qa_vec_sub(start, body.origin), body.origin,
                         qa_v3(0, 0, 0), false, error))
            return false;
        if (!q2m_alive(context) || !q2_actor_live(context->game, enemy))
            return true;
        context->body.origin.z += 1;
        if (!q2m_write_body(context, false, error))
            return false;
        if (!q2m_alive(context) || !q2_actor_live(context->game, enemy))
            return true;
        if (!qa_world_body_read(context->game->services.world, enemy, &body, error))
            return false;
        qa_vec3 forward;
        qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
        body.velocity = qa_vec_scale(forward, -1200);
        return qa_world_body_write(context->game->services.world, enemy, &body, error);
    }
    if ((callback == Q2M_CALLBACK_sham_smash10) || (callback == Q2M_CALLBACK_ShamClaw)) {
        *handled = true;
        bool smash = (callback == Q2M_CALLBACK_sham_smash10);
        if (!q2_actor_live(context->game, monster->enemy))
            return true;
        if (!q2m_run_ai(context, Q2M_AI_CHARGE, smash ? 0 : 10, error))
            return false;
        if (!q2m_alive(context))
            return true;
        qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q2);
        policy.contents_mask = qa_collision_contents_mask(1u, QA_COLLISION_Q2);
        bool visible;
        if (!qa_builtin_can_damage(&context->game->services, context->body.origin,
                                    monster->enemy, context->actor->id, policy, true,
                                    &visible, error))
            return false;
        if (!q2m_alive(context) || !visible)
            return true;
        bool hit;
        if (!q2m_hit(context, qa_v3(80, context->body.bounds.mins.x, -4),
                      (smash ? 110 : 70) + floorf(q2m_random(context->game) * 10),
                      smash ? 120 : 80, &hit, error))
            return false;
        return !q2m_alive(context) || !hit ||
               q2m_sound(context, QA_Q2_SOUND_SHAMBLER_SMACK, 1, 1, error);
    }
    if (callback == Q2M_CALLBACK_brain_tentacle_attack) {
        *handled = true;
        bool hit;
        if (!q2m_hit(context, qa_v3(80, 0, 8),
                      10 + floorf(q2m_random(context->game) * 5), -600, &hit, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (hit && rerelease)
            monster->count = 1;
        else if (hit && context->game->options.skill > 0)
            monster->spawnflags |= UINT32_C(65536);
        if (!hit && rerelease)
            monster->melee_ns = q2m_after(context->game->now_ns, 3);
        return q2m_sound(context, QA_Q2_SOUND_BRAIN_BRNATCK3, 1, 1, error);
    }
    const melee_action *action = actions + callback;
    if (action->callback == callback) {
        *handled = true;
        if (action->before && !q2m_sound(context, action->before, 1, 1, error))
            return false;
        if (!q2m_alive(context))
            return true;
        float damage = action->damage, random_damage = action->random_damage,
              kick = action->kick, miss_seconds = action->rerelease_miss_seconds;
        if (rerelease && (callback == Q2M_CALLBACK_berserk_attack_spike)) {
            damage = 5;
            kick = 80;
            miss_seconds = 1.2f;
        } else if (rerelease && (callback == Q2M_CALLBACK_berserk_attack_club)) {
            damage = 15;
            miss_seconds = 2.5f;
        } else if (rerelease && monster->definition->species == Q2M_MUTANT) {
            damage = 5;
            random_damage = 10;
        }
        if (rerelease && (callback == Q2M_CALLBACK_floater_wham))
            damage += (float)q2_random_bounded(context->game, 6);
        else if (random_damage != 0)
            damage += floorf(q2m_random(context->game) * random_damage);
        float side = action->side == MELEE_LEFT ? context->body.bounds.mins.x
                   : action->side == MELEE_RIGHT ? context->body.bounds.maxs.x : 0;
        bool hit;
        float reach = rerelease && ((callback == Q2M_CALLBACK_stalker_swing_attack) ||
                                    (callback == Q2M_CALLBACK_guardian_kick)) ? 50 : 80;
        if (!q2m_hit(context, qa_v3(reach, side, action->height), damage, kick, &hit, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!hit && rerelease && miss_seconds != 0)
            monster->melee_ns = q2m_after(context->game->now_ns, miss_seconds);
        const char *sound = hit ? action->connected : action->missed;
        if (hit && (callback == Q2M_CALLBACK_stalker_swing_attack))
            sound = monster->frame < 60 ? QA_Q2_SOUND_STALKER_MELEE2 : QA_Q2_SOUND_STALKER_MELEE1;
        return !sound || q2m_sound(context, sound,
                                  (callback == Q2M_CALLBACK_stalker_swing_attack) ? 1 : action->channel,
                                  1, error);

    }
    return true;
}
