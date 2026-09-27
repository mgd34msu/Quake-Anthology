#include "internal.h"

bool q2_grapple_weapon(q2_weapon_call *c, qa_error *e) {
    bool lm = c->definition->weapon == QA_Q2_LMCTF_HOOK;
    qa_q2_grapple_state *source = &c->actor->grapples[lm ? QA_Q2_LMCTF_GRAPPLE : QA_Q2_CTF_GRAPPLE];
    qa_q2_weapon_state *s = c->state;
    bool change = s->pending != QA_Q2_WEAPON_NONE || s->handoff == QA_Q2_PRIMARY_HOLSTERING;
    if (lm) {
        if (s->phase == QA_Q2_ACTIVATING)
            ++s->frame;
        if (change && s->phase != QA_Q2_DROPPING) {
            s->phase = QA_Q2_DROPPING;
            s->frame = 36;
            return true;
        }
        if (!c->input.attack && !s->latched_attack && !source->hook_held &&
            !qa_q2_grapple_reset(c->game, c->actor->id, QA_Q2_LMCTF_GRAPPLE, e))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        return q2_generic_classic(c, e);
    }
    bool held = c->input.attack || (c->rerelease && c->input.holster);
    if (held && s->phase == QA_Q2_FIRING && source->hook.registry != 0)
        s->frame = c->rerelease ? 6 : 9;
    if (!held && source->hook.registry != 0) {
        if (!qa_q2_grapple_reset(c->game, c->actor->id, QA_Q2_CTF_GRAPPLE, e))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        if (s->phase == QA_Q2_FIRING)
            s->phase = QA_Q2_READY;
    }
    if ((change || (c->rerelease && (c->input.holster || c->input.latched_holster))) &&
        source->phase != QA_Q2_GRAPPLE_FLY && s->phase == QA_Q2_FIRING) {
        if (c->equipment)
            s->handoff = QA_Q2_PRIMARY_HOLSTERING;
        else if (c->rerelease && s->handoff == QA_Q2_PRIMARY_ACTIVE &&
                 s->pending == QA_Q2_WEAPON_NONE)
            s->pending = s->weapon;
        s->phase = QA_Q2_DROPPING;
        s->frame = 32;
    }
    qa_q2_weapon_phase before = s->phase;
    if (!q2_generic(c, e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (c->rerelease && held && s->phase == QA_Q2_FIRING && source->hook.registry != 0)
        s->frame = 6;
    if (before == QA_Q2_ACTIVATING && s->phase == QA_Q2_READY &&
        source->phase != QA_Q2_GRAPPLE_FLY) {
        s->frame = held ? 5 : c->rerelease ? 6 : 9;
        s->phase = QA_Q2_FIRING;
    }
    return true;
}
static q2_actor *equipment_actor(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind,
                                 bool create, qa_error *e) {
    if (g == NULL || (unsigned)kind > QA_Q2_LMCTF_GRAPPLE || g->services.physics == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple equipment binding");
        return NULL;
    }
    q2_actor *a = q2_actor_get(g, id, create, e);
    if (a == NULL)
        return NULL;
    qa_q2_grapple_state *s = &a->grapples[kind];
    if (!s->equipment_bound) {
        if (!create) {
            qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 grapple equipment is not bound");
            return NULL;
        }
        s->equipment_bound = true;
        s->equipment = (qa_q2_weapon_state){.weapon = kind == QA_Q2_LMCTF_GRAPPLE ? QA_Q2_LMCTF_HOOK
                                                                                  : QA_Q2_GRAPPLE,
                                            .phase = QA_Q2_ACTIVATING,
                                            .handoff = QA_Q2_PRIMARY_HOLSTERED,
                                            .gun_rate = 10,
                                            .kick_seconds = 0.2f};
    }
    return a;
}
bool qa_q2_grapple_equipment_resume(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind,
                                    qa_error *e) {
    q2_actor *a = equipment_actor(g, id, kind, true, e);
    if (a == NULL)
        return false;
    qa_q2_weapon_state *s = &a->grapples[kind].equipment;
    s->handoff = QA_Q2_PRIMARY_ACTIVE;
    s->phase = QA_Q2_ACTIVATING;
    s->frame = 0;
    s->pending = QA_Q2_WEAPON_NONE;
    return true;
}
bool qa_q2_grapple_equipment_holster(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind,
                                     qa_error *e) {
    q2_actor *a = equipment_actor(g, id, kind, false, e);
    if (a == NULL)
        return false;
    qa_q2_weapon_state *s = &a->grapples[kind].equipment;
    if (s->handoff == QA_Q2_PRIMARY_ACTIVE)
        s->handoff = QA_Q2_PRIMARY_HOLSTERING;
    return true;
}
bool qa_q2_grapple_equipment_tick(qa_q2_game *g, qa_actor_id id, qa_q2_grapple_kind kind,
                                  const qa_q2_weapon_input *input, uint64_t now, uint64_t frame,
                                  qa_error *e) {
    if (input == NULL || frame == 0 || !qa_vec_finite(input->angles) ||
        !isfinite(input->view_height) || !isfinite(input->gravity) ||
        (unsigned)input->hand > QA_Q2_CENTER_HAND) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 grapple equipment frame");
        return false;
    }
    q2_actor *a = equipment_actor(g, id, kind, false, e);
    if (a == NULL)
        return false;
    qa_q2_weapon_state *s = &a->grapples[kind].equipment;
    if (s->handoff == QA_Q2_PRIMARY_HOLSTERED)
        return true;
    g->now_ns = now;
    g->frame_ns = frame;
    a->input = *input;
    s->latched_attack |= input->latched_attack;
    q2_weapon_call c = {.game = g,
                        .actor = a,
                        .state = s,
                        .input = *input,
                        .definition = &g->definitions[s->weapon],
                        .now_ns = now,
                        .frame_ns = frame,
                        .rerelease = g->options.edition == QA_Q2_RERELEASE,
                        .silenced = a->silencer > 0,
                        .equipment = true};
    c.input.source_rules =
        kind == QA_Q2_CTF_GRAPPLE ? QA_Q2_WEAPON_RULES_CTF : QA_Q2_WEAPON_RULES_LMCTF;
    if (!q2_weapon_powerups(&c, e))
        return false;
    return q2_grapple_weapon(&c, e) && (!q2_actor_live(g, id) || q2_present(&c, e));
}
bool qa_q2_grapple_hold(qa_q2_game *g, qa_actor_id id, bool pressed, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    qa_q2_grapple_state *s = &a->grapples[QA_Q2_LMCTF_GRAPPLE];
    s->hook_held = pressed;
    if (pressed) {
        if (a->weapon_bound && a->weapon.weapon == QA_Q2_LMCTF_HOOK)
            a->weapon.latched_attack = true;
        if (s->equipment_bound && s->equipment.handoff == QA_Q2_PRIMARY_ACTIVE)
            s->equipment.latched_attack = true;
        return true;
    }
    return qa_q2_grapple_reset(g, id, QA_Q2_LMCTF_GRAPPLE, e);
}
