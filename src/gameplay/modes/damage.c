#include "internal.h"

bool mode_damage(qa_modes *m, qa_game_family family, qa_damage_request *request, qa_error *e) {
    if (!m->options.hooks.combat_provider)
        return mode_fail(e, "mode damage needs selected target combat policy");
    request->attack.combat_provider =
        m->options.hooks.combat_provider(m->options.hooks.context, request->target, family);
    if (!request->attack.combat_provider)
        return mode_fail(e, "mode target has no combat policy");
    if (!qa_attack_next(&m->attack_sequence, &request->attack, e))
        return false;
    qa_damage_outcome result = {0};
    bool ok = MODE_CALLBACK(m, qa_combat_apply(m->options.services.combat, request, &result, e));
    qa_damage_outcome_free(&result);
    return ok;
}
qa_team_id qa_modes_combat_team(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                qa_team_id fallback) {
    mode_instance *v = mode_get(m, id);
    if (!v || !v->value.rules.enabled || !mode_member_get(m, v, actor))
        return fallback;
    if (v->value.rules.source == QA_MODE_LMCTF && (v->value.rules.flags & 128u))
        return 0;
    qa_team_id team;
    return qa_modes_team(m, v->id, actor, &team, NULL) ? team : fallback;
}
bool qa_modes_damage_effect(qa_modes *m, qa_mode_id id, qa_damage_effect_stage stage,
                            const qa_damage_request *request, qa_damage_effect *effect,
                            qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !request || !effect)
        return mode_fail(e, "invalid mode damage effect");
    if (!v->value.rules.enabled)
        return true;
    qa_mode_source source = v->value.rules.source;
    mode_member *attacker = mode_member_get(m, v, request->attack.attacker),
                *target = mode_member_get(m, v, request->target);
    bool friendly = attacker && target && !qa_actor_id_equal(attacker->actor, target->actor) &&
                    attacker->last_team && attacker->last_team == target->last_team;
    bool falling = request->attack.cause.kind == QA_CAUSE_ENVIRONMENT &&
                   request->attack.cause.source.hazard == QA_HAZARD_FALL;
    if (request->attack.cause.kind == QA_CAUSE_Q1) {
        const char *cause = qa_strings_cstr(qa_session_strings(m->options.services.session),
                                            request->attack.cause.source.q1.death_type);
        falling = cause && !strcmp(cause, "falling");
    }
    if (stage == QA_DAMAGE_BEFORE_QUAD && source == QA_MODE_THREEWAVE && target && falling &&
        m->options.hooks.grapple_pulling &&
        m->options.hooks.grapple_pulling(m->options.hooks.context, request->target))
        effect->allowed = false;
    if (stage == QA_DAMAGE_AFTER_QUAD) {
        if (!qa_modes_attack_damage(m, id, request->attack.attacker, effect->amount,
                                    &effect->amount, e))
            return false;
        if (source <= QA_MODE_Q1_HORDE && source != QA_MODE_ROGUE &&
            !qa_modes_resist_damage(m, id, request->target, effect->amount, &effect->amount, e))
            return false;
        if (!qa_modes_player_hurt(m, id, request, e))
            return false;
    }
    if (stage == QA_DAMAGE_AFTER_POWER && (source == QA_MODE_Q2_CTF || source == QA_MODE_LMCTF) &&
        !qa_modes_resist_damage(m, id, request->target, effect->amount, &effect->amount, e))
        return false;
    if (stage == QA_DAMAGE_AFTER_ARMOR && source == QA_MODE_ROGUE &&
        !qa_modes_resist_damage(m, id, request->target, effect->amount, &effect->amount, e))
        return false;
    if (source == QA_MODE_THREEWAVE && !v->value.rules.start_map) {
        int32_t flags = v->value.rules.teamplay;
        if ((stage == QA_DAMAGE_POWER_ALLOWED || stage == QA_DAMAGE_ARMOR_ALLOWED) && flags >= 0 &&
            friendly && (flags & 2))
            effect->allowed = false;
        if (stage == QA_DAMAGE_PROTECTION_APPLIES && target) {
            qa_team_id team;
            if (!qa_modes_team(m, v->id, target->actor, &team, e))
                return false;
            if (team != target->last_team)
                effect->allowed = false;
        }
        if (stage == QA_DAMAGE_BEFORE_HEALTH && flags >= 0 && friendly) {
            if (flags & 4) {
                qa_string_id type;
                if (!qa_builtin_resource(&m->options.services, "ctf:reflection", &type, e))
                    return false;
                qa_damage_request reflected = {
                    .target = attacker->actor,
                    .amount = effect->amount,
                    .attack = {.attacker = attacker->actor,
                               .inflictor = request->attack.inflictor,
                               .weapon_provider = m->options.owner,
                               .time_ns = v->value.time_ns,
                               .cause = {.kind = QA_CAUSE_Q1, .source.q1 = {.death_type = type}}}};
                if (!mode_damage(m, QA_GAME_Q1, &reflected, e))
                    return false;
            }
            if (flags & 1)
                effect->allowed = false;
        }
    } else if (source == QA_MODE_ROGUE &&
               (friendly || (v->value.rules.teamplay == 1 && attacker && target &&
                             attacker->last_team && attacker->last_team == target->last_team))) {
        bool ctf = v->value.rules.teamplay >= 4 && v->value.rules.teamplay <= 6;
        if ((stage == QA_DAMAGE_POWER_ALLOWED || stage == QA_DAMAGE_ARMOR_ALLOWED) && ctf &&
            !(v->value.rules.flags & 2u))
            effect->allowed = false;
        if (stage == QA_DAMAGE_BEFORE_HEALTH &&
            (v->value.rules.teamplay == 1 || (ctf && !(v->value.rules.flags & 4u))))
            effect->allowed = false;
    }
    return true;
}
static bool rogue_haste_weapon(const char *name) {
    return name && (!strcmp(name, "q1:weapon/axe") || !strcmp(name, "q1:weapon/shotgun") ||
        !strcmp(name, "q1:weapon/supershotgun") || !strcmp(name, "q1:weapon/grenadelauncher") ||
        !strcmp(name, "q1:weapon/rocketlauncher") || !strcmp(name, "q1:weapon/rogue:multi-grenade") ||
        !strcmp(name, "q1:weapon/rogue:multi-rocket") || !strcmp(name, "q1:weapon/rogue:plasma"));
}
bool qa_modes_haste_weapon(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_item_id weapon,
                           float base, float *interval, float *nail_speed, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !interval || !nail_speed || !isfinite(base))
        return mode_fail(e, "invalid haste weapon query");
    *interval = base;
    if ((v->value.rules.source != QA_MODE_THREEWAVE && v->value.rules.source != QA_MODE_ROGUE) ||
        !qa_modes_has_relic(m, id, actor, QA_RELIC_HASTE))
        return true;
    const char *name = qa_strings_cstr(qa_session_strings(m->options.services.session), weapon);
    if (!name)
        return true;
    if (v->value.rules.source == QA_MODE_ROGUE) {
        if (!rogue_haste_weapon(name)) return true;
        *interval = (base * 2.0f) / 3.0f;
        return true;
    }
    if (!strcmp(name, "q1:weapon/axe") || !strcmp(name, "q1:weapon/shotgun") ||
        !strcmp(name, "q1:weapon/grenadelauncher"))
        *interval = .3f;
    else if (!strcmp(name, "q1:weapon/supershotgun") || !strcmp(name, "q1:weapon/rocketlauncher"))
        *interval = .4f;
    *nail_speed = 2000;
    return true;
}

static bool weapon_attack_delay(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                 qa_item_id weapon, float *delay, qa_error *e) {
    float base = *delay, nail_speed = 0;
    if (!qa_modes_haste_weapon(m, id, actor, weapon, base, delay, &nail_speed, e)) return false;
    mode_instance *v = mode_get(m, id);
    const char *name = qa_strings_cstr(qa_session_strings(m->options.services.session), weapon);
    if (v->value.rules.source == QA_MODE_ROGUE && rogue_haste_weapon(name) &&
        qa_modes_has_relic(m, id, actor, QA_RELIC_HASTE)) {
        bool handled;
        return qa_modes_tech_sound(m, id, actor, QA_RELIC_HASTE, false, false, &handled, e);
    }
    return true;
}
bool qa_modes_weapon_attack_delay(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                                   qa_item_id weapon, float *delay, qa_error *e) {
    if (!m || !delay || !isfinite(*delay) || *delay <= 0)
        return mode_fail(e, "invalid source weapon attack delay");
    return MODE_CALLBACK(m, weapon_attack_delay(m, id, actor, weapon, delay, e));
}
