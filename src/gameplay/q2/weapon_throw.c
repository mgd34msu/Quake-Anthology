#include "internal.h"

bool q2_hand_calculate(q2_weapon_call *c, uint64_t expires, bool alive, bool held,
                       qa_q2_hand_projection_fn project, void *project_context, q2_hand_spec *out,
                       qa_error *e) {
    qa_vec3 angles = c->input.angles;
    if (c->rerelease)
        angles.x = fmaxf(-62.5f, angles.x);
    qa_vec3 offset = c->rerelease ? qa_v3(2, 0, -14) : qa_v3(8, 8, -8);
    if (project != NULL) {
        if (!project(project_context, c->actor->id, angles, offset, &out->start, &out->direction,
                     e))
            return false;
        if (!qa_vec_finite(out->start) || !qa_vec_finite(out->direction)) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid selected hand grenade projection");
            return false;
        }
    } else if (!q2_project(c, angles, offset, &out->start, &out->direction, e))
        return false;
    out->fuse = expires >= c->now_ns ? (float)((double)(expires - c->now_ns) / 1e9)
                                     : -(float)((double)(c->now_ns - expires) / 1e9);
    float speed = 400 + (3 - out->fuse) * (400.0f / 3);
    out->speed = truncf(c->rerelease ? alive ? fminf(800, speed) : 400 : speed);
    out->held = held;
    return true;
}

static bool reserve(q2_weapon_call *c, bool *reserved, qa_error *e) {
    *reserved = false;
    if (c->definition->weapon != QA_Q2_GRENADES) {
        int ammo;
        if (!q2_ammo(c, &ammo, e))
            return false;
        *reserved = ammo > 0;
        return true;
    }
    if (c->state->hand_reservation != QA_Q2_HAND_UNRESERVED)
        return true;
    bool infinite =
        c->rerelease ? c->input.infinite_ammo : (c->game->options.deathmatch_flags & 8192u) != 0;
    if (infinite) {
        c->state->hand_reservation = QA_Q2_HAND_INFINITE;
        *reserved = true;
        return true;
    }
    int before;
    if (!q2_ammo(c, &before, e))
        return false;
    bool consumed;
    qa_item_id item = c->game->ammo[QA_Q2_GRENADES];
    if (!qa_inventory_consume(c->game->services.inventory, c->actor->id, item, 1, &consumed, e))
        return false;
    if (!consumed)
        return true;
    c->state->hand_reservation = QA_Q2_HAND_FINITE;
    *reserved = true;
    if (c->rerelease && before > c->definition->warning && before - 1 <= c->definition->warning &&
        !q2_sound(c, "weapons/lowammo.wav", 0, 1, e))
        return false;
    return c->game->hooks.ammo_changed == NULL ||
           c->game->hooks.ammo_changed(c->game->hooks.context, c->actor->id, item, e);
}
bool q2_throw(q2_weapon_call *c, bool held, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_q2_weapon_state *s = c->state;
    bool hand = c->definition->weapon == QA_Q2_GRENADES, trap = c->definition->weapon == QA_Q2_TRAP;
    if (hand && s->hand_reservation == QA_Q2_HAND_UNRESERVED)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(c->game->services.combat, c->actor->id, &combat, e))
        return false;
    double fuse = ((double)s->grenade_ns - (double)c->now_ns) / 1e9;
    float duration = c->rerelease && trap ? 5 : 3;
    float minimum = c->rerelease && trap ? 300 : 400;
    float maximum = c->rerelease && trap ? 700 : 800;
    float speed = minimum + (duration - (float)fuse) * (maximum - minimum) / duration;
    if (c->rerelease)
        speed = combat.health > 0 ? fminf(maximum, speed) : minimum;
    else if (!hand && !trap)
        speed = fminf(maximum, speed);
    speed = truncf(speed);
    qa_vec3 angles = c->input.angles, start, dir;
    if (c->rerelease)
        angles.x = fmaxf(-62.5f, angles.x);
    qa_vec3 offset = c->rerelease ? (hand   ? qa_v3(2, 0, -14)
                                     : trap ? qa_v3(8, 0, -8)
                                            : qa_v3(0, 0, -22))
                                  : qa_v3(8, 8, -8);
    float multiplier;
    if (hand) {
        if (!q2_multiplier(c, &multiplier, e)) return false;
        q2_hand_spec spec;
        if (!q2_hand_calculate(c, s->grenade_ns, combat.health > 0, held, NULL, NULL, &spec, e))
            return false;
        start = spec.start;
        dir = spec.direction;
        speed = spec.speed;
        fuse = spec.fuse;
    } else if (!q2_project(c, angles, offset, &start, &dir, e))
        return false;
    if (!c->rerelease && !hand && !trap) {
        qa_body_state body;
        qa_vec3 right, up;
        if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
            return false;
        qa_builtin_angle_vectors(angles, &dir, &right, &up);
        float side = c->input.hand == QA_Q2_LEFT_HAND     ? 4
                     : c->input.hand == QA_Q2_CENTER_HAND ? 0
                                                          : -4;
        start = qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(right, side)),
                           qa_vec_scale(up, c->input.view_height - 22));
    }
    if (hand)
        s->hand_reservation = QA_Q2_HAND_UNRESERVED;
    if (c->rerelease) s->grenade_ns = 0;
    if (hand && !c->rerelease) {
        uint64_t recovery;
        if (!q2_interval(c, Q2_NS, &recovery, e)) return false;
        s->grenade_ns = q2_deadline(c->now_ns, recovery);
    }
    if (!hand && !q2_multiplier(c, &multiplier, e)) return false;
    float damage = 125 * multiplier, projectile_damage = hand || trap ? damage : 3 * multiplier;
    if (!q2_projectile_spawn(c,
                             hand   ? Q2_GRENADE
                             : trap ? Q2_TRAP
                                    : Q2_TESLA,
                             start, dir, projectile_damage, 0, speed,
                             hand || trap ? 165 : 128, hand || trap ? damage : 0,
                             hand   ? (float)fuse
                             : trap ? (c->rerelease ? 1 : (float)fuse)
                                    : 30,
                             hand   ? 15
                             : trap ? 39
                                    : 45,
                             hand   ? (held ? 24 : 16)
                             : trap ? 39
                                    : 45,
                             hand, held, e))
        return false;
    if (!hand && !q2_consume(c, 1, !c->rerelease && !trap, e))
        return false;
    if (!hand && !c->rerelease) {
        uint64_t recovery;
        if (!q2_interval(c, Q2_NS, &recovery, e)) return false;
        s->grenade_ns = q2_deadline(c->now_ns, recovery);
    }
    if (!c->rerelease && combat.health > 0 && (hand || !trap) &&
        !q2_animation(c, c->input.ducked ? 0 : 2, c->input.ducked ? 159 : 119,
                       c->input.ducked ? 162 : 112, e))
        return false;
    return q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
bool q2_throw_frame(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    const qa_q2_weapon_definition *d = c->definition;
    bool hand = d->weapon == QA_Q2_GRENADES, trap = d->weapon == QA_Q2_TRAP;
    int idle = d->fire_last + 1, ready = c->rerelease && hand ? d->idle_last + 1 : idle;
    int sound_frame = hand || trap ? 5 : 99, hold_frame = hand || trap ? 11 : 1,
        fire_frame = hand || trap ? 12 : 2;
    const char *cock = hand ? "weapons/hgrena1b.wav" : "weapons/trapcock.wav";
    const char *loop = hand ? "weapons/hgrenc1b.wav" : trap ? "weapons/traploop.wav" : "";
    bool explodes = hand || (trap && !c->rerelease);
    uint64_t now = c->now_ns;
    if (s->handoff == QA_Q2_PRIMARY_HOLSTERING)
        return !c->rerelease || s->think_ns <= now || c->input.instant_switch
                   ? q2_change_weapon(c, e)
                   : true;
    if (s->pending != QA_Q2_WEAPON_NONE && s->phase == QA_Q2_READY) {
        if (!c->rerelease || s->think_ns <= now) {
            if (!q2_change_weapon(c, e))
                return false;
            if (c->rerelease && !q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
        }
        return true;
    }
    if (s->phase == QA_Q2_ACTIVATING) {
        if (!c->rerelease || s->think_ns <= now) {
            s->phase = QA_Q2_READY;
            s->frame = ready;
            if (c->rerelease) {
                if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
                s->fire_finished_ns = s->think_ns;
            }
        }
        return true;
    }
    if (s->phase == QA_Q2_READY) {
        if ((s->latched_attack || c->input.attack || (c->rerelease && s->fire_buffered)) &&
            (!c->rerelease || s->fire_finished_ns <= now)) {
            bool admitted;
            s->latched_attack = false;
            if (!reserve(c, &admitted, e))
                return false;
            if (!admitted)
                return q2_no_ammo(c, true, e);
            s->frame = c->rerelease && hand ? 2 : 1;
            s->phase = QA_Q2_FIRING;
            s->grenade_ns = 0;
            if (c->rerelease && !q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
            return true;
        }
        if (c->rerelease) {
            if (s->think_ns > now)
                return true;
            if (!q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
            if (s->frame >= d->idle_last) {
                s->frame = idle;
                return true;
            }
        } else if (!hand && !trap && s->frame == d->idle_last) {
            s->frame = idle;
            return true;
        }
        if (q2_frame_bit(d->pauses, s->frame) && (int)(q2_random(c->game) * 16) != 0)
            return true;
        if (++s->frame > d->idle_last)
            s->frame = idle;
        return true;
    }
    if (s->phase != QA_Q2_FIRING)
        return true;
    if (c->rerelease) {
        s->last_firing_ns = q2_deadline(now, 2500 * Q2_MS);
        if (s->think_ns > now)
            return true;
    }
    if (s->frame == sound_frame && !q2_sound(c, cock, 1, 1, e))
        return false;
    uint64_t wait;
    if (!q2_interval(c, Q2_NS / (c->rerelease && c->input.haste ? 2u : 1u) /
        (c->rerelease && c->input.quad_fire_until_ns > now ? 2u : 1u), &wait, e)) return false;
    if (s->frame == hold_frame) {
        if (s->grenade_ns == 0 && (!c->rerelease || s->grenade_finished_ns == 0))
            s->grenade_ns = q2_deadline(now, 3200 * Q2_MS);
        if ((!c->rerelease || !s->grenade_blew_up) && !q2_loop(c, loop, e))
            return false;
        if (explodes && !s->grenade_blew_up && now >= s->grenade_ns) {
            if ((c->rerelease && !q2_power_sound(c, e)) || !q2_loop(c, "", e) ||
                !q2_throw(c, true, e))
                return false;
            s->grenade_blew_up = true;
            if (c->rerelease)
                s->grenade_finished_ns = q2_deadline(now, wait);
        }
        if (c->input.attack) {
            if (c->rerelease)
                s->think_ns = q2_deadline(now, Q2_MS);
            return true;
        }
        if (s->grenade_blew_up) {
            if (now < (c->rerelease ? s->grenade_finished_ns : s->grenade_ns))
                return true;
            s->frame = d->fire_last;
            s->grenade_blew_up = false;
            if (c->rerelease && !q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
        } else if (c->rerelease) {
            ++s->frame;
            if (!q2_power_sound(c, e) || !q2_loop(c, "", e) || !q2_throw(c, false, e))
                return false;
            s->grenade_finished_ns = q2_deadline(now, wait);
            if (!q2_animation(c, c->input.ducked ? 0 : 2, c->input.ducked ? 159 : 119,
                              c->input.ducked ? 162 : 112, e))
                return false;
        }
    }
    if (!c->rerelease && s->frame == fire_frame)
        if (!q2_loop(c, "", e) || !q2_throw(c, !hand && !trap, e))
            return false;
    if (c->rerelease && !q2_animation_deadline(c, now, 0, &s->think_ns, e)) return false;
    if (s->frame == d->fire_last && now < (c->rerelease ? s->grenade_finished_ns : s->grenade_ns))
        return true;
    if (++s->frame == idle) {
        s->phase = QA_Q2_READY;
        if (c->rerelease) {
            s->grenade_finished_ns = 0;
            s->fire_buffered = false;
            if (!q2_animation_deadline(c, now, 0, &s->fire_finished_ns, e)) return false;
            s->frame = ready;
            int ammo;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo == 0)
                return q2_no_ammo(c, false, e) && q2_change_weapon(c, e);
        } else
            s->grenade_ns = 0;
    }
    return true;
}
