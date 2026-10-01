#include "combat_internal.h"
#include <limits.h>

static bool self_damage(const qa_damage_request *request) {
    return request->attack.attacker.registry &&
           qa_actor_id_equal(request->target, request->attack.attacker);
}
static bool same_team(const qa_combat_state *target, const qa_combat_state *attacker) {
    return attacker && target->team && target->team == attacker->team;
}
static bool current(qa_combat *combat, const qa_damage_request *request, qa_combat_state *victim,
                    qa_combat_state *attacker, bool *has_attacker, qa_error *error) {
    if (!qa_combat_read(combat, request->target, victim, error))
        return false;
    *has_attacker = false;
    if (qa_combat_live(combat, request->attack.attacker)) {
        qa_error ignored = {0};
        if (qa_combat_read(combat, request->attack.attacker, attacker, &ignored))
            *has_attacker = true;
        else if (ignored.code != QA_ERROR_NOT_FOUND) {
            if (error)
                *error = ignored;
            return false;
        }
    }
    return true;
}
static bool describe(qa_combat *combat, const qa_combat_policy *policy,
                     const qa_damage_request *request, qa_combat_state *target,
                     qa_combat_state *attacker, bool *has_attacker, qa_combat_context *context,
                     qa_error *error) {
    if (!current(combat, request, target, attacker, has_attacker, error))
        return false;
    *context = (qa_combat_context){0};
    ++combat->active_calls;
    bool ok = policy->describe(policy->context, request, target, *has_attacker ? attacker : NULL,
                               context, error);
    --combat->active_calls;
    return ok;
}
static bool effect(qa_combat *combat, const qa_combat_policy *policy, qa_damage_effect_stage stage,
                   const qa_damage_request *request, qa_damage_effect *value, qa_error *error) {
    if (policy->effect) {
        ++combat->active_calls;
        bool ok = policy->effect(policy->context, combat, stage, request, value, error);
        --combat->active_calls;
        if (!ok)
            return false;
    }
    if (combat->hooks.effect && qa_combat_live(combat, request->target)) {
        ++combat->active_calls;
        bool ok = combat->hooks.effect(combat->hooks.context, combat, stage, request, value, error);
        --combat->active_calls;
        if (!ok)
            return false;
    }
    if (!isfinite(value->amount) || value->reaction < QA_REACTION_NONE ||
        value->reaction > QA_REACTION_DEATH)
        return qa_combat_argument(error, "source damage effect returned an invalid result");
    return true;
}
static bool lethal_health(qa_combat *combat, const qa_combat_policy *policy,
                          const qa_damage_request *request, float *health,
                          qa_reaction *reaction, bool *changed, qa_error *error) {
    if (changed) *changed = false;
    if (*health > 0) return true;
    qa_damage_effect value = {.amount = *health, .allowed = true,
                              .reaction = QA_REACTION_DEATH};
    if (!effect(combat, policy, QA_DAMAGE_LETHAL_HEALTH, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) return true;
    if (changed) *changed = value.amount != *health || value.reaction != *reaction;
    *health = value.amount;
    *reaction = value.reaction;
    return true;
}

static bool integer(float value, int32_t *out, qa_error *error) {
    double truncated = trunc((double)value);
    if (truncated < INT32_MIN || truncated > INT32_MAX || !isfinite(truncated))
        return qa_combat_argument(error, "source damage exceeds its signed integer representation");
    *out = (int32_t)truncated;
    return true;
}
static int32_t signed_word(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value
                              : (int32_t)(value - UINT32_C(2147483648)) + INT32_MIN;
}
static int32_t multiply_integer(int32_t a, int32_t b) {
    return signed_word((uint32_t)a * (uint32_t)b);
}
/* Q2/Q3 weapons already apply their source power multiplier before damage.
 * They still expose the same ordered attachment boundaries as native Q1. */
static bool weapon_damage(qa_combat *combat, const qa_combat_policy *policy,
                          const qa_damage_request *request, qa_damage_effect *value,
                          qa_error *error) {
    *value = (qa_damage_effect){.amount = request->amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_QUAD, request, value, error))
        return false;
    if (!value->allowed || !qa_combat_live(combat, request->target)) {
        value->allowed = false;
        return true;
    }
    if (!effect(combat, policy, QA_DAMAGE_AFTER_QUAD, request, value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        value->allowed = false;
    return true;
}
static bool absorb(qa_combat *combat, const qa_combat_policy *policy,
                   const qa_damage_request *request, qa_protection_channel channel, float amount,
                   qa_damage_flags flags, float *saved, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *saved = 0;
        return true;
    }
    return qa_combat_absorb(combat, request, channel, NULL, amount, flags, &context.armor, saved,
                            error);
}

static bool q1_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_damage_effect value = {.amount = request->amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_QUAD, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (context.game.q1.quad && !request->attack.powerup_applied)
        value.amount *= 4;
    if (!effect(combat, policy, QA_DAMAGE_AFTER_QUAD, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    float damage = value.amount;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q1_combat_context source = context.game.q1;
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    bool armor_allowed = value.allowed;
    float power = 0, regular = 0;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (armor_allowed && value.allowed &&
        !absorb(combat, policy, request, QA_PROTECTION_POWERED, damage, flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = damage - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    float after_power = value.amount;
    if (armor_allowed && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, after_power, flags,
                                 &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    float take = ceilf(after_power - regular);
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (source.walk && !target.no_knockback && source.has_momentum_direction &&
        !qa_combat_impulse(combat, request, source.momentum_direction, damage * 8, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    bool protected_health = target.invulnerable;
    if (protected_health) {
        value = (qa_damage_effect){.amount = damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &value, error))
            return false;
        protected_health = value.allowed;
    }
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (protected_health || (!source.skip_base_team_health && source.teamplay == 1 &&
                             same_team(&target, has_attacker ? &attacker : NULL)))
        return true;
    value = (qa_damage_effect){.amount = damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
        return false;
    if (!value.allowed || !qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = take, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    take = value.amount;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    float health = fmaxf(-99, target.health - take);
    qa_reaction reaction = health <= 0 ? QA_REACTION_DEATH : QA_REACTION_PAIN;
    if (!lethal_health(combat, policy, request, &health, &reaction, NULL, error))
        return false;
    if (!qa_combat_live(combat, request->target)) return true;
    if (!isfinite(health) || !isfinite(take))
        return qa_combat_argument(error, "Q1 damage arithmetic overflow");
    if (!qa_combat_set_health(combat, request->target, health, error))
        return false;
    *result = (qa_damage_result){.applied_damage = take, .reaction = reaction};
    return true;
}

static bool q2_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q2_combat_context source = context.game.q2;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    int32_t damage;
    qa_damage_effect input;
    if (!weapon_damage(combat, policy, request, &input, error))
        return false;
    if (!input.allowed)
        return true;
    if (policy->effect || combat->hooks.effect) {
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
            return true;
        source = context.game.q2;
    }
    if (!integer(input.amount, &damage, error))
        return false;
    if (!self_damage(request) && source.team_damage_enabled &&
        same_team(&target, has_attacker ? &attacker : NULL) && !source.friendly_fire &&
        !source.nuke)
        damage = 0;
    if (source.reject_friendly_damage && !flags.no_protection && !source.nuke)
        damage = 0;
    if (source.easy_skill && !source.deathmatch && source.player &&
        (!source.rerelease || damage != 0)) {
        damage /= 2;
        if (damage == 0)
            damage = 1;
    }
    if (source.rerelease)
        damage = multiply_integer(damage, source.damage_scale);
    if (source.defender_sphere && source.player && (!source.rerelease || damage != 0)) {
        damage /= 2;
        if (damage == 0)
            damage = 1;
    }
    if (!source.rerelease && !request->radius && source.monster && source.attacker_player && !source.has_enemy &&
        target.health > 0)
        damage = multiply_integer(damage, 2);
    qa_damage_effect value = {.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_MOMENTUM, request, &value, error) ||
        !integer(value.amount, &damage, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    int32_t knockback = 0;
    if (!source.no_knockback && !target.no_knockback &&
        !integer(request->knockback, &knockback, error))
        return false;
    if (!flags.no_knockback && source.movable) {
        float coefficient = source.player && self_damage(request) ? 1600 : 500;
        if (!qa_combat_impulse(combat, request, request->direction,
                               coefficient * (float)knockback / fmaxf(50, target.mass), error))
            return false;
    }
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    bool protected_health = target.invulnerable && !flags.no_protection;
    bool protection_bypassed = false;
    if (protected_health) {
        value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target))
            return true;
        protected_health = value.allowed;
        protection_bypassed = !value.allowed;
    }
    float protection_saved = protected_health ? (float)damage : 0;
    float amount = (float)damage - protection_saved, power = 0, regular = 0;
    value = (qa_damage_effect){.amount = amount, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (value.allowed && !source.team_armor_protect &&
        !absorb(combat, policy, request, QA_PROTECTION_POWERED, amount, flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = amount - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    int32_t after_power;
    if (!integer(value.amount, &after_power, error))
        return false;
    value = (qa_damage_effect){.amount = (float)after_power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (value.allowed && !source.team_armor_protect && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, (float)after_power,
                                 flags, &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    value = (qa_damage_effect){.amount = (float)after_power - regular, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    int32_t take;
    if (!integer(value.amount, &take, error) ||
        !current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (!flags.no_protection && source.reject_team_damage)
        return true;
    if (flags.destroy_armor && (!target.invulnerable || protection_bypassed) &&
        !flags.no_protection)
        take = damage;
    *result = (qa_damage_result){.has_feedback = true,
                                 .feedback_family = QA_GAME_Q2,
                                 .power_saved = power,
                                 .armor_saved = regular + protection_saved,
                                 .blood = (float)take,
                                 .knockback = (float)knockback,
                                 .has_q2_damage = true, .q2_damage = (float)damage};
    if (!protected_health) {
        value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
        if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target)) {
            *result = (qa_damage_result){0};
            return true;
        }
        if (!value.allowed) {
            result->blood = 0;
            return true;
        }
        if (!current(combat, request, &target, &attacker, &has_attacker, error))
            return false;
    }
    if (!take) {
        value = (qa_damage_effect){.amount = 0, .allowed = true, .reaction = QA_REACTION_NONE};
        if (!effect(combat, policy, QA_DAMAGE_AFTER_HEALTH, request, &value, error)) return false;
        if (!qa_combat_live(combat, request->target)) *result = (qa_damage_result){0};
        return true;
    }
    float health = fmaxf(-999, truncf(target.health - (float)take));
    qa_reaction reaction = health <= 0 ? QA_REACTION_DEATH
                           : source.suppress_pain ? QA_REACTION_NONE : QA_REACTION_PAIN;
    bool lethal_changed;
    if (!lethal_health(combat, policy, request, &health, &reaction, &lethal_changed, error))
        return false;
    if (!qa_combat_live(combat, request->target)) return true;
    if (!qa_combat_set_health(combat, request->target, health, error))
        return false;
    result->applied_damage = (float)take;
    result->reaction = reaction;
    if ((policy->effect || combat->hooks.effect) && qa_combat_live(combat, request->target)) {
        value = (qa_damage_effect){
            .amount = result->applied_damage, .allowed = true, .reaction = result->reaction};
        if (!effect(combat, policy, QA_DAMAGE_AFTER_HEALTH, request, &value, error))
            return false;
        if (!qa_combat_live(combat, request->target)) {
            result->reaction = QA_REACTION_NONE;
            return true;
        }
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        result->reaction = !qa_combat_live(combat, request->target) ? QA_REACTION_NONE
                           : lethal_changed                         ? reaction
                           : target.health <= 0                     ? QA_REACTION_DEATH
                           : context.game.q2.suppress_pain          ? QA_REACTION_NONE
                                                                    : QA_REACTION_PAIN;
    }
    return true;
}

static bool q3_damage(qa_combat *combat, const qa_combat_policy *policy,
                      const qa_damage_request *request, qa_damage_result *result, qa_error *error) {
    qa_combat_state target, attacker;
    qa_combat_context context;
    bool has_attacker;
    if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
        return false;
    if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
        return true;
    qa_q3_combat_context source = context.game.q3;
    if (source.intermission || source.noclip ||
        (source.missionpack_invulnerability && !source.juiced))
        return true;
    qa_damage_flags flags = qa_attack_flags(&request->attack);
    int32_t damage;
    qa_damage_effect input;
    if (!weapon_damage(combat, policy, request, &input, error))
        return false;
    if (!input.allowed)
        return true;
    if (policy->effect || combat->hooks.effect) {
        if (!describe(combat, policy, request, &target, &attacker, &has_attacker, &context, error))
            return false;
        if (!qa_combat_live(combat, request->target) || !target.can_take_damage)
            return true;
        source = context.game.q3;
        if (source.intermission || source.noclip ||
            (source.missionpack_invulnerability && !source.juiced))
            return true;
    }
    if (!isfinite(source.knockback_scale))
        return qa_combat_argument(error, "invalid Q3 knockback scale");
    if (!integer(input.amount, &damage, error))
        return false;
    if (source.attacker_player && !self_damage(request)) {
        int32_t maximum =
            source.attacker_guard ? source.attacker_max_health / 2 : source.attacker_max_health;
        damage = multiply_integer(damage, maximum) / 100;
    }
    int32_t knockback = source.no_knockback || target.no_knockback || flags.no_knockback ? 0
                        : damage < 200                                                   ? damage
                                                                                         : 200;
    *result = (qa_damage_result){
        .has_feedback = true, .feedback_family = QA_GAME_Q3, .knockback = (float)knockback};
    if (source.player && !source.no_knockback && !target.no_knockback && !flags.no_knockback &&
        !qa_combat_impulse(combat, request, request->direction,
                           source.knockback_scale * (float)knockback / 200, error))
        return false;
    if (!qa_combat_live(combat, request->target))
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    if (!flags.no_protection) {
        bool check_team = !source.missionpack || (!source.juiced && !flags.no_team_protection);
        if ((check_team && !self_damage(request) &&
             same_team(&target, has_attacker ? &attacker : NULL) && !source.friendly_fire) ||
            source.proximity_protected)
            return true;
        if (target.invulnerable) {
            qa_damage_effect protection = {.amount = (float)damage, .allowed = true};
            if (!effect(combat, policy, QA_DAMAGE_PROTECTION_APPLIES, request, &protection, error))
                return false;
            if (!qa_combat_live(combat, request->target)) {
                *result = (qa_damage_result){0};
                return true;
            }
            if (protection.allowed)
                return true;
        }
    }
    if (source.battlesuit) {
        result->battlesuit = true;
        if (request->radius || source.falling)
            return true;
        damage /= 2;
    }
    if (self_damage(request))
        damage /= 2;
    if (damage < 1)
        damage = 1;
    float power = 0, regular = 0;
    qa_damage_effect value = {.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_POWER_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (value.allowed && !absorb(combat, policy, request, QA_PROTECTION_POWERED, (float)damage,
                                 flags, &power, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    value = (qa_damage_effect){.amount = (float)damage - power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_POWER, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    float after_power = value.amount;
    value = (qa_damage_effect){.amount = after_power, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_ARMOR_ALLOWED, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (value.allowed && !absorb(combat, policy, request, QA_PROTECTION_REGULAR, after_power, flags,
                                 &regular, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    value = (qa_damage_effect){.amount = after_power - regular, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_AFTER_ARMOR, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    int32_t take;
    if (!integer(value.amount, &take, error))
        return false;
    result->power_saved = power;
    result->armor_saved = regular;
    result->blood = (float)take;
    value = (qa_damage_effect){.amount = (float)damage, .allowed = true};
    if (!effect(combat, policy, QA_DAMAGE_BEFORE_HEALTH, request, &value, error))
        return false;
    if (!qa_combat_live(combat, request->target)) {
        *result = (qa_damage_result){0};
        return true;
    }
    if (!value.allowed) {
        result->blood = 0;
        return true;
    }
    if (!take)
        return true;
    if (!current(combat, request, &target, &attacker, &has_attacker, error))
        return false;
    int32_t previous_health;
    if (!integer(target.health, &previous_health, error))
        return false;
    int32_t source_health = signed_word((uint32_t)previous_health - (uint32_t)take);
    float health = source_health < -999 ? -999 : (float)source_health;
    qa_reaction reaction = health <= 0 ? QA_REACTION_DEATH : QA_REACTION_PAIN;
    if (!lethal_health(combat, policy, request, &health, &reaction, NULL, error))
        return false;
    if (!qa_combat_live(combat, request->target)) return true;
    if (!qa_combat_set_health(combat, request->target, health, error))
        return false;
    result->applied_damage = (float)take;
    result->reaction = reaction;
    return true;
}
bool qa_combat_policy_execute(qa_combat *combat, const qa_combat_policy *policy,
                              const qa_damage_request *request, qa_damage_result *result,
                              qa_error *error) {
    *result = (qa_damage_result){0};
    switch (policy->family) {
    case QA_GAME_Q1:
        return q1_damage(combat, policy, request, result, error);
    case QA_GAME_Q2:
        return q2_damage(combat, policy, request, result, error);
    case QA_GAME_Q3:
        return q3_damage(combat, policy, request, result, error);
    }
    return qa_combat_argument(error, "unknown combat family");
}
