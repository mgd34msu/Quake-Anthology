/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/gameplay.h"

bool qa_damage_request_validate(const qa_damage_request *request, qa_error *error) {
    if (!request || !request->target.registry || !isfinite(request->amount) ||
        !isfinite(request->knockback) || !qa_vec_finite(request->direction) ||
        !qa_vec_finite(request->point) || !qa_vec_finite(request->normal)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid damage request"); return false;
    }
    switch (request->attack.cause.kind) {
    case QA_CAUSE_Q1:
        if (request->attack.cause.source.q1.armor > QA_Q1_ARMOR_HALF ||
            request->attack.cause.source.q1.armor < QA_Q1_ARMOR_NORMAL) goto invalid;
        break;
    case QA_CAUSE_Q2:
        if (request->attack.cause.source.q2.native < QA_Q2_CAUSE_NONE ||
            request->attack.cause.source.q2.native > QA_Q2_CAUSE_RERELEASE) goto invalid;
        break;
    case QA_CAUSE_Q3: break;
    case QA_CAUSE_ENVIRONMENT:
        if (request->attack.cause.source.hazard < QA_HAZARD_FALL ||
            request->attack.cause.source.hazard > QA_HAZARD_TRIGGER) goto invalid;
        break;
    default: goto invalid;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid source damage cause"); return false;
}

bool qa_attack_next(uint64_t *sequence, qa_attack *attack, qa_error *error) {
    if (!sequence || !attack || *sequence == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "attack sequence exhausted or unavailable"); return false;
    }
    attack->sequence = ++*sequence;
    return true;
}

bool qa_damage_apply_modifier(const qa_actor_registry *actors, const qa_damage_request *request,
                              const qa_damage_modifier *modifier, qa_damage_request *out, qa_error *error) {
    if (!actors || !out || !qa_damage_request_validate(request, error)) return false;
    qa_damage_request next = *request;
    if (modifier) {
        if (!modifier->transform) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "damage modifier has no callback"); return false; }
        if (!qa_actors_get(actors, next.attack.attacker)) next.attack.attacker = (qa_actor_id){0};
        if (!qa_actors_get(actors, next.attack.inflictor)) next.attack.inflictor = (qa_actor_id){0};
        if ((!next.attack.powerup_applied || next.attack.powerup_owner != modifier->owner) &&
            !modifier->transform(modifier->context, next.attack.attacker, request->amount, &next.amount, error)) return false;
        if (!isfinite(next.amount)) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "damage modifier returned nonfinite damage"); return false; }
        next.attack.powerup_applied = true;
        next.attack.powerup_owner = modifier->owner;
    }
    *out = next;
    return true;
}
