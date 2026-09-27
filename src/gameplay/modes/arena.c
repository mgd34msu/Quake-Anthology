#include "internal.h"

bool mode_obelisk_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                        bool *accepted, qa_error *e) {
    if (v->value.rules.kind != QA_MODE_HARVESTER || !o->spec.team)
        return true;
    mode_member *p = mode_member_get(m, v, actor);
    qa_team_id team;
    if (!p || p->stats.tokens <= 0 || !qa_modes_team(m, v->id, actor, &team, e))
        return p == NULL || p->stats.tokens <= 0;
    if (!team || team == o->spec.team)
        return true;
    int count = p->stats.tokens;
    p->stats.tokens = 0;
    mode_stat_add(v, &p->stats.captures, count);
    int index = mode_team_index(v, team);
    if (index >= 0)
        v->value.team_captures[index] = mode_add_i32(v->value.team_captures[index], count);
    int64_t score = (int64_t)count * 100;
    if (!qa_modes_team_score(m, v->id, team, count, e) ||
        !qa_modes_add_score(m, v->id, actor, score > INT32_MAX ? INT32_MAX : (int32_t)score, e))
        return false;
    *accepted = true;
    return mode_event(m, v, QA_MODE_HARVEST, actor, (qa_actor_id){0}, o->actor, team, count, 0x800,
                      e) &&
           qa_modes_rank(m, v->id, e);
}
bool qa_modes_object_damage(qa_modes *m, qa_mode_id id, qa_damage_request *request,
                            bool *allowed, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !request || !allowed)
        return mode_fail(e, "invalid mode damage admission");
    *allowed = true;
    if (!v->value.rules.enabled)
        return true;
    mode_object *o = mode_object_get(m, request->target);
    if (o && (o->mode.slot != id.slot || o->mode.generation != id.generation))
        return true;
    mode_member *target = mode_member_get(m, v, request->target);
    mode_member *attacker = mode_member_get(m, v, request->attack.attacker);
    /* Objects belong to their exact instance. Ordinary damage rules belong to
     * participating targets; unrelated actors in the shared world stay neutral. */
    if (!o && (!target || target->player.spectator))
        return true;
    if (o && o->spec.kind == QA_MODE_OBJECT_OBELISK) {
        qa_team_id team;
        if (v->value.rules.kind != QA_MODE_OVERLOAD ||
            o->value.phase == QA_OBJECTIVE_DESTROYED) {
            *allowed = false;
            return true;
        }
        if (attacker) {
            if (!qa_modes_team(m, v->id, request->attack.attacker, &team, e))
                return false;
            if (team == o->spec.team) {
                *allowed = false;
                return true;
            }
            int index = mode_team_index(v, o->spec.team);
            if (index >= 0 && v->value.time_ns > v->attack_sound_ns[index] + 20 * MODE_SECOND) {
                v->attack_sound_ns[index] = v->value.time_ns;
                if (!mode_event(m, v, QA_MODE_OBELISK_ATTACKED, request->attack.attacker,
                                (qa_actor_id){0}, o->actor, o->spec.team, 0, 0, e))
                    return false;
            }
        }
    }
    if (v->value.rules.kind == QA_MODE_TAG && v->value.rules.source != QA_MODE_ROGUE &&
        !qa_actor_id_equal(request->target, v->tag_owner) &&
        !qa_actor_id_equal(request->attack.attacker, v->tag_owner))
        request->amount = truncf(request->amount * .75f);
    if (v->value.rules.kind != QA_MODE_DEATHBALL)
        return true;
    if (!qa_actor_id_equal(request->target, v->ball)) {
        if (!qa_actor_id_equal(request->attack.attacker, v->ball))
            request->amount = truncf(request->amount * .5f);
        return true;
    }
    request->amount = 1;
    if (request->attack.cause.kind != QA_CAUSE_Q2)
        return true;
    int mod = request->attack.cause.source.q2.means_of_death;
    float kick = request->knockback;
    if (kick < 1) {
        if (mod == 8)
            kick = 70;
        else if (mod == 14)
            kick = 90;
    } else
        switch (mod) {
        case 1:
            kick *= 3;
            break;
        case 2:
            kick = truncf(kick * 3 / 8);
            break;
        case 3:
        case 11:
        case 44:
            kick = truncf(kick / 3);
            break;
        case 4:
        case 9:
            kick = truncf(kick * 1.5f);
            break;
        case 10:
            kick *= 4;
            break;
        case 6:
        case 15:
        case 46:
        case 7:
        case 16:
        case 24:
        case 51:
        case 41:
            kick = truncf(kick * .5f);
            break;
        default:
            break;
        }
    request->knockback = kick;
    return true;
}
bool qa_modes_object_reaction(qa_modes *m, const qa_damage_outcome *outcome, qa_error *e) {
    if (!m || !outcome)
        return mode_fail(e, "invalid objective reaction");
    mode_object *o = mode_object_get(m, outcome->request.target);
    if (!o)
        return true;
    mode_instance *v = mode_get(m, o->mode);
    if (!v)
        return true;
    qa_actor_id attacker = outcome->request.attack.attacker;
    if (o->spec.kind == QA_MODE_OBJECT_BALL) {
        v->last_ball_touch = mode_member_get(m, v, attacker) ? attacker : (qa_actor_id){0};
        if (outcome->result.reaction == QA_REACTION_DEATH)
            return mode_ball_reset(m, v, o, e);
        return qa_combat_set_health(m->options.services.combat, o->actor, 50000, e);
    }
    if (o->spec.kind != QA_MODE_OBJECT_OBELISK || v->value.rules.kind != QA_MODE_OVERLOAD)
        return true;
    if (outcome->result.reaction == QA_REACTION_DEATH) {
        int index = mode_team_index(v, o->spec.team);
        qa_team_id winner = index == 0 ? v->value.rules.teams[1] : v->value.rules.teams[0];
        if (!qa_modes_team_score(m, v->id, winner, 1, e))
            return false;
        if (mode_member_get(m, v, attacker) && !qa_modes_add_score(m, v->id, attacker, 100, e))
            return false;
        qa_combat_state state;
        if (!qa_combat_read_traits(m->options.services.combat, o->actor, &state, e))
            return false;
        state.can_take_damage = false;
        if (!qa_combat_set_traits(m->options.services.combat, o->actor, &state, e))
            return false;
        o->value.phase = QA_OBJECTIVE_DESTROYED;
        o->value.frame = 2;
        o->next_ns = v->value.time_ns + v->value.rules.obelisk_respawn_ns;
        memset(v->attack_sound_ns, 0, sizeof(v->attack_sound_ns));
        return mode_event(m, v, QA_MODE_OBELISK_EXPLODE, attacker, (qa_actor_id){0}, o->actor,
                          winner, 100, 0x800, e) &&
               qa_modes_rank(m, v->id, e);
    }
    if (mode_member_get(m, v, attacker)) {
        float damage = outcome->result.applied_damage / 10;
        int32_t amount = damage >= (float)INT32_MAX ? INT32_MAX : damage < 1 ? 1 : (int32_t)damage;
        if (!qa_modes_add_score(m, v->id, attacker, amount, e))
            return false;
    }
    bool first = o->value.frame == 0;
    o->value.frame = 1;
    return !first || mode_event(m, v, QA_MODE_OBELISK_PAIN, attacker, (qa_actor_id){0}, o->actor,
                                o->spec.team, 0, 0, e);
}
