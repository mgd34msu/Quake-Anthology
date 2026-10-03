#include "internal.h"

typedef enum melee_side { MELEE_CENTER, MELEE_LEFT, MELEE_RIGHT } melee_side;
typedef struct melee_action {
    const char *callback;
    melee_side side;
    float height, damage, random_damage, kick;
    const char *before, *connected, *missed;
    int channel;
    float rerelease_miss_seconds;
} melee_action;

static const melee_action actions[] = {
    {"berserk_attack_spike", MELEE_CENTER, -24, 15, 6, 400, NULL, NULL, NULL, 0, 0},
    {"berserk_attack_club", MELEE_LEFT, -4, 5, 6, 400, NULL, NULL, NULL, 0, 0},
    {"brain_hit_right", MELEE_RIGHT, 8, 15, 5, 40, NULL, "brain/melee3.wav", NULL, 1, 3},
    {"brain_hit_left", MELEE_LEFT, 8, 15, 5, 40, NULL, "brain/melee3.wav", NULL, 1, 3},
    {"ChickSlash", MELEE_LEFT, 10, 10, 6, 100, "chick/chkatck3.wav", NULL, NULL, 0, 0},
    {"flipper_bite", MELEE_CENTER, 0, 5, 0, 0, NULL, NULL, NULL, 0, 0},
    {"floater_wham", MELEE_CENTER, 0, 5, 6, -50, "floater/fltatck3.wav", NULL, NULL, 1, 3},
    {"flyer_slash_left", MELEE_LEFT, 0, 5, 0, 0, NULL, "flyer/flyatck2.wav", "flyer/flyatck2.wav", 1, 1.5f},
    {"flyer_slash_right", MELEE_RIGHT, 0, 5, 0, 0, NULL, "flyer/flyatck2.wav", "flyer/flyatck2.wav", 1, 1.5f},
    {"GaldiatorMelee", MELEE_LEFT, -4, 20, 5, 300, NULL, "gladiator/melee2.wav", "gladiator/melee3.wav", 0, 1.5f},
    {"GladiatorMelee", MELEE_LEFT, -4, 20, 5, 300, NULL, "gladiator/melee2.wav", "gladiator/melee3.wav", 0, 1.5f},
    {"GladbMelee", MELEE_LEFT, -4, 20, 5, 300, NULL, "gladiator/melee2.wav", "gladiator/melee3.wav", 0, 0},
    {"infantry_smack", MELEE_CENTER, 0, 5, 5, 50, NULL, "infantry/melee2.wav", NULL, 1, 1.5f},
    {"mutant_hit_left", MELEE_LEFT, 8, 10, 5, 100, NULL, "mutant/mutatck2.wav", "mutant/mutatck1.wav", 1, 1.5f},
    {"mutant_hit_right", MELEE_RIGHT, 8, 10, 5, 100, NULL, "mutant/mutatck3.wav", "mutant/mutatck1.wav", 1, 1.5f},
    {"gekk_hit_left", MELEE_LEFT, 8, 15, 5, 100, NULL, "gek/gk_atck2.wav", "gek/gk_atck1.wav", 1, 0},
    {"gekk_hit_right", MELEE_RIGHT, 8, 15, 5, 100, NULL, "gek/gk_atck3.wav", "gek/gk_atck1.wav", 1, 0},
    {"gekk_bite", MELEE_CENTER, 0, 5, 0, 0, NULL, NULL, NULL, 0, 0},
    {"stalker_swing_attack", MELEE_CENTER, 0, 5, 5, 50, NULL, NULL, NULL, 1, .8f},
    {"arachnid_melee_hit", MELEE_CENTER, 0, 15, 0, 50, NULL, NULL, NULL, 0, 1},
    {"guardian_kick", MELEE_CENTER, -80, 85, 0, 700, NULL, NULL, NULL, 0, 1},
};

bool q2m_species_melee(q2m_context *context, const char *callback, bool *handled,
                       qa_error *error) {
    *handled = false;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    struct qa_q2_monster *monster = context->monster;
    if (!strcmp(callback, "sham_smash10") || !strcmp(callback, "ShamClaw")) {
        *handled = true;
        bool smash = !strcmp(callback, "sham_smash10");
        if (!q2_actor_live(context->game, monster->enemy))
            return true;
        if (!q2m_run_ai(context, Q2M_AI_CHARGE, NULL, smash ? 0 : 10, error))
            return false;
        if (!q2m_alive(context))
            return true;
        qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q2);
        policy.contents_mask = 1u;
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
               q2m_sound(context, "shambler/smack.wav", 1, 1, error);
    }
    if (!strcmp(callback, "brain_tentacle_attack")) {
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
        return q2m_sound(context, "brain/brnatck3.wav", 1, 1, error);
    }
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        const melee_action *action = &actions[i];
        if (strcmp(callback, action->callback))
            continue;
        *handled = true;
        if (action->before && !q2m_sound(context, action->before, 1, 1, error))
            return false;
        if (!q2m_alive(context))
            return true;
        float damage = action->damage, random_damage = action->random_damage,
              kick = action->kick, miss_seconds = action->rerelease_miss_seconds;
        if (rerelease && !strcmp(callback, "berserk_attack_spike")) {
            damage = 5;
            kick = 80;
            miss_seconds = 1.2f;
        } else if (rerelease && !strcmp(callback, "berserk_attack_club")) {
            damage = 15;
            miss_seconds = 2.5f;
        } else if (rerelease && monster->definition->species == Q2M_MUTANT) {
            damage = 5;
            random_damage = 10;
        }
        if (rerelease && !strcmp(callback, "floater_wham"))
            damage += (float)q2_random_bounded(context->game, 6);
        else if (random_damage != 0)
            damage += floorf(q2m_random(context->game) * random_damage);
        float side = action->side == MELEE_LEFT ? context->body.bounds.mins.x
                   : action->side == MELEE_RIGHT ? context->body.bounds.maxs.x : 0;
        bool hit;
        float reach = rerelease && (!strcmp(callback, "stalker_swing_attack") ||
                                    !strcmp(callback, "guardian_kick")) ? 50 : 80;
        if (!q2m_hit(context, qa_v3(reach, side, action->height), damage, kick, &hit, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!hit && rerelease && miss_seconds != 0)
            monster->melee_ns = q2m_after(context->game->now_ns, miss_seconds);
        const char *sound = hit ? action->connected : action->missed;
        if (hit && !strcmp(callback, "stalker_swing_attack"))
            sound = monster->frame < 60 ? "stalker/melee2.wav" : "stalker/melee1.wav";
        return !sound || q2m_sound(context, sound,
                                  !strcmp(callback, "stalker_swing_attack") ? 1 : action->channel,
                                  1, error);
    }
    return true;
}
