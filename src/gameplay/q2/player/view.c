#include "internal.h"

static qa_q2_blend blend_add(qa_q2_blend b, qa_vec3 color, float alpha) {
    if (alpha <= 0)
        return b;
    float total = b.w + (1 - b.w) * alpha, old = b.w / total;
    return (qa_q2_blend){b.x * old + color.x * (1 - old), b.y * old + color.y * (1 - old),
                         b.z * old + color.z * (1 - old), total};
}
static float kick_ratio(uint64_t until, uint64_t now, float duration, float slack) {
    float remaining = q2_seconds_left(until, now);
    return remaining <= 0                       ? 0
           : slack != 0 && remaining > duration ? (duration + slack - remaining) / slack
                                                : remaining / duration;
}
static float angle_difference(float old, float current) {
    return q2_clamp(qa_builtin_angle_delta(old, current), -45, 45);
}
static bool feedback(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m,
                     const qa_q2_powerups *powers, int *flashes, qa_error *e) {
    q2_client_state *s = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    int flash =
        (s->damage_blood != 0 ? 1 : 0) |
        (s->damage_armor != 0 && !s->info.god && powers->invulnerability_until_ns <= g->now_ns ? 2
                                                                                               : 0);
    if (rr) {
        if (flash) {
            s->flash_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            s->flashes = flash;
        } else if (s->flash_ns < g->now_ns)
            s->flashes = 0;
        flash = s->flashes;
    }
    *flashes = flash;
    float blood = s->damage_blood, armor = s->damage_armor, power = s->damage_power,
          total = blood + armor + power;
    if (total == 0)
        return true;
    if (m->animate_q2 && s->animation_priority < 3) {
        s->animation_priority = 3;
        if (!m->ducked)
            g->player_runtime->pain_animation = (g->player_runtime->pain_animation + 1) % 3;
        s->visual.frame = m->ducked ? 168 : 53 + (int)g->player_runtime->pain_animation * 4;
        s->animation_end = s->visual.frame + 4;
    }
    qa_combat_state combat;
    qa_body_state body;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e) ||
        !qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (g->now_ns > s->pain_ns && !s->info.god && powers->invulnerability_until_ns <= g->now_ns) {
        int severity = combat.health < 25   ? 25
                       : combat.health < 50 ? 50
                       : combat.health < 75 ? 75
                                            : 100;
        char sound[32];
        snprintf(sound, sizeof(sound), "*pain%d_%d.wav", severity, (int)(q2_random(g) * 2) + 1);
        s->pain_ns = q2_deadline(g->now_ns, 700 * Q2_MS);
        if (!q2_player_sound(g, a->id, sound, 2, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (rr && !q2_player_noise(g, a->id, body.origin, false, e))
            return false;
    }
    float count = rr ? (blood != 0 ? fmaxf(total, 10) : fminf(total, 2)) : fmaxf(total, 10);
    if (rr) {
        s->animation_ns = 0;
        s->damage_alpha = fmaxf(0, s->damage_alpha);
        if (blood != 0 || s->damage_alpha + count * .06f < .15f)
            s->damage_alpha = q2_clamp(s->damage_alpha + count * .06f, .06f, .4f);
        s->damage_blend =
            qa_vec_normalize(qa_v3(armor / total + (blood != 0 ? fmaxf(15, blood / total) : 0),
                                   (power + armor) / total, armor / total));
    } else {
        s->damage_alpha = q2_clamp(fmaxf(0, s->damage_alpha) + count * .01f, .2f, .6f);
        s->damage_blend = qa_v3((armor + blood) / total, (power + armor) / total, armor / total);
    }
    if (s->damage_knockback != 0 && combat.health > 0) {
        float kick = q2_clamp(fabsf(s->damage_knockback) * 100 / combat.health, count * .5f, 50);
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(s->damage_from, body.origin)), forward,
                right;
        qa_builtin_angle_vectors(m->view_angles, &forward, &right, NULL);
        s->damage_roll = kick * qa_vec_dot(direction, right) * .3f;
        s->damage_pitch = -kick * qa_vec_dot(direction, forward) * .3f;
        uint64_t duration =
            rr ? (g->frame_ns < 600 * Q2_MS ? 600 * Q2_MS - g->frame_ns : 0) : 500 * Q2_MS;
        s->damage_ns = q2_deadline(g->now_ns, duration);
    }
    s->damage_blood = s->damage_armor = s->damage_power = s->damage_knockback = 0;
    return true;
}
static void animation(qa_q2_game *g, q2_client_state *s, qa_q2_visual *visual,
                      const qa_q2_player_movement *m, float speed) {
    if (!m->animate_q2 || s->gibbed)
        return;
    bool rr = g->options.edition == QA_Q2_RERELEASE, run = speed != 0, duck = m->ducked;
    int priority = rr && s->animation_priority == 6 ? 256 : s->animation_priority;
    bool reverse = rr ? (priority & 256) != 0 : priority == 6;
    bool changed = (duck != s->animation_duck && priority < 5) ||
                   (run != s->animation_run && priority == 0) || (!m->grounded && priority <= 1);
    if (!changed) {
        if (rr && s->animation_ns > g->now_ns)
            return;
        if ((reverse && visual->frame > s->animation_end) ||
            (!reverse && visual->frame < s->animation_end)) {
            visual->frame += reverse ? -1 : 1;
            s->animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return;
        }
        if (priority == 5)
            return;
        if (priority == 2) {
            if (!m->grounded)
                return;
            s->animation_priority = rr && duck ? 257 : 1;
            visual->frame = rr && duck ? 71 : 68;
            s->animation_end = rr && duck ? 69 : 71;
            s->animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return;
        }
    }
    s->animation_priority = 0;
    s->animation_duck = duck;
    s->animation_run = run;
    s->animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    if (!m->grounded && (!rr || !m->grapple_attached)) {
        s->animation_priority = 2;
        if (rr && duck) {
            if (visual->frame != 155)
                visual->frame = 154;
            s->animation_end = 155;
        } else {
            if (visual->frame != 67)
                visual->frame = 66;
            s->animation_end = 67;
        }
    } else if (run && (!rr || m->grounded)) {
        visual->frame = duck ? 154 : 40;
        s->animation_end = duck ? 159 : 45;
    } else {
        visual->frame = duck ? 135 : 0;
        s->animation_end = duck ? 153 : 39;
    }
}
bool q2_player_animate_reference(qa_q2_game *g, qa_actor_id owner, const qa_body_state *body,
                                 qa_q2_visual *visual, qa_error *e) {
    q2_actor *a = q2_client(g, owner, e);
    if (!a)
        return false;
    qa_q2_player_movement movement;
    if (!q2_player_observe(g, a, &movement, e))
        return false;
    if (!q2_actor_live(g, owner))
        return true;
    movement.grounded = body->ground.registry != 0;
    animation(g, a->client, visual, &movement, hypotf(body->velocity.x, body->velocity.y));
    return true;
}
static bool flashing(uint64_t until, uint64_t now) {
    return until > now &&
           (until - now > 3 * Q2_NS || (((until - now + 50 * Q2_MS) / (100 * Q2_MS)) & 4) != 0);
}
bool q2_player_loop(qa_q2_game *g, q2_actor *a, qa_string_id loop, qa_error *e) {
    q2_client_state *s = a->client;
    if (loop == s->loop_sound)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_builtin_event event = {.family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .origin = body.origin,
                              .volume = 1,
                              .attenuation = 1,
                              .time_ns = g->now_ns,
                              .channel = 0};
    qa_string_id previous = s->loop_sound;
    s->loop_sound = loop;
    if (previous) {
        event.kind = QA_BUILTIN_STOP_SOUND;
        event.resource = previous;
        if (!qa_builtin_emit(&g->services, &event, e))
            return false;
    }
    if (!q2_actor_live(g, a->id) || s->loop_sound != loop)
        return true;
    if (loop) {
        event.kind = QA_BUILTIN_SOUND;
        event.resource = loop;
        event.flags = 1;
        return qa_builtin_emit(&g->services, &event, e);
    }
    return true;
}
static bool effects(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m,
                    const qa_q2_powerups *powers, const qa_combat_state *combat,
                    const qa_q2_character_weapon *weapon, qa_error *e) {
    q2_client_state *s = a->client;
    s->visual.effects = 0;
    s->visual.render_flags = g->options.edition == QA_Q2_RERELEASE ? 32768 : 0;
    if (combat->health > 0) {
        if (s->power_armor_ns > g->now_ns) {
            if (combat->armor.powered.kind == QA_POWER_SCREEN)
                s->visual.effects |= 0x200;
            else if (combat->armor.powered.kind == QA_POWER_SHIELD) {
                s->visual.effects |= 0x100;
                s->visual.render_flags |= 0x800;
            }
        }
        if (flashing(powers->quad_until_ns, g->now_ns))
            s->visual.effects |= 0x8000;
        if (flashing(powers->invulnerability_until_ns, g->now_ns))
            s->visual.effects |= 0x10000;
        if (s->info.god) {
            s->visual.effects |= 0x100;
            s->visual.render_flags |= 0x1c00;
        }
    }
    if (s->tracker_ns > g->now_ns) {
        s->visual.effects |= 0x8000000;
        s->visual.render_flags |= 0x800;
    }
    qa_string_id loop = weapon->loop_sound;
    const char *path = m->water_level && (m->water_type & 24) ? "player/fry.wav"
                       : weapon->q2_weapon == QA_Q2_RAILGUN   ? "weapons/rg_hum.wav"
                       : weapon->q2_weapon == QA_Q2_BFG       ? "weapons/bfg_hum.wav"
                                                              : NULL;
    if (path && !qa_builtin_resource(&g->services, path, &loop, e))
        return false;
    return q2_player_loop(g, a, loop, e);
}
bool q2_player_build_view(qa_q2_game *g, q2_actor *a, const qa_q2_player_movement *m, qa_error *e) {
    q2_client_state *s = a->client;
    q2_players *players = g->player_runtime;
    qa_q2_player_rules *r = &players->rules;
    qa_body_state body;
    qa_combat_state combat;
    qa_q2_powerups powers;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !qa_combat_read(g->services.combat, a->id, &combat, e) ||
        !qa_q2_powerups_read(g, a->id, &powers, e))
        return false;
    bool rr = g->options.edition == QA_Q2_RERELEASE, intermission = players->intermission;
    int flashes = 0;
    if (!intermission && !feedback(g, a, m, &powers, &flashes, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_q2_character_weapon weapon = {0};
    if (players->services.weapon_state) {
        if (!players->services.weapon_state(players->services.context, a->id, &weapon, e))
            return false;
    } else if (a->weapon_bound) {
        weapon = (qa_q2_character_weapon){.q2_weapon = a->weapon.weapon,
                                          .ammo = g->ammo[a->weapon.weapon],
                                          .kick_origin = a->weapon.kick_origin,
                                          .kick_angles = a->weapon.kick_angles,
                                          .loop_sound = a->weapon.loop_sound};
    }
    if (!q2_actor_live(g, a->id))
        return true;
    int32_t score;
    if (!q2_player_score_read(g, a->id, &score, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    float dt = (float)((double)g->frame_ns / Q2_NS),
          speed = hypotf(body.velocity.x, body.velocity.y);
    bool duck = m->ducked && (!rr || m->grounded);
    float bob_time = s->bob_time * (duck ? 4 : 1);
    int cycle = (int)fmodf(truncf(bob_time), 2);
    float bob = fabsf(sinf(bob_time * 3.14159265358979323846f)), sign = cycle ? -1 : 1;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(m->view_angles, &forward, &right, &up);
    float fall = kick_ratio(s->fall_ns, g->now_ns, .3f, rr ? .1f - dt : 0),
          damage = kick_ratio(s->damage_ns, g->now_ns, .5f, rr ? .1f - dt : 0);
    if (damage == 0)
        s->damage_pitch = s->damage_roll = 0;
    qa_q2_player_view view = {.angles = m->view_angles,
                              .fov = intermission ? 90 : s->fov,
                              .health = combat.health,
                              .score = score,
                              .selected_item = s->info.selected_item,
                              .spectator = s->info.spectator,
                              .flashes = flashes};
    if (s->info.dead)
        view.angles = qa_v3(-15, s->killer_yaw, 40);
    else if (!rr || !s->bob_skip) {
        float pitch = bob * r->bob_pitch * speed * (duck ? 6 : 1),
              roll = bob * r->bob_roll * speed * (duck ? 6 : 1);
        view.kick_angles =
            qa_v3(weapon.kick_angles.x + damage * s->damage_pitch + fall * s->fall_value +
                      qa_vec_dot(body.velocity, forward) * r->run_pitch +
                      (rr ? fminf(pitch, 1.2f) : pitch),
                  weapon.kick_angles.y,
                  weapon.kick_angles.z + damage * s->damage_roll +
                      qa_vec_dot(body.velocity, right) * r->run_roll +
                      (rr ? fminf(roll, 1.2f) : roll) * sign);
        if (rr && s->quake_ns > g->now_ns) {
            float factor =
                g->now_ns ? fminf(1, (float)((double)s->quake_ns / g->now_ns) * .25f) : 1;
            view.kick_angles.x += q2_crandom(g) * factor;
            view.kick_angles.y += q2_crandom(g) * factor;
            view.kick_angles.z += q2_crandom(g) * factor;
        }
    }
    if (!rr || !s->bob_skip)
        view.offset =
            qa_v3(q2_clamp(weapon.kick_origin.x, -14, 14), q2_clamp(weapon.kick_origin.y, -14, 14),
                  q2_clamp((rr ? 0 : s->info.view_height) - fall * s->fall_value * .4f +
                               fminf(bob * speed * r->bob_up, 6) + weapon.kick_origin.z,
                           -22, 30));
    float yaw = angle_difference(s->old_view_angles.y, view.angles.y);
    view.gun_angles =
        qa_v3(speed * bob * .005f + angle_difference(s->old_view_angles.x, view.angles.x) * .2f,
              speed * bob * .01f * sign + yaw * .2f,
              speed * bob * .005f * sign +
                  angle_difference(s->old_view_angles.z, view.angles.z) * .2f + yaw * .1f);
    if (rr) {
        view.kick_angles =
            qa_v3(q2_clamp(view.kick_angles.x, -31, 31), q2_clamp(view.kick_angles.y, -31, 31),
                  q2_clamp(view.kick_angles.z, -31, 31));
        if ((weapon.q2_weapon == QA_Q2_HEATBEAM || weapon.q2_weapon == QA_Q2_GRAPPLE) &&
            a->weapon_bound && a->weapon.phase == QA_Q2_FIRING)
            view.gun_angles = qa_v3(0, 0, 0);
        else {
            qa_vec3 delta = qa_vec_sub(s->old_view_angles, view.angles), slow = s->slow_view_angles;
            float changes[] = {delta.x, delta.y, delta.z}, values[] = {slow.x, slow.y, slow.z},
                  kicks[3];
            for (size_t i = 0; i < 3; i++) {
                float value = values[i] + changes[i];
                if (value > 180)
                    value -= 360;
                if (value < -180)
                    value += 360;
                value = q2_clamp(value, -45, 45);
                kicks[i] = value;
                float amount = dt * 1000 * (changes[i] != 0 ? .05f : .15f);
                values[i] = value > 0 ? fmaxf(0, value - amount) : fminf(0, value + amount);
            }
            s->slow_view_angles = qa_v3(values[0], values[1], values[2]);
            view.gun_angles = qa_v3(speed * bob * .005f + kicks[0] * .1f,
                                    speed * bob * .01f * sign + kicks[1] * .1f,
                                    -(speed * bob * .005f * sign + kicks[2] * .05f));
        }
    }
    view.gun_offset = qa_vec_add(
        qa_vec_add(qa_vec_scale(forward, r->gun_offset.y), qa_vec_scale(right, r->gun_offset.x)),
        qa_vec_scale(up, -r->gun_offset.z));
    qa_vec3 eye = qa_vec_add(body.origin, view.offset);
    if (rr)
        eye.z += s->info.view_height;
    qa_point_contents contents;
    if (!qa_world_point_contents(
            g->services.world,
            &(qa_point_query){.point = eye,
                              .pass_actor = a->id,
                              .policy = {.family = QA_COLLISION_Q2, .q2_merged_contents = rr}},
            &contents, e))
        return false;
    int mask = rr ? contents.merged : contents.contents;
    if (mask & 9)
        view.blend = blend_add(view.blend, qa_v3(1, .3f, 0), .6f);
    else if (mask & 16)
        view.blend = blend_add(view.blend, qa_v3(0, .1f, .05f), .6f);
    else if (mask & 32)
        view.blend = blend_add(view.blend, qa_v3(.5f, .3f, .2f), .4f);
    static const struct {
        const char *item, *sound;
        qa_vec3 color;
        float alpha;
    } table[] = {{"item_quad", "items/damage2.wav", {0, 0, 1}, .08f},
                 {"item_invulnerability", "items/protect2.wav", {1, 1, 0}, .08f},
                 {"item_enviro", "items/airout.wav", {0, 1, 0}, .08f},
                 {"item_breather", "items/airout.wav", {.4f, 1, .4f}, .04f}};
    uint64_t until[] = {powers.quad_until_ns, powers.invulnerability_until_ns,
                        powers.enviro_until_ns, powers.breather_until_ns};
    for (size_t i = 0; i < 4; i++)
        if (until[i] > g->now_ns) {
            uint64_t remaining = until[i] - g->now_ns;
            if ((rr ? (remaining + Q2_MS / 2) / Q2_MS == 3000
                    : (remaining + 50 * Q2_MS) / (100 * Q2_MS) == 30) &&
                !q2_player_sound(g, a->id, table[i].sound, 3, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (flashing(until[i], g->now_ns))
                view.blend = blend_add(view.blend, table[i].color, table[i].alpha);
            const qa_q2_item_definition *d = qa_q2_item_lookup(g, table[i].item);
            view.timer_item = d ? d->item : 0;
            view.timer_seconds = (int)fmin((double)(remaining / Q2_NS), INT_MAX);
            break;
        }
    view.blend = blend_add(view.blend, s->damage_blend, s->damage_alpha);
    view.blend = blend_add(view.blend, qa_v3(.85f, .7f, .3f), s->bonus_alpha);
    s->damage_alpha = fmaxf(0, s->damage_alpha - (rr ? dt * .6f : .06f));
    s->bonus_alpha = fmaxf(0, s->bonus_alpha - (rr ? dt : .1f));
    if (s->nuke_ns > g->now_ns)
        view.blend = blend_add(
            view.blend, qa_v3(1, 1, 1),
            q2_clamp(q2_seconds_left(s->nuke_ns, g->now_ns) / (s->nuke_inside ? 2 : 1), 0, 1));
    view.underwater = (mask & 56) != 0;
    view.armor = combat.armor.regular.kind == QA_ARMOR_NONE ? 0 : combat.armor.regular.points;
    if (combat.armor.powered.kind != QA_POWER_NONE &&
        (view.armor == 0 || (((g->now_ns + 50 * Q2_MS) / (100 * Q2_MS)) & 8)))
        view.armor = combat.armor.powered.cells;
    if (weapon.q2_weapon != QA_Q2_WEAPON_NONE && weapon.ammo) {
        int ammo;
        if (!q2_count(g, a->id, weapon.ammo, &ammo, e))
            return false;
        view.ammo = (float)ammo;
    }
    view.layouts = (s->show_scores || s->show_help || combat.health <= 0 || intermission ? 1 : 0) |
                   (s->show_inventory && combat.health > 0 ? 2 : 0);
    if (intermission) {
        view.offset = view.kick_angles = qa_v3(0, 0, 0);
        view.blend = (qa_q2_blend){0};
        view.underwater = false;
    }
    if (players->fade_ns)
        view.blend = (qa_q2_blend){
            0, 0, 0, q2_clamp(1 - (q2_seconds_left(players->fade_ns, g->now_ns) - .3f), 0, 1)};
    if (!q2_player_emit(
            g, &(qa_q2_player_event){.kind = QA_Q2_PLAYER_VIEW, .actor = a->id, .view = view}, e))
        return false;
    if (!q2_actor_live(g, a->id) || intermission)
        return true;
    if (!s->event && m->grounded && speed > 225 &&
        (int)truncf(s->bob_time + s->bob_move) !=
            (int)truncf(m->ducked ? s->bob_time * 4 : s->bob_time))
        s->event = 2;
    if (s->event) {
        uint32_t event = s->event;
        s->event = 0;
        if (!qa_builtin_emit(&g->services,
                             &(qa_builtin_event){.kind = QA_BUILTIN_ANIMATION,
                                                 .family = QA_GAME_Q2,
                                                 .provider = g->options.owner,
                                                 .actor = a->id,
                                                 .origin = body.origin,
                                                 .code = (int)event,
                                                 .time_ns = g->now_ns},
                             e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (!effects(g, a, m, &powers, &combat, &weapon, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    animation(g, s, &s->visual, m, speed);
    if (m->animate_q2) {
        float side = qa_vec_dot(body.velocity, right),
              roll = (side < 0 ? -1 : 1) *
                     fminf(fabsf(side) * r->roll_angle / r->roll_speed, r->roll_angle);
        body.angles =
            qa_v3((m->view_angles.x > 180 ? m->view_angles.x - 360 : m->view_angles.x) / 3,
                  m->view_angles.y, roll * 4);
        if (!qa_world_body_write(g->services.world, a->id, &body, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!rr)
            s->visual.alpha = 1;
        if (!q2_publish_visual(g, a->id, &s->visual, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    s->old_velocity = body.velocity;
    s->old_view_angles = view.angles;
    if (s->show_scores && q2_actor_live(g, a->id) &&
        (((g->now_ns + 50 * Q2_MS) / (100 * Q2_MS)) & 31) == 0)
        return q2_player_scoreboard(g, a, false, e);
    return true;
}
