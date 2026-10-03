#include "internal.h"
#include "qa/game_q2_source.h"

bool q2_interval(q2_weapon_call *c, uint64_t native, uint64_t *out, qa_error *e) {
    bool handled = false;
    if (!c->equipment && c->game->hooks.selected_firing_interval &&
        !c->game->hooks.selected_firing_interval(c->game->hooks.context, c->actor->id,
            native, out, &handled, e)) return false;
    if (!handled) {
        uint64_t interval = c->game->hooks.firing_interval == NULL ? native :
            c->game->hooks.firing_interval(c->game->hooks.context, c->actor->id, native);
        *out = interval == 0 ? Q2_MS : interval;
    }
    return true;
}
static unsigned animation_rate(const q2_weapon_call *c) {
    const qa_q2_weapon_state *s = c->state;
    unsigned rate = c->input.quick_switch && c->frame_ns <= 50 * Q2_MS &&
                            (s->phase == QA_Q2_ACTIVATING || s->phase == QA_Q2_DROPPING)
                        ? 20
                        : 10;
    if (s->frame != 0) {
        if (c->input.quad_fire_until_ns > c->now_ns)
            rate *= 2;
        if (c->input.haste)
            rate *= 2;
    }
    return rate;
}
uint64_t q2_animation_native(const q2_weapon_call *c) {
    return (1000 / animation_rate(c)) * Q2_MS;
}
bool q2_animation_time(q2_weapon_call *c, uint64_t *out, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    unsigned rate = animation_rate(c);
    uint64_t native = q2_animation_native(c);
    uint64_t interval = native;
    if (!c->equipment && s->phase == QA_Q2_FIRING && !q2_interval(c, native, &interval, e)) return false;
    s->gun_rate = interval == native ? (float)rate : interval ?
        (float)((double)Q2_NS / (double)interval) : 0;
    *out = interval;
    return true;
}
bool q2_animation_deadline(q2_weapon_call *c, uint64_t from, uint64_t extra,
    uint64_t *out, qa_error *e) {
    uint64_t duration;
    if (!q2_animation_time(c, &duration, e)) return false;
    *out = q2_deadline(from, q2_deadline(duration, extra));
    return true;
}
static bool classic(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    const qa_q2_weapon_definition *d = c->definition;
    int idle = d->fire_last + 1;
    bool lmctf = c->input.source_rules == QA_Q2_WEAPON_RULES_LMCTF &&
                 d->weapon != QA_Q2_LMCTF_PLASMA;
    if (s->phase == QA_Q2_DROPPING) {
        if (s->frame == d->deactivate_last)
            return q2_change_weapon(c, e);
        if (d->deactivate_last - s->frame == 4 && !q2_reverse_animation(c, e))
            return false;
        ++s->frame;
        return true;
    }
    if (s->phase == QA_Q2_ACTIVATING) {
        if (lmctf) {
            float fastswitch;
            if (!qa_q2_source_value(c->game, "fastswitch", 0, &fastswitch, e))
                return false;
            if (!q2_actor_live(c->game, c->actor->id))
                return true;
            if (fastswitch != 0)
                s->frame = d->activate_last;
        }
        if (s->frame == d->activate_last) {
            s->phase = QA_Q2_READY;
            s->frame = idle;
        } else
            ++s->frame;
        return true;
    }
    if ((s->pending != QA_Q2_WEAPON_NONE || s->handoff == QA_Q2_PRIMARY_HOLSTERING) &&
        s->phase != QA_Q2_FIRING) {
        s->phase = QA_Q2_DROPPING;
        if (lmctf && s->pending != QA_Q2_WEAPON_NONE) {
            float fastswitch;
            if (!qa_q2_source_value(c->game, "fastswitch", 0, &fastswitch, e))
                return false;
            if (!q2_actor_live(c->game, c->actor->id))
                return true;
            if (fastswitch != 0)
                return q2_change_weapon(c, e);
        }
        s->frame = d->idle_last + 1;
        return d->deactivate_last - s->frame >= 4 || q2_reverse_animation(c, e);
    }
    if (s->phase == QA_Q2_READY) {
        if (c->input.attack || s->latched_attack) {
            int ammo;
            s->latched_attack = false;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo < d->quantity)
                return q2_no_ammo(c, true, e);
            s->frame = d->activate_last + 1;
            s->phase = QA_Q2_FIRING;
            if (!q2_attack_animation(c, 1, e))
                return false;
        } else {
            if (s->frame == d->idle_last) {
                s->frame = idle;
                return true;
            }
            if (q2_frame_bit(d->pauses, s->frame) && (int)(q2_random(c->game) * 16) != 0)
                return true;
            ++s->frame;
            return true;
        }
    }
    if (s->phase == QA_Q2_FIRING) {
        if (q2_frame_bit(d->fires, s->frame)) {
            s->source_firing = true;
            if (!q2_power_sound(c, e) || !q2_fire(c, false, e))
                return false;
        } else
            ++s->frame;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        if (lmctf && s->pending != QA_Q2_WEAPON_NONE) {
            float fastswitch;
            if (!qa_q2_source_value(c->game, "fastswitch", 0, &fastswitch, e))
                return false;
            if (!q2_actor_live(c->game, c->actor->id))
                return true;
            if (fastswitch != 0)
                return q2_loop(c, "", e) && q2_change_weapon(c, e);
        }
        if (s->frame == idle + 1)
            s->phase = QA_Q2_READY;
        if (lmctf && c->game->ammo[d->weapon] != 0) {
            int ammo;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo < d->quantity)
                return q2_no_ammo(c, true, e);
        }
    }
    return true;
}
static bool rerelease(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    const qa_q2_weapon_definition *d = c->definition;
    uint64_t now = c->now_ns;
    int idle = d->fire_last + 1, last = d->weapon == QA_Q2_BFG ? 54 : d->idle_last;
    if (s->phase == QA_Q2_DROPPING) {
        if (s->think_ns <= now) {
            if (s->frame == d->deactivate_last)
                return q2_change_weapon(c, e);
            if (d->deactivate_last - s->frame == 4 && !q2_reverse_animation(c, e))
                return false;
            ++s->frame;
            if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
        }
        return true;
    }
    if (s->phase == QA_Q2_ACTIVATING && (s->think_ns <= now || c->input.instant_switch)) {
        if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
        if (s->frame == d->activate_last || c->input.instant_switch) {
            s->phase = QA_Q2_READY;
            s->frame = idle;
            s->fire_buffered = false;
            s->fire_finished_ns = 0;
            if (!c->input.instant_switch &&
                !q2_animation_deadline(c, now, 0, &s->fire_finished_ns, e)) return false;
        } else
            ++s->frame;
        return true;
    }
    bool change = s->pending != QA_Q2_WEAPON_NONE || s->handoff == QA_Q2_PRIMARY_HOLSTERING;
    if ((change || (!c->input.instant_switch && c->input.holster)) && s->phase != QA_Q2_FIRING) {
        if (c->input.instant_switch || s->think_ns <= now) {
            if (s->handoff == QA_Q2_PRIMARY_ACTIVE && s->pending == QA_Q2_WEAPON_NONE)
                s->pending = s->weapon;
            s->phase = QA_Q2_DROPPING;
            if (c->input.instant_switch)
                return q2_change_weapon(c, e);
            s->frame = last + 1;
            if (d->deactivate_last - s->frame < 4 && !q2_reverse_animation(c, e))
                return false;
            if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
        }
        return true;
    }
    if (s->phase == QA_Q2_READY) {
        if ((s->fire_buffered || s->latched_attack || c->input.attack) &&
            s->fire_finished_ns <= now) {
            int ammo;
            s->latched_attack = false;
            s->think_ns = now;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo < d->quantity)
                return q2_no_ammo(c, true, e);
            s->phase = QA_Q2_FIRING;
            s->last_firing_ns = q2_deadline(now, 2500 * Q2_MS);
            if (!d->repeating) {
                s->frame = d->activate_last + 1;
                s->fire_buffered = false;
                if (!q2_animation_deadline(c, s->think_ns, c->input.weapon_thunk ? c->frame_ns : 0,
                    &s->think_ns, e) || !q2_animation_deadline(c, now, 0, &s->fire_finished_ns, e)) return false;
                if (q2_frame_bit(d->fires, s->frame) &&
                    (!q2_power_sound(c, e) || !q2_fire(c, false, e)))
                    return false;
                return q2_attack_animation(c, 1, e);
            }
        } else if (s->think_ns <= now) {
            if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
            if (s->frame == last) {
                s->frame = idle;
                return true;
            }
            if (!q2_frame_bit(d->pauses, s->frame) || (int)(q2_random(c->game) * 16) == 0)
                ++s->frame;
            return true;
        }
    }
    if (s->phase == QA_Q2_FIRING && s->think_ns <= now) {
        s->last_firing_ns = q2_deadline(now, 2500 * Q2_MS);
        if (!d->repeating)
            ++s->frame;
        if (!q2_animation_deadline(c, now, 0, &s->fire_finished_ns, e)) return false;
        bool buffered = s->fire_buffered;
        s->fire_buffered = false;
        if (d->repeating) {
            if (!q2_fire(c, buffered, e))
                return false;
        } else if (q2_frame_bit(d->fires, s->frame) &&
                   !(d->weapon == QA_Q2_SHOTGUN && s->frame == 9))
            if (!q2_power_sound(c, e) || !q2_fire(c, buffered, e))
                return false;
        if (s->frame == idle) {
            s->phase = QA_Q2_READY;
            s->fire_buffered = false;
        }
        if (!q2_animation_deadline(c, now, d->repeating && c->input.weapon_thunk ? c->frame_ns : 0,
            &s->think_ns, e)) return false;
    }
    return true;
}
bool q2_generic_classic(q2_weapon_call *c, qa_error *e) {
    if (c->state->handoff == QA_Q2_PRIMARY_HOLSTERED)
        return true;
    if (c->input.source_rules == QA_Q2_WEAPON_RULES_LMCTF)
        c->state->source_firing = false;
    qa_q2_weapon_phase phase = c->state->phase;
    if (!classic(c, e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id) || c->state->handoff == QA_Q2_PRIMARY_HOLSTERED ||
        phase != c->state->phase)
        return true;
    bool grapple = c->definition != NULL && c->definition->weapon == QA_Q2_GRAPPLE;
    bool extra = grapple ? c->state->phase != QA_Q2_FIRING
                         : c->input.source_rules == QA_Q2_WEAPON_RULES_CTF && c->input.haste;
    return !extra || classic(c, e);
}
bool q2_generic(q2_weapon_call *c, qa_error *e) {
    if (c->state->handoff == QA_Q2_PRIMARY_HOLSTERED)
        return true;
    return c->rerelease ? rerelease(c, e) : q2_generic_classic(c, e);
}
