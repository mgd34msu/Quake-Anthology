#include "internal.h"

static bool tag_bonus(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    qa_combat_state state;
    qa_builtin_actor_traits traits = {.max_health = 100};
    if (!qa_combat_read(m->options.services.combat, actor, &state, e))
        return false;
    if (m->options.services.actor_traits)
        m->options.services.actor_traits(m->options.services.context, actor, &traits);
    float maximum = traits.max_health > 0 ? traits.max_health : 100;
    if (state.health < maximum && !qa_combat_set_health(m->options.services.combat, actor,
                                                        fminf(maximum, state.health + 200), e))
        return false;
    if (!m->options.hooks.give_body_armor)
        return mode_fail(e, "Tag requires the admitted Q2 body-armor pickup provider");
    if (!m->options.hooks.give_body_armor(m->options.hooks.context, v->id, actor, e))
        return false;
    return mode_event(m, v, QA_MODE_TAG_CHANGED, actor, (qa_actor_id){0}, v->tag, 0, 1, 0, e);
}
bool mode_tag_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                    bool *accepted, qa_error *e) {
    if (v->value.rules.source == QA_MODE_ROGUE)
        return mode_rogue_tag_touch(m, v, o, actor, accepted, e);
    if (o->owner_until_ns && v->value.time_ns < o->owner_until_ns)
        return true;
    if (!mode_object_count(m, v, o, actor, 1, e))
        return false;
    v->tag_owner = actor;
    v->tag_count = 0;
    o->value.phase = QA_OBJECTIVE_CARRIED;
    o->value.carrier = actor;
    o->expire_ns = 0;
    o->physics.motion = QA_PHYSICS_STATIONARY;
    if (!mode_object_hide(m, o, true, e))
        return false;
    *accepted = true;
    return tag_bonus(m, v, actor, e);
}
bool mode_tag_death(qa_modes *m, mode_instance *v, const qa_damage_outcome *outcome,
                    bool primary_score, const qa_mode_frag *native, qa_error *e) {
    qa_actor_id attacker = outcome->request.attack.attacker, victim = outcome->request.target;
    mode_member *killer = mode_member_get(m, v, attacker);
    bool self = qa_actor_id_equal(attacker, victim),
         friendly = killer && qa_modes_same_team(m, v->id, attacker, victim);
    if (v->value.rules.source == QA_MODE_ROGUE) {
        bool evaluated = native && native->evaluated_mode.slot == v->id.slot &&
                         native->evaluated_mode.generation == v->id.generation;
        int32_t points = !killer || self ? -1 : 1;
        if (!evaluated && killer && !self &&
            !qa_modes_rogue_tag_score(m, v->id, victim, attacker, &points, e))
            return false;
        if (killer && !self)
            mode_stat_add(v, &killer->stats.kills, 1);
        if (evaluated)
            return !primary_score || !native->recipient.registry ||
                   qa_modes_add_score(m, v->id, native->recipient, native->delta, e);
        int32_t base = !killer || self ? -1 : 1, bonus = points - base;
        return qa_modes_add_score(
            m, v->id, killer ? attacker : victim,
            primary_score ? mode_add_i32(native ? native->delta : base, bonus) : bonus, e);
    }
    int32_t change = !killer || self || friendly ? -1 : 1;
    int32_t ordinary = change;
    if (killer && change > 0 && qa_actor_id_equal(attacker, v->tag_owner)) {
        change = 3;
        if (++v->tag_count == 5) {
            v->tag_count = 0;
            qa_item_id quad;
            double count;
            if (!qa_builtin_resource(&m->options.services, "q2:item_quad", &quad, e) ||
                !mode_count(m, attacker, quad, &count, e) ||
                !mode_set_count(m, attacker, quad, count + 1, e))
                return false;
            if (!m->options.hooks.use_item)
                return mode_fail(e, "Tag quad requires the selected source item action");
            if (!m->options.hooks.use_item(m->options.hooks.context, attacker, quad, e))
                return false;
        }
    } else if (killer && !self && qa_actor_id_equal(victim, v->tag_owner)) {
        change = 5;
        const qa_damage_cause *cause = &outcome->request.attack.cause;
        int mod = cause->kind == QA_CAUSE_Q2 ? cause->source.q2.means_of_death & ~0x8000000 : -1;
        if (mod == 49 || mod == 53 || mod == 54 || mod == 55 || !mode_alive(m, attacker)) {
            if (!qa_modes_drop(m, v->id, victim, true, e))
                return false;
        } else {
            mode_object *o = mode_object_get(m, v->tag);
            if (o && (!mode_object_count(m, v, o, victim, 0, e) ||
                      !mode_object_count(m, v, o, attacker, 1, e)))
                return false;
            if (o)
                o->value.carrier = attacker;
            v->tag_owner = attacker;
            v->tag_count = 0;
            if (!tag_bonus(m, v, attacker, e))
                return false;
        }
    }
    if (killer && !self && !friendly)
        mode_stat_add(v, &killer->stats.kills, 1);
    qa_actor_id recipient = killer ? attacker : victim;
    int32_t bonus = change - ordinary;
    if (primary_score && native && !qa_actor_id_equal(native->recipient, recipient)) {
        if (native->recipient.registry &&
            !qa_modes_add_score(m, v->id, native->recipient, native->delta, e))
            return false;
        return qa_modes_add_score(m, v->id, recipient, bonus, e);
    }
    return qa_modes_add_score(
        m, v->id, recipient,
        primary_score ? mode_add_i32(native ? native->delta : ordinary, bonus) : bonus, e);
}
