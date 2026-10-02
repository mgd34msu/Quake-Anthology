#include "internal.h"

bool q2_weapon_fired(qa_q2_game *g, qa_actor_id actor, qa_q2_weapon weapon, qa_error *e) {
    if (g->hooks.fired == NULL || !q2_actor_live(g, actor))
        return true;
    if (weapon <= QA_Q2_WEAPON_NONE || weapon >= QA_Q2_WEAPON_COUNT || !g->items[weapon]) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Completed Q2 shot has no registered weapon item");
        return false;
    }
    return g->hooks.fired(g->hooks.context, actor, g->items[weapon], e);
}

static qa_vec3 forward(q2_weapon_call *c) {
    qa_vec3 f;
    qa_builtin_angle_vectors(c->input.angles, &f, NULL, NULL);
    return f;
}
static bool flash(q2_weapon_call *c, int code, qa_error *e) {
    return q2_event(c, QA_BUILTIN_MUZZLE, code, qa_v3(0, 0, 0), qa_v3(0, 0, 0), e);
}
static bool finish(q2_weapon_call *c, int code, qa_vec3 start, int consume, qa_error *e) {
    return flash(c, code, e) && q2_noise(c, start, e) && q2_consume(c, consume, true, e) &&
           q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
static bool lag_begin(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, bool *active,
                      qa_error *e) {
    *active = c->rerelease && c->game->hooks.lag_begin != NULL;
    return !*active ||
           c->game->hooks.lag_begin(c->game->hooks.context, c->actor->id, start, direction, e);
}
static bool lag_end(q2_weapon_call *c, bool active, bool result, qa_error *e) {
    qa_error ignored;
    bool restored =
        !active || c->game->hooks.lag_end(c->game->hooks.context, result ? e : &ignored);
    return result && restored;
}
static bool blaster(q2_weapon_call *c, qa_vec3 offset, float damage, bool hyper, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_vec_add(qa_v3(24, 8, -8), offset), &start, &dir, e))
        return false;
    qa_vec3 kick = qa_v3(-1, 0, 0);
    if (hyper && c->rerelease) {
        kick.x = q2_crandom(c->game) * 0.7f;
        kick.y = q2_crandom(c->game) * 0.7f;
        kick.z = q2_crandom(c->game) * 0.7f;
    }
    q2_kick(c, qa_vec_scale(forward(c), -2), kick, c->rerelease ? 0.2f : 0);
    float multiplier;
    if (!q2_multiplier(c, &multiplier, e)) return false;
    return q2_projectile_spawn(c, Q2_BOLT, start, dir, damage * multiplier, 1,
                               c->rerelease && !hyper ? 1500 : 1000, 0, 0, 2, hyper ? 10 : 1, 0,
                               false, false, e) &&
           flash(c, hyper ? 14 : 0, e) && q2_noise(c, start, e) &&
           q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
static bool hyperblaster(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (c->rerelease) {
        s->frame = s->frame > 20 ? 6 : s->frame + 1;
        if (s->frame == 12) {
            if (ammo > 0 && q2_continues(c))
                s->frame = 6;
            else if (!q2_sound(c, "weapons/hyprbd1a.wav", 0, 1, e))
                return false;
        }
        if (!q2_loop(c, s->frame >= 6 && s->frame <= 11 ? "weapons/hyprbl1a.wav" : "", e))
            return false;
        if (q2_continues(c) && s->frame >= 6 && s->frame <= 11) {
            if (ammo < 1)
                return q2_no_ammo(c, true, e);
            float rotation = (float)(s->frame - 5) * 6.283185307179586f / 6;
            if (!blaster(c, qa_v3(-4 * sinf(rotation), 4 * cosf(rotation), 0),
                         c->game->options.deathmatch ? 15 : 20, true, e) ||
                !q2_power_sound(c, e) || !q2_consume(c, 1, true, e))
                return false;
            return q2_attack_animation(c, (int)(q2_random(c->game) + 0.25f), e);
        }
        return true;
    }
    if (!q2_loop(c, "weapons/hyprbl1a.wav", e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (!q2_continues(c))
        ++s->frame;
    else {
        if (ammo < 1) {
            if (!q2_no_ammo(c, true, e))
                return false;
        } else {
            float rotation = (float)(s->frame - 5) * 6.283185307179586f / 6;
            if (!blaster(c, qa_v3(-4 * sinf(rotation), 0, 4 * cosf(rotation)),
                         c->game->options.deathmatch ? 15 : 20, true, e) ||
                !q2_consume(c, 1, true, e) || !q2_attack_animation(c, 1, e))
                return false;
        }
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        ++s->frame;
        if (!q2_ammo(c, &ammo, e))
            return false;
        if (s->frame == 12 && ammo > 0)
            s->frame = 6;
    }
    return s->frame != 12 || (q2_sound(c, "weapons/hyprbd1a.wav", 0, 1, e) && q2_loop(c, "", e));
}
static bool machinegun(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    if (!q2_continues(c)) {
        s->machinegun_shots = 0;
        s->frame = c->rerelease ? 6 : s->frame + 1;
        return true;
    }
    s->frame = s->frame == 4 ? 5 : 4;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (ammo < 1) {
        s->frame = 6;
        return q2_no_ammo(c, true, e);
    }
    qa_vec3 origin, angles;
    if (c->rerelease) {
        origin.x = q2_crandom(c->game) * 0.35f;
        origin.y = q2_crandom(c->game) * 0.35f;
        origin.z = q2_crandom(c->game) * 0.35f;
        angles.x = q2_crandom(c->game) * 0.7f;
        angles.y = q2_crandom(c->game) * 0.7f;
        angles.z = q2_crandom(c->game) * 0.7f;
    } else {
        origin.y = q2_crandom(c->game) * 0.35f;
        angles.y = q2_crandom(c->game) * 0.7f;
        origin.z = q2_crandom(c->game) * 0.35f;
        angles.z = q2_crandom(c->game) * 0.7f;
        origin.x = q2_crandom(c->game) * 0.35f;
        angles.x = (float)s->machinegun_shots * -1.5f;
        if (!c->game->options.deathmatch && s->machinegun_shots < 9)
            ++s->machinegun_shots;
    }
    q2_kick(c, origin, angles, c->rerelease ? 0.2f : 0);
    qa_vec3 start, dir;
    if (!q2_project(c, c->rerelease ? c->input.angles : qa_vec_add(c->input.angles, angles),
                    qa_v3(0, c->rerelease ? 0 : 8, -8), &start, &dir, e))
        return false;
    bool lag;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e))
        return lag_end(c, lag, false, e);
    bool result = q2_bullet(c, start, dir, 8 * damage_factor, 2 * kick_factor, 300, 500, 1, 4, false, e);
    if (!lag_end(c, lag, result, e) || (c->rerelease && !q2_power_sound(c, e)) ||
        !finish(c, 1, start, 1, e))
        return false;
    return q2_attack_animation(c, (int)(q2_random(c->game) + 0.25f), e);
}
static bool chaingun(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (c->rerelease && s->frame > 31) {
        s->frame = 5;
        if (!q2_sound(c, "weapons/chngnu1a.wav", 0, 2, e))
            return false;
    } else {
        if (!c->rerelease && s->frame == 5 && !q2_sound(c, "weapons/chngnu1a.wav", 0, 2, e))
            return false;
        if (s->frame == 14 && !q2_continues(c)) {
            s->frame = 32;
            return q2_loop(c, "", e);
        }
        if (s->frame == 21 && q2_continues(c) && ammo > 0)
            s->frame = 15;
        else
            ++s->frame;
    }
    if (s->frame == 22) {
        if (!q2_loop(c, "", e) || !q2_sound(c, "weapons/chngnd1a.wav", 0, 2, e))
            return false;
    } else if (!c->rerelease && !q2_loop(c, "weapons/chngnl1a.wav", e))
        return false;
    if (c->rerelease && (s->frame < 5 || s->frame > 21))
        return true;
    if (c->rerelease && !q2_loop(c, "weapons/chngnl1a.wav", e))
        return false;
    if (!q2_attack_animation(c, s->frame & 1, e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    int shots = s->frame <= 9 ? 1 : s->frame <= 14 ? (q2_continues(c) ? 2 : 1) : 3;
    if (shots > ammo)
        shots = ammo;
    if (shots <= 0)
        return q2_no_ammo(c, true, e);
    qa_vec3 o, a;
    if (c->rerelease) {
        o.x = q2_crandom(c->game) * 0.35f;
        o.y = q2_crandom(c->game) * 0.35f;
        o.z = q2_crandom(c->game) * 0.35f;
        float f = 0.5f + (float)shots * 0.15f;
        a.x = q2_crandom(c->game) * f;
        a.y = q2_crandom(c->game) * f;
        a.z = q2_crandom(c->game) * f;
    } else {
        o.x = q2_crandom(c->game) * 0.35f;
        a.x = q2_crandom(c->game) * 0.7f;
        o.y = q2_crandom(c->game) * 0.35f;
        a.y = q2_crandom(c->game) * 0.7f;
        o.z = q2_crandom(c->game) * 0.35f;
        a.z = q2_crandom(c->game) * 0.7f;
    }
    q2_kick(c, o, a, c->rerelease ? 0.2f : 0);
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_v3(0, 0, -8), &start, &dir, e))
        return false;
    bool lag, result = true;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    for (int i = 0; i < shots && result && q2_actor_live(c->game, c->actor->id); ++i) {
        float side = (c->rerelease ? 0 : 7) + q2_crandom(c->game) * 4;
        float up = q2_crandom(c->game) * 4 - 8;
        float damage_factor, kick_factor;
        result = q2_project(c, c->input.angles, qa_v3(0, side, up), &start, &dir, e) &&
                 q2_multiplier(c, &damage_factor, e) && q2_multiplier(c, &kick_factor, e) &&
                 q2_bullet(c, start, dir, (c->game->options.deathmatch ? 6 : 8) * damage_factor,
                           2 * kick_factor, 300, 500, 1, 5, false, e);
    }
    return lag_end(c, lag, result, e) && (!c->rerelease || q2_power_sound(c, e)) &&
           finish(c, 3 + shots - 1, start, shots, e);
}
static bool shotgun(q2_weapon_call *c, bool super, qa_error *e) {
    if (!super && c->state->frame == 9) {
        if (!c->rerelease)
            ++c->state->frame;
        return true;
    }
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_v3(0, c->rerelease ? 0 : 8, -8), &start, &dir, e))
        return false;
    q2_kick(c, qa_vec_scale(forward(c), -2), qa_v3(-2, 0, 0), c->rerelease ? 0.2f : 0);
    bool lag, result = true;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    if (super)
        for (int i = 0; i < 2 && result && q2_actor_live(c->game, c->actor->id); ++i) {
            qa_vec3 angles = c->input.angles;
            angles.y += i == 0 ? -5 : 5;
            if (c->rerelease)
                result = q2_project(c, angles, qa_v3(0, 0, -8), &start, &dir, e);
            else
                qa_builtin_angle_vectors(angles, &dir, NULL, NULL);
            if (result) {
                float damage_factor, kick_factor;
                result = q2_multiplier(c, &damage_factor, e) && q2_multiplier(c, &kick_factor, e) &&
                    q2_bullet(c, start, dir, 6 * damage_factor, 12 * kick_factor, 1000, 500, 10, 3, true, e);
            }
        }
    else {
        float damage_factor, kick_factor;
        result = q2_multiplier(c, &damage_factor, e) && q2_multiplier(c, &kick_factor, e) &&
            q2_bullet(c, start, dir, 4 * damage_factor, 8 * kick_factor, 500, 500, 12, 2, true, e);
    }
    if (!lag_end(c, lag, result, e))
        return false;
    if (!c->rerelease)
        ++c->state->frame;
    return finish(c, super ? 13 : 2, start, c->definition->quantity, e);
}
static bool launch(q2_weapon_call *c, qa_error *e) {
    qa_vec3 start, dir, angles = c->input.angles, offset = qa_v3(8, 8, -8);
    qa_q2_weapon w = c->definition->weapon;
    float m = 1, damage = 0, speed = 0, radius = 0, splash = 0, fuse = 0, kick = 0;
    if (w == QA_Q2_PHALANX && !q2_multiplier(c, &m, e)) return false;
    int mod = 0, splash_mod = 0, muzzle = 0;
    q2_projectile_kind kind = Q2_PROJECTILE_NONE;
    switch (w) {
    case QA_Q2_GRENADELAUNCHER:
        if (c->rerelease) {
            angles.x = fmaxf(-62.5f, angles.x);
            offset.y = 0;
        }
        kind = Q2_GRENADE;
        damage = 120;
        speed = 600;
        radius = 160;
        splash = damage;
        fuse = 2.5f;
        mod = 6;
        splash_mod = 7;
        muzzle = 8;
        break;
    case QA_Q2_ROCKETLAUNCHER:
        kind = Q2_ROCKET;
        damage = 100 + floorf(q2_random(c->game) * 20);
        speed = 650;
        radius = 120;
        splash = 120;
        fuse = 8000 / speed;
        mod = 8;
        splash_mod = 9;
        muzzle = 7;
        break;
    case QA_Q2_IONRIPPER:
        angles.y += q2_crandom(c->game);
        offset = qa_v3(16, 7, -8);
        kind = Q2_ION;
        damage = c->game->options.deathmatch ? 30 : 50;
        speed = 500;
        fuse = 3;
        mod = 34;
        muzzle = 16;
        break;
    case QA_Q2_PHALANX:
        angles.y += c->state->frame == 8 ? -1.5f : 1.5f;
        offset = qa_v3(0, 8, -8);
        kind = Q2_PLASMA;
        damage = (70 + floorf(q2_random(c->game) * 10)) * m;
        speed = 725;
        radius = 120;
        splash = c->state->frame == 8 ? 30 : 120 * m;
        fuse = 8000 / speed;
        mod = 35;
        splash_mod = 35;
        muzzle = c->state->frame == 8 ? 20 : 18;
        break;
    case QA_Q2_DISINTEGRATOR:
        kind = Q2_TRACKER;
        damage = (c->rerelease ? (c->game->options.deathmatch ? 45 : 135)
                               : (c->game->options.deathmatch ? 30 : 45));
        speed = 1000;
        fuse = 10;
        mod = 51;
        offset = qa_v3(24, 8, -8);
        muzzle = 35;
        break;
    case QA_Q2_PROXLAUNCHER:
        kind = Q2_PROX;
        damage = 90;
        splash = damage;
        speed = 600;
        radius = 192;
        fuse = 45;
        mod = 46;
        splash_mod = 46;
        muzzle = c->rerelease ? 31 : 6;
        if (c->rerelease) {
            angles.x = fmaxf(-62.5f, angles.x);
            offset.y = 0;
        }
        break;
    case QA_Q2_BFG:
        if (c->state->frame == 9) {
            if (!c->rerelease)
                ++c->state->frame;
            qa_body_state body;
            if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
                return false;
            return flash(c, 12, e) && q2_noise(c, c->rerelease ? body.origin : qa_v3(0, 0, 0), e);
        }
        {
            int ammo;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo < 50) {
                if (!c->rerelease)
                    ++c->state->frame;
                return true;
            }
        }
        kind = Q2_BFG_BALL;
        damage = c->game->options.deathmatch ? 200 : 500;
        radius = 1000;
        speed = 400;
        fuse = 20;
        mod = 13;
        splash_mod = 14;
        muzzle = 19;
        break;
    default:
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Weapon does not launch a Q2 projectile");
        return false;
    }
    if (!q2_project(c, w == QA_Q2_PHALANX && !c->rerelease ? c->input.angles : angles, offset,
                    &start, &dir, e))
        return false;
    if (w == QA_Q2_PHALANX && !c->rerelease)
        qa_builtin_angle_vectors(angles, &dir, NULL, NULL);
    if (w == QA_Q2_DISINTEGRATOR) {
        if (!q2_tracker_target(c, start, dir, &c->projectile_enemy, e))
            return false;
        c->has_projectile_enemy = true;
    }
    q2_kick(c, qa_vec_scale(forward(c), -2), qa_v3(-1, 0, 0), c->rerelease ? 0.2f : 0);
    if (w == QA_Q2_IONRIPPER) {
        qa_vec3 aim;
        qa_builtin_angle_vectors(c->rerelease ? c->input.angles : angles, &aim, NULL, NULL);
        q2_kick(c, qa_vec_scale(aim, -3), qa_v3(-3, 0, 0), c->rerelease ? 0.2f : 0);
    } else if (w == QA_Q2_PHALANX)
        q2_kick(c, qa_vec_scale(forward(c), -2), qa_v3(-2, 0, 0), c->rerelease ? 0.2f : 0);
    if (w != QA_Q2_PHALANX) {
        if (!q2_multiplier(c, &m, e)) return false;
        damage *= m;
        if (w == QA_Q2_ROCKETLAUNCHER) {
            if (!q2_multiplier(c, &m, e)) return false;
            splash *= m;
        } else if (w == QA_Q2_GRENADELAUNCHER || w == QA_Q2_PROXLAUNCHER)
            splash = damage;
    }
    if (!q2_projectile_spawn(c, kind, start, dir, damage, kick, speed, radius, splash, fuse, mod,
                             splash_mod, false, false, e))
        return false;
    if (w == QA_Q2_BFG)
        q2_kick(c, qa_vec_scale(forward(c), -2),
                qa_v3(c->rerelease ? -20 : -40, 0, q2_crandom(c->game) * 8),
                c->rerelease ? fmaxf(0, 0.6f - (float)((double)c->frame_ns / 1e9)) : 0.5f);
    bool second = w == QA_Q2_PHALANX && c->state->frame == 8;
    if (!c->rerelease)
        ++c->state->frame;
    if (w == QA_Q2_PHALANX)
        return (second ? q2_consume(c, 1, true, e) && (!c->rerelease || flash(c, 20, e))
                       : flash(c, 18, e) && q2_noise(c, start, e)) &&
               q2_weapon_fired(c->game, c->actor->id, w, e);
    if (w == QA_Q2_BFG && !c->rerelease)
        return q2_noise(c, start, e) && q2_consume(c, 50, true, e) &&
               q2_weapon_fired(c->game, c->actor->id, w, e);
    return finish(c, muzzle, start, c->definition->quantity, e);
}
static bool rail(q2_weapon_call *c, qa_error *e) {
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_v3(0, 7, -8), &start, &dir, e))
        return false;
    q2_kick(c, qa_vec_scale(forward(c), -3), qa_v3(-3, 0, 0), c->rerelease ? 0.2f : 0);
    float damage = c->game->options.deathmatch ? 100
                                         : c->rerelease              ? 125
                                                                     : 150;
    float kick = c->game->options.deathmatch ? 200 : c->rerelease ? 225 : 250;
    bool lag;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e))
        return lag_end(c, lag, false, e);
    bool result = q2_rail(c, start, dir, damage * damage_factor, kick * kick_factor, 11, 0, e);
    if (!lag_end(c, lag, result, e))
        return false;
    if (!c->rerelease)
        ++c->state->frame;
    return finish(c, 6, start, 1, e);
}
static bool heatbeam(q2_weapon_call *c, qa_error *e) {
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_v3(7, 2, -3), &start, &dir, e))
        return false;
    if (c->rerelease) {
        int ammo;
        if (!q2_ammo(c, &ammo, e))
            return false;
        if (!q2_continues(c) || ammo < 2) {
            c->state->frame = 13;
            c->state->view_skin = 0;
            return q2_loop(c, "", e) && (!q2_continues(c) || q2_no_ammo(c, true, e));
        }
        c->state->frame = c->state->frame > 12 || c->state->frame == 11 ? 8 : c->state->frame + 1;
        c->state->view_skin = 1;
        if (!q2_loop(c, "weapons/bfg__l1a.wav", e) || !q2_power_sound(c, e))
            return false;
    } else {
        ++c->state->frame;
        if (!qa_builtin_resource(&c->game->services, "models/weapons/v_beamer2/tris.md2",
                                 &c->state->view_model, e))
            return false;
    }
    q2_kick(c, qa_v3(0, 0, 0), qa_v3(0, 0, 0), c->rerelease ? 0.2f : 0);
    bool lag;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e))
        return lag_end(c, lag, false, e);
    bool result =
        q2_heatbeam(c, start, dir, 15 * damage_factor,
                   (c->game->options.deathmatch ? 75 : 30) * kick_factor, e);
    return lag_end(c, lag, result, e) && finish(c, 33, start, 2, e) && q2_attack_animation(c, 1, e);
}
static bool etf_rifle(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon_state *s = c->state;
    if (c->rerelease) {
        if (!q2_continues(c)) {
            s->frame = 8;
            return true;
        }
        s->frame = s->frame == 6 ? 7 : 6;
    }
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (ammo < c->definition->quantity) {
        q2_kick(c, qa_v3(0, 0, 0), qa_v3(0, 0, 0), c->rerelease ? 0.2f : 0);
        s->frame = 8;
        return q2_no_ammo(c, true, e);
    }
    qa_vec3 origin, angles;
    origin.x = q2_crandom(c->game) * 0.85f;
    angles.x = q2_crandom(c->game) * 0.85f;
    origin.y = q2_crandom(c->game) * 0.85f;
    angles.y = q2_crandom(c->game) * 0.85f;
    origin.z = q2_crandom(c->game) * 0.85f;
    angles.z = q2_crandom(c->game) * 0.85f;
    q2_kick(c, origin, angles, c->rerelease ? 0.2f : 0);
    qa_vec3 start, dir;
    if (c->rerelease) {
        if (!q2_project(c, qa_vec_add(c->input.angles, angles),
                        qa_v3(15, s->frame == 6 ? 8 : 6, -8), &start, &dir, e))
            return false;
    } else {
        qa_body_state body;
        qa_vec3 right, up;
        if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e))
            return false;
        qa_builtin_angle_vectors(c->input.angles, &dir, &right, &up);
        float side = (float)(s->frame == 6 ? 8 : 6) * (c->input.hand == QA_Q2_LEFT_HAND     ? -1
                                                       : c->input.hand == QA_Q2_CENTER_HAND ? 0
                                                                                            : 1);
        start = qa_vec_add(
            qa_vec_add(qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, c->input.view_height)),
                                  qa_vec_scale(dir, 15)),
                       qa_vec_scale(right, side)),
            qa_vec_scale(up, -8));
    }
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e)) return false;
    if (!q2_projectile_spawn(c, Q2_FLECHETTE, start, dir, 10 * damage_factor, 3 * kick_factor,
                             c->rerelease ? 1150 : 750,
                             0, 0, 8000 / (c->rerelease ? 1150.0f : 750.0f), 42, 0, false, false,
                             e))
        return false;
    if (c->rerelease && !q2_power_sound(c, e))
        return false;
    if (!flash(c, c->rerelease && s->frame == 7 ? 32 : 30, e) || !q2_noise(c, start, e))
        return false;
    if (!c->rerelease)
        ++s->frame;
    return q2_consume(c, c->definition->quantity, c->rerelease, e) && q2_attack_animation(c, 1, e) &&
           q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
bool q2_fire(q2_weapon_call *c, bool buffered, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    q2_weapon_call invocation = *c;
    if (buffered)
        invocation.input.attack = true;
    c = &invocation;
    switch (c->definition->weapon) {
    case QA_Q2_LMCTF_PLASMA:
        return q2_lmctf_plasma_fire(c, e);
    case QA_Q2_GRAPPLE:
    case QA_Q2_LMCTF_HOOK:
        return q2_grapple_fire(c, e);
    case QA_Q2_BLASTER:
        if (!blaster(c, qa_v3(0, 0, 0), c->rerelease || c->game->options.deathmatch ? 15 : 10,
                     false, e))
            return false;
        if (!c->rerelease)
            ++c->state->frame;
        return true;
    case QA_Q2_HYPERBLASTER:
        return hyperblaster(c, e);
    case QA_Q2_MACHINEGUN:
        return machinegun(c, e);
    case QA_Q2_CHAINGUN:
        return chaingun(c, e);
    case QA_Q2_SHOTGUN:
        return shotgun(c, false, e);
    case QA_Q2_SUPERSHOTGUN:
        return shotgun(c, true, e);
    case QA_Q2_RAILGUN:
        return rail(c, e);
    case QA_Q2_HEATBEAM:
        return heatbeam(c, e);
    case QA_Q2_CHAINFIST:
        return q2_fire_chainfist(c, e);
    case QA_Q2_GRENADES:
    case QA_Q2_TRAP:
    case QA_Q2_TESLA:
        return q2_throw(c, false, e);
    case QA_Q2_ETF_RIFLE:
        return etf_rifle(c, e);
    case QA_Q2_GRENADELAUNCHER:
    case QA_Q2_ROCKETLAUNCHER:
    case QA_Q2_BFG:
    case QA_Q2_IONRIPPER:
    case QA_Q2_PHALANX:
    case QA_Q2_DISINTEGRATOR:
    case QA_Q2_PROXLAUNCHER:
        return launch(c, e);
    default:
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 firing weapon");
        return false;
    }
}
