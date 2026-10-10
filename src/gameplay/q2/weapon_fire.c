#include "qa/q2_sound.h"
#include "internal.h"

bool q2_weapon_shot_spec(const q2_weapon_call *c, q2_shot_spec *out) {
    const qa_q2_weapon w = c->definition->weapon;
    const bool dm = c->game->options.deathmatch;
    *out = (q2_shot_spec){.offset = {8, 8, -8}, .shots = 1};
    switch (w) {
    case QA_Q2_BLASTER:
    case QA_Q2_HYPERBLASTER:
        out->kind = Q2_BOLT;
        out->offset = qa_v3(24, 8, -8);
        out->damage = w == QA_Q2_BLASTER ? (c->rerelease || dm ? 15 : 10) : (dm ? 15 : 20);
        out->kick = 1;
        out->speed = c->rerelease && w == QA_Q2_BLASTER ? 1500 : 1000;
        out->fuse = 2;
        out->mod = w == QA_Q2_BLASTER ? 1 : 10;
        break;
    case QA_Q2_SHOTGUN:
    case QA_Q2_SUPERSHOTGUN:
        out->offset = qa_v3(0, c->rerelease ? 0 : 8, -8);
        out->damage = w == QA_Q2_SHOTGUN ? 4 : 6;
        out->kick = w == QA_Q2_SHOTGUN ? 8 : 12;
        out->spread_x = w == QA_Q2_SHOTGUN ? 500 : 1000;
        out->spread_y = 500;
        out->shots = w == QA_Q2_SHOTGUN ? 12 : 20;
        out->spread_degrees_x = w == QA_Q2_SUPERSHOTGUN ? 5 : 0;
        out->range = q2_hitscan_range();
        break;
    case QA_Q2_MACHINEGUN:
    case QA_Q2_CHAINGUN:
        out->offset = qa_v3(0, c->rerelease ? 0 : w == QA_Q2_MACHINEGUN ? 8 : 7, -8);
        out->damage = w == QA_Q2_CHAINGUN && dm ? 6 : 8;
        out->kick = 2;
        out->spread_x = 300;
        out->spread_y = 500;
        out->range = q2_hitscan_range();
        if (w == QA_Q2_CHAINGUN)
            out->shots = c->state->frame <= 9 ? 1 : c->state->frame <= 14 ?
                (q2_continues(c) ? 2 : 1) : 3;
        break;
    case QA_Q2_GRENADELAUNCHER:
        out->kind = Q2_GRENADE;
        if (c->rerelease) out->offset.y = 0;
        out->damage = 120;
        out->speed = 600;
        out->radius = 160;
        out->splash = out->damage;
        out->fuse = 2.5f;
        out->mod = 6;
        out->splash_mod = 7;
        out->muzzle = 8;
        out->ballistic = true;
        break;
    case QA_Q2_ROCKETLAUNCHER:
        out->kind = Q2_ROCKET;
        out->damage = 100;
        out->damage_random = 20;
        out->speed = 650;
        out->radius = 120;
        out->splash = 120;
        out->fuse = 8000 / out->speed;
        out->mod = 8;
        out->splash_mod = 9;
        out->muzzle = 7;
        break;
    case QA_Q2_IONRIPPER:
        out->offset = qa_v3(16, 7, -8);
        out->kind = Q2_ION;
        out->damage = dm ? 30 : 50;
        out->speed = 500;
        out->fuse = 3;
        out->spread_degrees_x = 1;
        out->mod = 34;
        out->muzzle = 16;
        break;
    case QA_Q2_PHALANX:
        out->offset = qa_v3(0, 8, -8);
        out->kind = Q2_PLASMA;
        out->damage = 70;
        out->damage_random = 10;
        out->yaw_offset = c->state->frame == 8 ? -1.5f : 1.5f;
        out->speed = 725;
        out->radius = 120;
        out->splash = c->state->frame == 8 ? 30 : 120;
        out->fuse = 8000 / out->speed;
        out->mod = out->splash_mod = 35;
        out->muzzle = c->state->frame == 8 ? 20 : 18;
        break;
    case QA_Q2_DISINTEGRATOR:
        out->kind = Q2_TRACKER;
        out->damage = c->rerelease ? (dm ? 45 : 135) : (dm ? 30 : 45);
        out->speed = 1000;
        out->fuse = 10;
        out->mod = 51;
        out->offset = qa_v3(24, 8, -8);
        out->muzzle = 35;
        out->homing = true;
        out->conditional = true;
        break;
    case QA_Q2_PROXLAUNCHER:
        out->kind = Q2_PROX;
        out->damage = out->splash = 90;
        out->speed = 600;
        out->radius = 192;
        out->fuse = 45;
        out->mod = out->splash_mod = 46;
        out->muzzle = c->rerelease ? 31 : 6;
        if (c->rerelease) out->offset.y = 0;
        out->ballistic = out->deployable = out->conditional = true;
        break;
    case QA_Q2_BFG:
        out->kind = Q2_BFG_BALL;
        out->damage = dm ? 200 : 500;
        out->radius = 1000;
        out->speed = 400;
        out->fuse = 20;
        out->mod = 13;
        out->splash_mod = 14;
        out->muzzle = 19;
        break;
    case QA_Q2_RAILGUN:
        out->offset = qa_v3(0, 7, -8);
        out->damage = dm ? 100 : c->rerelease ? 125 : 150;
        out->kick = dm ? 200 : c->rerelease ? 225 : 250;
        out->range = q2_hitscan_range();
        break;
    case QA_Q2_HEATBEAM:
        out->offset = qa_v3(7, 2, -3);
        out->damage = 15;
        out->kick = dm ? 75 : 30;
        out->range = q2_hitscan_range();
        break;
    case QA_Q2_ETF_RIFLE:
        out->kind = Q2_FLECHETTE;
        out->offset = qa_v3(15, c->state->frame == 6 ? 8 : 6, -8);
        out->damage = 10;
        out->kick = 3;
        out->speed = c->rerelease ? 1150 : 750;
        out->fuse = 8000 / out->speed;
        out->mod = 42;
        break;
    case QA_Q2_GRENADES:
    case QA_Q2_TRAP:
    case QA_Q2_TESLA:
        return false; /* The throwing recipe requires actual remaining fuse and health. */
    case QA_Q2_CHAINFIST:
        q2_chainfist_spec(c->game, out);
        break;
    case QA_Q2_GRAPPLE:
    case QA_Q2_LMCTF_HOOK:
        q2_grapple_spec(c->game, w == QA_Q2_LMCTF_HOOK, out);
        break;
    case QA_Q2_LMCTF_PLASMA:
        q2_lmctf_plasma_spec(c->actor->lmctf_plasma_bounce, out);
        break;
    default:
        return false;
    }
    if (out->speed > 0 && out->range == 0 && out->fuse > 0 &&
        !out->ballistic && !out->grapple && !out->homing)
        out->range = out->speed * out->fuse;
    return true;
}

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
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, qa_vec_add(spec.offset, offset), &start, &dir, e))
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
                               spec.speed, 0, 0, spec.fuse, spec.mod, 0,
                               false, false, e) &&
           flash(c, hyper ? 14 : 0, e) && q2_noise(c, start, e) &&
           q2_weapon_fired(c->game, c->actor->id, c->definition->weapon, e);
}
qa_vec3 q2_hyper_offset(const q2_weapon_call *c) {
    float rotation = (float)(c->state->frame - 5) * 6.283185307179586f / 6;
    return c->rerelease ? qa_v3(-4 * sinf(rotation), 4 * cosf(rotation), 0) :
                         qa_v3(-4 * sinf(rotation), 0, 4 * cosf(rotation));
}

bool q2_weapon_muzzle(q2_weapon_call *c, const q2_shot_spec *spec, qa_vec3 angles,
                       qa_vec3 *start, qa_vec3 *direction, qa_error *error) {
    qa_q2_weapon weapon = c->definition->weapon;
    if (spec->grapple)
        return q2_grapple_project(c, weapon == QA_Q2_LMCTF_HOOK, start, direction, error);
    qa_vec3 offset = spec->offset;
    if (weapon == QA_Q2_HYPERBLASTER) offset = qa_vec_add(offset, q2_hyper_offset(c));
    qa_vec3 projected = angles;
    if (weapon == QA_Q2_PHALANX) {
        projected.y += spec->yaw_offset;
        if (!q2_project(c, c->rerelease ? projected : angles, offset, start, direction, error)) return false;
        if (!c->rerelease) qa_builtin_angle_vectors(projected, direction, NULL, NULL);
        return true;
    }
    if (!c->rerelease && (weapon == QA_Q2_ETF_RIFLE || weapon == QA_Q2_TESLA)) {
        qa_body_state body;
        qa_vec3 forward, right, up;
        if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, error)) return false;
        qa_builtin_angle_vectors(angles, &forward, &right, &up);
        float side = c->input.hand == QA_Q2_LEFT_HAND ? -offset.y :
            c->input.hand == QA_Q2_CENTER_HAND ? 0 : offset.y;
        if (weapon == QA_Q2_TESLA) {
            side = c->input.hand == QA_Q2_LEFT_HAND ? 4 : c->input.hand == QA_Q2_CENTER_HAND ? 0 : -4;
            *start = qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(right, side)),
                               qa_vec_scale(up, c->input.view_height - 22));
        } else *start = qa_vec_add(qa_vec_add(qa_vec_add(qa_vec_add(body.origin,
            qa_v3(0, 0, c->input.view_height)), qa_vec_scale(forward, offset.x)),
            qa_vec_scale(right, side)), qa_vec_scale(up, offset.z));
        *direction = forward;
        return true;
    }
    return q2_project(c, angles, offset, start, direction, error);
}

static bool hyperblaster(q2_weapon_call *c, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    qa_q2_weapon_state *s = c->state;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (c->rerelease) {
        s->frame = s->frame > 20 ? 6 : s->frame + 1;
        if (s->frame == 12) {
            if (ammo > 0 && q2_continues(c))
                s->frame = 6;
            else if (!q2_sound(c, QA_Q2_SOUND_WEAPONS_HYPRBD1A, 0, 1, e))
                return false;
        }
        if (!q2_loop(c, s->frame >= 6 && s->frame <= 11 ? QA_Q2_SOUND_WEAPONS_HYPRBL1A : "", e))
            return false;
        if (q2_continues(c) && s->frame >= 6 && s->frame <= 11) {
            if (ammo < 1)
                return q2_no_ammo(c, true, e);
            if (!blaster(c, q2_hyper_offset(c),
                         spec.damage, true, e) ||
                !q2_power_sound(c, e) || !q2_consume(c, 1, true, e))
                return false;
            return q2_attack_animation(c, (int)(q2_random(c->game) + 0.25f), e);
        }
        return true;
    }
    if (!q2_loop(c, QA_Q2_SOUND_WEAPONS_HYPRBL1A, e))
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
            if (!blaster(c, q2_hyper_offset(c),
                         spec.damage, true, e) ||
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
    return s->frame != 12 || (q2_sound(c, QA_Q2_SOUND_WEAPONS_HYPRBD1A, 0, 1, e) && q2_loop(c, "", e));
}
static bool machinegun(q2_weapon_call *c, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
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
                    spec.offset, &start, &dir, e))
        return false;
    bool lag;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e))
        return lag_end(c, lag, false, e);
    bool result = q2_bullet(c, start, dir, spec.damage * damage_factor, spec.kick * kick_factor, spec.spread_x, spec.spread_y, 1, 4, false, e);
    if (!lag_end(c, lag, result, e) || (c->rerelease && !q2_power_sound(c, e)) ||
        !finish(c, 1, start, 1, e))
        return false;
    return q2_attack_animation(c, (int)(q2_random(c->game) + 0.25f), e);
}
static bool chaingun(q2_weapon_call *c, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    qa_q2_weapon_state *s = c->state;
    int ammo;
    if (!q2_ammo(c, &ammo, e))
        return false;
    if (c->rerelease && s->frame > 31) {
        s->frame = 5;
        if (!q2_sound(c, QA_Q2_SOUND_WEAPONS_CHNGNU1A, 0, 2, e))
            return false;
    } else {
        if (!c->rerelease && s->frame == 5 && !q2_sound(c, QA_Q2_SOUND_WEAPONS_CHNGNU1A, 0, 2, e))
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
        if (!q2_loop(c, "", e) || !q2_sound(c, QA_Q2_SOUND_WEAPONS_CHNGND1A, 0, 2, e))
            return false;
    } else if (!c->rerelease && !q2_loop(c, QA_Q2_SOUND_WEAPONS_CHNGNL1A, e))
        return false;
    if (c->rerelease && (s->frame < 5 || s->frame > 21))
        return true;
    if (c->rerelease && !q2_loop(c, QA_Q2_SOUND_WEAPONS_CHNGNL1A, e))
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
                 q2_bullet(c, start, dir, spec.damage * damage_factor,
                           spec.kick * kick_factor, spec.spread_x, spec.spread_y, 1, 5, false, e);
    }
    return lag_end(c, lag, result, e) && (!c->rerelease || q2_power_sound(c, e)) &&
           finish(c, 3 + shots - 1, start, shots, e);
}
static bool shotgun(q2_weapon_call *c, bool super, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    if (!super && c->state->frame == 9) {
        if (!c->rerelease)
            ++c->state->frame;
        return true;
    }
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, spec.offset, &start, &dir, e))
        return false;
    q2_kick(c, qa_vec_scale(forward(c), -2), qa_v3(-2, 0, 0), c->rerelease ? 0.2f : 0);
    bool lag, result = true;
    if (!lag_begin(c, start, dir, &lag, e))
        return false;
    if (super)
        for (int i = 0; i < 2 && result && q2_actor_live(c->game, c->actor->id); ++i) {
            qa_vec3 angles = c->input.angles;
            angles.y += i == 0 ? -spec.spread_degrees_x : spec.spread_degrees_x;
            if (c->rerelease)
                result = q2_project(c, angles, qa_v3(0, 0, -8), &start, &dir, e);
            else
                qa_builtin_angle_vectors(angles, &dir, NULL, NULL);
            if (result) {
                float damage_factor, kick_factor;
                result = q2_multiplier(c, &damage_factor, e) && q2_multiplier(c, &kick_factor, e) &&
                    q2_bullet(c, start, dir, spec.damage * damage_factor, spec.kick * kick_factor, spec.spread_x, spec.spread_y, (int)(spec.shots / 2), 3, true, e);
            }
        }
    else {
        float damage_factor, kick_factor;
        result = q2_multiplier(c, &damage_factor, e) && q2_multiplier(c, &kick_factor, e) &&
            q2_bullet(c, start, dir, spec.damage * damage_factor, spec.kick * kick_factor, spec.spread_x, spec.spread_y, (int)spec.shots, 2, true, e);
    }
    if (!lag_end(c, lag, result, e))
        return false;
    if (!c->rerelease)
        ++c->state->frame;
    return finish(c, super ? 13 : 2, start, c->definition->quantity, e);
}
static bool launch(q2_weapon_call *c, qa_error *e) {
    qa_q2_weapon w = c->definition->weapon;
    float m = 1;
    if (w == QA_Q2_PHALANX && !q2_multiplier(c, &m, e)) return false;
    q2_shot_spec spec;
    if (!q2_weapon_shot_spec(c, &spec)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Weapon does not launch a Q2 projectile");
        return false;
    }
    qa_vec3 start, dir, angles = c->input.angles, offset = spec.offset;
    float damage = spec.damage, splash = spec.splash;
    if (w == QA_Q2_GRENADELAUNCHER || w == QA_Q2_PROXLAUNCHER) {
        if (c->rerelease) angles.x = q2_launch_pitch(angles.x);
    } else if (w == QA_Q2_ROCKETLAUNCHER)
        damage += floorf(q2_random(c->game) * spec.damage_random);
    else if (w == QA_Q2_IONRIPPER)
        angles.y += q2_crandom(c->game) * spec.spread_degrees_x;
    else if (w == QA_Q2_PHALANX) {
        angles.y += spec.yaw_offset;
        damage = (damage + floorf(q2_random(c->game) * spec.damage_random)) * m;
        if (c->state->frame != 8) splash *= m;
    } else if (w == QA_Q2_BFG) {
        if (c->state->frame == 9) {
            if (!c->rerelease) ++c->state->frame;
            qa_body_state body;
            if (!qa_world_body_read(c->game->services.world, c->actor->id, &body, e)) return false;
            return flash(c, 12, e) && q2_noise(c, c->rerelease ? body.origin : qa_v3(0, 0, 0), e);
        }
        int ammo;
        if (!q2_ammo(c, &ammo, e)) return false;
        if (ammo < c->definition->quantity) {
            if (!c->rerelease) ++c->state->frame;
            return true;
        }
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
    if (!q2_projectile_spawn(c, spec.kind, start, dir, damage, spec.kick, spec.speed, spec.radius, splash, spec.fuse, spec.mod,
                             spec.splash_mod, false, false, e))
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
    return finish(c, spec.muzzle, start, c->definition->quantity, e);
}
static bool rail(q2_weapon_call *c, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, spec.offset, &start, &dir, e))
        return false;
    q2_kick(c, qa_vec_scale(forward(c), -3), qa_v3(-3, 0, 0), c->rerelease ? 0.2f : 0);
    float damage = spec.damage, kick = spec.kick;
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
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
    qa_vec3 start, dir;
    if (!q2_project(c, c->input.angles, spec.offset, &start, &dir, e))
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
        if (!q2_loop(c, QA_Q2_SOUND_WEAPONS_BFG__L1A, e) || !q2_power_sound(c, e))
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
        q2_heatbeam(c, start, dir, spec.damage * damage_factor,
                   spec.kick * kick_factor, e);
    return lag_end(c, lag, result, e) && finish(c, 33, start, 2, e) && q2_attack_animation(c, 1, e);
}
static bool etf_rifle(q2_weapon_call *c, qa_error *e) {
    q2_shot_spec spec;
    (void)q2_weapon_shot_spec(c, &spec);
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
        if (!q2_weapon_muzzle(c, &spec, c->input.angles, &start, &dir, e)) return false;
    }
    float damage_factor, kick_factor;
    if (!q2_multiplier(c, &damage_factor, e) || !q2_multiplier(c, &kick_factor, e)) return false;
    if (!q2_projectile_spawn(c, Q2_FLECHETTE, start, dir, spec.damage * damage_factor, spec.kick * kick_factor,
                             spec.speed, 0, 0, spec.fuse, spec.mod, 0, false, false,
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
    case QA_Q2_BLASTER: {
        q2_shot_spec spec;
        (void)q2_weapon_shot_spec(c, &spec);
        if (!blaster(c, qa_v3(0, 0, 0), spec.damage, false, e))
            return false;
        if (!c->rerelease)
            ++c->state->frame;
        return true;
    }
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
