#include "qa/gameplay.h"

bool qa_regular_armor_equal(qa_regular_armor a, qa_regular_armor b) {
    if (a.kind != b.kind) return false;
    if (a.kind == QA_ARMOR_NONE) return true;
    if (a.points != b.points) return false;
    switch (a.kind) {
    case QA_ARMOR_Q1: return a.item == b.item && a.protection.q1_absorption == b.protection.q1_absorption;
    case QA_ARMOR_Q2: return a.item == b.item && a.protection.q2.normal == b.protection.q2.normal && a.protection.q2.energy == b.protection.q2.energy;
    case QA_ARMOR_Q3: return a.protection.q3_protection == b.protection.q3_protection;
    case QA_ARMOR_SOURCE: return a.item == b.item;
    case QA_ARMOR_NONE: return true;
    }
    return false;
}

bool qa_powered_armor_equal(qa_powered_armor a, qa_powered_armor b) {
    return a.kind == b.kind && a.source_owner == b.source_owner &&
        a.source_edition == b.source_edition && (a.kind == QA_POWER_NONE || a.cells == b.cells);
}

bool qa_armor_equal(qa_armor a, qa_armor b) {
    return qa_regular_armor_equal(a.regular, b.regular) && qa_powered_armor_equal(a.powered, b.powered);
}

bool qa_armor_validate(const qa_armor *armor, qa_error *error) {
    if (!armor || armor->regular.kind < QA_ARMOR_NONE || armor->regular.kind > QA_ARMOR_SOURCE ||
        armor->powered.kind < QA_POWER_NONE || armor->powered.kind > QA_POWER_SHIELD) goto invalid;
    if (armor->regular.kind != QA_ARMOR_NONE && !isfinite(armor->regular.points)) goto invalid;
    if (armor->powered.kind != QA_POWER_NONE && !isfinite(armor->powered.cells)) goto invalid;
    if (armor->powered.source_edition < QA_Q2_POWER_ARMOR_NONE ||
        armor->powered.source_edition > QA_Q2_POWER_ARMOR_RERELEASE ||
        (armor->powered.kind == QA_POWER_NONE ?
            armor->powered.source_owner != 0 || armor->powered.source_edition != QA_Q2_POWER_ARMOR_NONE :
            !armor->powered.source_owner || armor->powered.source_edition == QA_Q2_POWER_ARMOR_NONE)) goto invalid;
    switch (armor->regular.kind) {
    case QA_ARMOR_Q1: if (!isfinite(armor->regular.protection.q1_absorption)) goto invalid; break;
    case QA_ARMOR_Q2: if (!isfinite(armor->regular.protection.q2.normal) || !isfinite(armor->regular.protection.q2.energy)) goto invalid; break;
    case QA_ARMOR_Q3: if (!isfinite(armor->regular.protection.q3_protection)) goto invalid; break;
    case QA_ARMOR_NONE: case QA_ARMOR_SOURCE: break;
    }
    return true;
invalid:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid armor state"); return false;
}

qa_damage_flags qa_attack_flags(const qa_attack *attack) {
    qa_damage_flags flags = {.regular_scale = 1};
    if (!attack) return flags;
    uint32_t q2 = attack->cause.kind == QA_CAUSE_Q2 ? attack->cause.source.q2.flags : 0;
    uint32_t q3 = attack->cause.kind == QA_CAUSE_Q3 ? attack->cause.source.q3.flags : 0;
    flags.no_armor = ((q2 | q3) & 2u) != 0 || (attack->cause.kind == QA_CAUSE_Q1 && attack->cause.source.q1.armor == QA_Q1_ARMOR_BYPASS);
    flags.no_power_armor = (q2 & 0x100u) != 0;
    flags.no_regular_armor = (q2 & 0x80u) != 0;
    flags.energy = (q2 & 4u) != 0;
    if (attack->cause.kind == QA_CAUSE_Q1 && attack->cause.source.q1.armor == QA_Q1_ARMOR_HALF) flags.regular_scale = 0.5f;
    flags.no_knockback = (q2 & 8u) != 0 || (q3 & 4u) != 0;
    flags.no_protection = (q2 & 0x20u) != 0 || (q3 & 8u) != 0;
    flags.no_team_protection = (q3 & 0x10u) != 0;
    flags.destroy_armor = (q2 & 0x40u) != 0;
    return flags;
}

bool qa_armor_absorb(const qa_armor *armor, float damage, qa_damage_flags flags,
                     const qa_armor_context *context, const qa_protection_channel *stage,
                     qa_armor_result *out, qa_error *error) {
    if (!out || !context || !isfinite(damage) || !isfinite(context->screen_facing_dot) ||
        !isfinite(flags.regular_scale) || (stage && *stage != QA_PROTECTION_REGULAR && *stage != QA_PROTECTION_POWERED)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid armor stage"); return false;
    }
    if (!qa_armor_validate(armor, error)) return false;
    bool regular = !stage || *stage == QA_PROTECTION_REGULAR;
    bool power = !stage || *stage == QA_PROTECTION_POWERED;
    if (!context->q2_profile && ((regular && armor->regular.kind == QA_ARMOR_Q2) || (power && armor->powered.kind != QA_POWER_NONE))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 armor requires its source profile"); return false;
    }
    qa_armor_result result = {.armor = *armor};
    if (damage == 0 || flags.no_armor) { *out = result; return true; }
    qa_powered_armor *powered = &result.armor.powered;
    if (power && !flags.no_power_armor && (!context->rerelease || context->alive) &&
        powered->kind != QA_POWER_NONE && powered->cells > 0 &&
        (powered->kind != QA_POWER_SCREEN || context->screen_facing_dot > 0.3f)) {
        float damage_per_cell = powered->kind == QA_POWER_SCREEN || context->ctf ? 1.0f : 2.0f;
        float protected_damage = truncf(powered->kind == QA_POWER_SCREEN ? damage / 3 : 2 * damage / 3);
        bool doubled = context->rerelease ? flags.energy : flags.no_regular_armor;
        float available = powered->cells * damage_per_cell;
        if (doubled) available = truncf(available / 2);
        if (context->rerelease) { protected_damage = fmaxf(1, protected_damage); available = fmaxf(1, available); }
        result.power_saved = fminf(available, protected_damage);
        float used = truncf(result.power_saved / damage_per_cell) * (doubled ? 2 : 1);
        powered->cells = context->rerelease ? fmaxf(0, powered->cells - fmaxf(damage_per_cell, used)) : powered->cells - used;
    }
    qa_regular_armor *item = &result.armor.regular;
    if (regular && !flags.no_regular_armor && item->kind != QA_ARMOR_NONE) {
        float protection;
        switch (item->kind) {
        case QA_ARMOR_Q1: protection = item->protection.q1_absorption; break;
        case QA_ARMOR_Q2: protection = flags.energy ? item->protection.q2.energy : item->protection.q2.normal; break;
        case QA_ARMOR_Q3: protection = item->protection.q3_protection; break;
        case QA_ARMOR_SOURCE:
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source armor requires an absorption owner"); return false;
        default: protection = 0; break;
        }
        result.regular_saved = fminf(item->points, ceilf(protection * flags.regular_scale * (damage - result.power_saved)));
        item->points -= result.regular_saved;
        if (item->kind == QA_ARMOR_Q1 && item->points <= 0) item->protection.q1_absorption = 0;
    }
    if (!isfinite(result.power_saved) || !isfinite(result.regular_saved) || !qa_armor_validate(&result.armor, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "armor arithmetic overflow"); return false;
    }
    *out = result;
    return true;
}
