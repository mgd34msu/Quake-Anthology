#include "internal.h"
#include "../entities/internal.h"

static qa_vec4 blend_add(qa_vec4 b, qa_vec3 color, float alpha) {
    if (alpha <= 0)
        return b;
    float total = b.w + (1 - b.w) * alpha, old = b.w / total;
    return (qa_vec4){b.x * old + color.x * (1 - old), b.y * old + color.y * (1 - old),
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
        (s->rule.damage_blood != 0 ? 1 : 0) |
        (s->rule.damage_armor != 0 && !s->info.god && powers->invulnerability_until_ns <= g->now_ns ? 2
                                                                                               : 0);
    if (rr) {
        if (flash) {
            s->rule.flash_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            s->rule.flashes = flash;
        } else if (s->rule.flash_ns < g->now_ns)
            s->rule.flashes = 0;
        flash = s->rule.flashes;
    }
    *flashes = flash;
    float blood = s->rule.damage_blood, armor = s->rule.damage_armor, power = s->rule.damage_power,
          total = blood + armor + power;
    if (total == 0)
        return true;
    if (m->animate_q2 && s->rule.animation_priority < 3) {
        s->rule.animation_priority = 3;
        if (!m->ducked)
            g->player_runtime->pain_animation = (g->player_runtime->pain_animation + 1) % 3;
        s->rule.visual.frame = m->ducked ? 168 : 53 + (int)g->player_runtime->pain_animation * 4;
        s->rule.animation_end = s->rule.visual.frame + 4;
    }
    qa_combat_state combat;
    qa_body_state body;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e) ||
        !qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (g->now_ns > s->rule.pain_ns && !s->info.god && powers->invulnerability_until_ns <= g->now_ns) {
        int severity = combat.health < 25   ? 25
                       : combat.health < 50 ? 50
                       : combat.health < 75 ? 75
                                            : 100;
        char sound[32];
        snprintf(sound, sizeof(sound), "*pain%d_%d.wav", severity, (int)(q2_random(g) * 2) + 1);
        s->rule.pain_ns = q2_deadline(g->now_ns, 700 * Q2_MS);
        if (!q2_player_sound(g, a->id, sound, 2, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (rr && !q2_player_noise(g, a->id, body.origin, false, e))
            return false;
    }
    float count = rr ? (blood != 0 ? fmaxf(total, 10) : fminf(total, 2)) : fmaxf(total, 10);
    if (rr) {
        s->rule.animation_ns = 0;
        s->rule.damage_alpha = fmaxf(0, s->rule.damage_alpha);
        if (blood != 0 || s->rule.damage_alpha + count * .06f < .15f)
            s->rule.damage_alpha = q2_clamp(s->rule.damage_alpha + count * .06f, .06f, .4f);
        s->rule.damage_blend =
            qa_vec_normalize(qa_v3(armor / total + (blood != 0 ? fmaxf(15, blood / total) : 0),
                                   (power + armor) / total, armor / total));
    } else {
        s->rule.damage_alpha = q2_clamp(fmaxf(0, s->rule.damage_alpha) + count * .01f, .2f, .6f);
        s->rule.damage_blend = qa_v3((armor + blood) / total, (power + armor) / total, armor / total);
    }
    if (s->rule.damage_knockback != 0 && combat.health > 0) {
        float kick = q2_clamp(fabsf(s->rule.damage_knockback) * 100 / combat.health, count * .5f, 50);
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(s->rule.damage_from, body.origin)), forward,
                right;
        qa_builtin_angle_vectors(m->view_angles, &forward, &right, NULL);
        s->rule.damage_roll = kick * qa_vec_dot(direction, right) * .3f;
        s->rule.damage_pitch = -kick * qa_vec_dot(direction, forward) * .3f;
        uint64_t duration =
            rr ? (g->frame_ns < 600 * Q2_MS ? 600 * Q2_MS - g->frame_ns : 0) : 500 * Q2_MS;
        s->rule.damage_ns = q2_deadline(g->now_ns, duration);
    }
    s->rule.damage_blood = s->rule.damage_armor = s->rule.damage_power = s->rule.damage_knockback = 0;
    return true;
}
static void animation(qa_q2_game *g, q2_client_state *s, qa_entity_visual *visual,
                      const qa_q2_player_movement *m, float speed) {
    if (!m->animate_q2 || s->rule.gibbed)
        return;
    bool rr = g->options.edition == QA_Q2_RERELEASE, run = speed != 0, duck = m->ducked;
    int priority = rr && s->rule.animation_priority == 6 ? 256 : s->rule.animation_priority;
    bool reverse = rr ? (priority & 256) != 0 : priority == 6;
    bool changed = (duck != s->rule.animation_duck && priority < 5) ||
                   (run != s->rule.animation_run && priority == 0) || (!m->grounded && priority <= 1);
    if (!changed) {
        if (rr && s->rule.animation_ns > g->now_ns)
            return;
        if ((reverse && visual->frame > s->rule.animation_end) ||
            (!reverse && visual->frame < s->rule.animation_end)) {
            visual->frame += reverse ? -1 : 1;
            s->rule.animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return;
        }
        if (priority == 5)
            return;
        if (priority == 2) {
            if (!m->grounded)
                return;
            s->rule.animation_priority = rr && duck ? 257 : 1;
            visual->frame = rr && duck ? 71 : 68;
            s->rule.animation_end = rr && duck ? 69 : 71;
            s->rule.animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return;
        }
    }
    s->rule.animation_priority = 0;
    s->rule.animation_duck = duck;
    s->rule.animation_run = run;
    s->rule.animation_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    if (!m->grounded && (!rr || !m->grapple_attached)) {
        s->rule.animation_priority = 2;
        if (rr && duck) {
            if (visual->frame != 155)
                visual->frame = 154;
            s->rule.animation_end = 155;
        } else {
            if (visual->frame != 67)
                visual->frame = 66;
            s->rule.animation_end = 67;
        }
    } else if (run && (!rr || m->grounded)) {
        visual->frame = duck ? 154 : 40;
        s->rule.animation_end = duck ? 159 : 45;
    } else {
        visual->frame = duck ? 135 : 0;
        s->rule.animation_end = duck ? 153 : 39;
    }
}
bool q2_player_animate_reference(qa_q2_game *g, qa_actor_id owner, const qa_body_state *body,
                                 qa_entity_visual *visual, qa_error *e) {
    q2_actor *a = q2_client(g, owner, e);
    if (!a)
        return false;
    qa_q2_player_movement movement;
    if (!q2_player_observe(g, a, &movement, e))
        return false;
    if (!q2_actor_live(g, owner))
        return true;
    movement.grounded = qa_actor_reference_present(body->ground);
    animation(g, a->client, visual, &movement, hypotf(body->velocity.x, body->velocity.y));
    return true;
}
static bool flashing(uint64_t until, uint64_t now) {
    return until > now &&
           (until - now > 3 * Q2_NS || (((until - now + 50 * Q2_MS) / (100 * Q2_MS)) & 4) != 0);
}
bool q2_player_loop(qa_q2_game *g, q2_actor *a, qa_string_id loop, qa_error *e) {
    if (!q2_actor_live(g, a->id))
        return true;
    q2_client_state *s = a->client;
    if (loop == s->rule.loop_sound)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return !q2_actor_live(g, a->id);
    if (!q2_actor_live(g, a->id))
        return true;
    qa_builtin_event event = {.family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .origin = body.origin,
                              .volume = 1,
                              .attenuation = 1,
                              .time_ns = g->now_ns,
                              .channel = 0};
    qa_string_id previous = s->rule.loop_sound;
    s->rule.loop_sound = loop;
    if (previous) {
        event.kind = QA_BUILTIN_STOP_SOUND;
        event.resource = previous;
        if (!qa_builtin_emit(&g->services, &event, e))
            return false;
    }
    if (!q2_actor_live(g, a->id) || s->rule.loop_sound != loop)
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
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    s->rule.visual.effects = 0;
    s->rule.visual.render_flags = g->options.edition == QA_Q2_RERELEASE ? 32768 : 0;
    if (combat->health > 0) {
        if (s->rule.power_armor_ns > g->now_ns) {
            if (combat->armor.powered.kind == QA_POWER_SCREEN)
                s->rule.visual.effects |= 0x200;
            else if (combat->armor.powered.kind == QA_POWER_SHIELD) {
                s->rule.visual.effects |= 0x100;
                s->rule.visual.render_flags |= 0x800;
            }
        }
        if (flashing(powers->quad_until_ns, g->now_ns))
            s->rule.visual.effects |= 0x8000;
        if (flashing(powers->invulnerability_until_ns, g->now_ns))
            s->rule.visual.effects |= 0x10000;
        if (s->info.god) {
            s->rule.visual.effects |= 0x100;
            s->rule.visual.render_flags |= 0x1c00;
        }
    }
    if (s->rule.tracker_ns > g->now_ns) {
        s->rule.visual.effects |= 0x8000000;
        s->rule.visual.render_flags |= 0x800;
    }
    if (!rr && s->rule.mission_primary != g->entity_runtime->primary_changes) {
        s->rule.mission_primary = g->entity_runtime->primary_changes;
        s->rule.mission_changed = 1;
    }
    if (s->rule.mission_changed && s->rule.mission_changed <= 3 &&
        (rr ? s->rule.mission_time_ns < g->now_ns : (g->wire_frame & 63) == 0)) {
        bool beep = !rr || s->rule.mission_changed == 1;
        if (!rr) ++s->rule.mission_changed;
        if (beep && !q2_entity_sound(g, a, "misc/pc_up.wav", rr ? 0 : 2, 1, 3, 0, e))
            return false;
        if (!q2_actor_live(g, a->id)) return true;
        if (rr) {
            ++s->rule.mission_changed;
            s->rule.mission_time_ns = q2_deadline(g->now_ns, 5 * Q2_NS);
        }
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
    float bob_time = s->rule.bob_time * (duck ? 4 : 1);
    int cycle = (int)fmodf(truncf(bob_time), 2);
    float bob = fabsf(sinf(bob_time * 3.14159265358979323846f)), sign = cycle ? -1 : 1;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(m->view_angles, &forward, &right, &up);
    float fall = kick_ratio(s->rule.fall_ns, g->now_ns, .3f, rr ? .1f - dt : 0),
          damage = kick_ratio(s->rule.damage_ns, g->now_ns, .5f, rr ? .1f - dt : 0);
    if (damage == 0)
        s->rule.damage_pitch = s->rule.damage_roll = 0;
    qa_q2_player_view view = {.angles = m->view_angles,
                              .fov = intermission ? 90 : s->rule.fov,
                              .health = combat.health,
                              .score = score,
                              .hit_marker_damage = rr ? s->rule.hit_marker_damage : 0,
                              .selected_item = s->info.selected_item,
                              .spectator = s->info.spectator,
                              .flashes = flashes};
    if (s->info.dead)
        view.angles = qa_v3(-15, s->rule.killer_yaw, 40);
    else if (!rr || !s->rule.bob_skip) {
        float pitch = bob * r->bob_pitch * speed * (duck ? 6 : 1),
              roll = bob * r->bob_roll * speed * (duck ? 6 : 1);
        view.kick_angles =
            qa_v3(weapon.kick_angles.x + damage * s->rule.damage_pitch + fall * s->rule.fall_value +
                      qa_vec_dot(body.velocity, forward) * r->run_pitch +
                      (rr ? fminf(pitch, 1.2f) : pitch),
                  weapon.kick_angles.y,
                  weapon.kick_angles.z + damage * s->rule.damage_roll +
                      qa_vec_dot(body.velocity, right) * r->run_roll +
                      (rr ? fminf(roll, 1.2f) : roll) * sign);
        if (rr && s->rule.quake_ns > g->now_ns) {
            float factor =
                g->now_ns ? fminf(1, (float)((double)s->rule.quake_ns / (double)g->now_ns) * .25f) : 1;
            view.kick_angles.x += q2_crandom(g) * factor;
            view.kick_angles.y += q2_crandom(g) * factor;
            view.kick_angles.z += q2_crandom(g) * factor;
        }
    }
    if (!rr || !s->rule.bob_skip)
        view.offset =
            qa_v3(q2_clamp(weapon.kick_origin.x, -14, 14), q2_clamp(weapon.kick_origin.y, -14, 14),
                  q2_clamp((rr ? 0 : s->info.view_height) - fall * s->rule.fall_value * .4f +
                               fminf(bob * speed * r->bob_up, 6) + weapon.kick_origin.z,
                           -22, 30));
    float yaw = angle_difference(s->rule.old_view_angles.y, view.angles.y);
    view.gun_angles =
        qa_v3(speed * bob * .005f + angle_difference(s->rule.old_view_angles.x, view.angles.x) * .2f,
              speed * bob * .01f * sign + yaw * .2f,
              speed * bob * .005f * sign +
                  angle_difference(s->rule.old_view_angles.z, view.angles.z) * .2f + yaw * .1f);
    if (rr) {
        view.kick_angles =
            qa_v3(q2_clamp(view.kick_angles.x, -31, 31), q2_clamp(view.kick_angles.y, -31, 31),
                  q2_clamp(view.kick_angles.z, -31, 31));
        if ((weapon.q2_weapon == QA_Q2_HEATBEAM || weapon.q2_weapon == QA_Q2_GRAPPLE) &&
            a->weapon_bound && a->weapon.phase == QA_Q2_FIRING)
            view.gun_angles = qa_v3(0, 0, 0);
        else {
            qa_vec3 delta = qa_vec_sub(s->rule.old_view_angles, view.angles), slow = s->rule.slow_view_angles;
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
            s->rule.slow_view_angles = qa_v3(values[0], values[1], values[2]);
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
    int mask = rr ? qa_collision_point_contents_export(contents.merged, QA_COLLISION_Q2, contents.q1_opaque_token) : qa_collision_point_contents_export(contents.contents, QA_COLLISION_Q2, contents.q1_opaque_token);
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
    view.blend = blend_add(view.blend, s->rule.damage_blend, s->rule.damage_alpha);
    view.blend = blend_add(view.blend, qa_v3(.85f, .7f, .3f), s->rule.bonus_alpha);
    s->rule.damage_alpha = fmaxf(0, s->rule.damage_alpha - (rr ? dt * .6f : .06f));
    s->rule.bonus_alpha = fmaxf(0, s->rule.bonus_alpha - (rr ? dt : .1f));
    if (s->rule.nuke_ns > g->now_ns)
        view.blend = blend_add(
            view.blend, qa_v3(1, 1, 1),
            q2_clamp(q2_seconds_left(s->rule.nuke_ns, g->now_ns) / (s->rule.nuke_inside ? 2 : 1), 0, 1));
    view.underwater = (mask & 56) != 0;
    view.armor = combat.armor.regular.kind == QA_ARMOR_NONE ? 0 : combat.armor.regular.points;
    bool powered_armor = combat.armor.powered.kind != QA_POWER_NONE &&
        (view.armor == 0 || (rr ? g->now_ns % (3 * Q2_NS) < 1500 * Q2_MS : (g->wire_frame & 8) != 0));
    const char *armor_icon = NULL;
    if (powered_armor) {
        view.armor = combat.armor.powered.cells;
        armor_icon = rr && combat.armor.powered.kind == QA_POWER_SCREEN ? "i_powerscreen" : "i_powershield";
    } else if (view.armor > 0 && combat.armor.regular.item) {
        const qa_q2_item_definition *armor = q2_item_by_id(g, combat.armor.regular.item);
        if (armor && armor->kind == QA_Q2_ITEM_ARMOR) armor_icon = armor->icon;
    }
    if (armor_icon && !qa_builtin_resource(&g->services, armor_icon, &view.armor_icon, e)) return false;
    if (!q2_actor_live(g, a->id)) return true;
    if (weapon.q2_weapon != QA_Q2_WEAPON_NONE && weapon.ammo) {
        const qa_q2_item_definition *definition = q2_item_by_id(g, weapon.ammo);
        int ammo;
        if (!q2_count(g, a->id, weapon.ammo, &ammo, e))
            return false;
        if (!(rr && (g->options.deathmatch_flags & 8192) && definition && definition->infinite_quantity)) {
            view.ammo = (float)ammo; view.ammo_count = ammo;
            if (definition && definition->icon &&
                !qa_builtin_resource(&g->services, definition->icon, &view.ammo_icon, e)) return false;
        }
    }
    if (!q2_actor_live(g, a->id)) return true;
    if (g->now_ns <= a->pickup_until_ns) {
        view.pickup_icon = a->pickup_icon;
        view.pickup_text = a->pickup_text;
    }
    if (rr && view.selected_item && g->now_ns <= a->selected_item_name_until_ns)
        view.selected_item_name = a->selected_item_name;
    const char *help_icon = NULL;
    if (s->rule.mission_changed &&
        (rr ? s->rule.mission_changed <= 2 && g->now_ns % Q2_NS < 500 * Q2_MS
            : (g->wire_frame & 8) != 0))
        help_icon = "i_help";
    else if ((s->rule.hand == QA_Q2_CENTER_HAND || (!rr && view.fov > 91)) &&
             weapon.q2_weapon != QA_Q2_WEAPON_NONE) {
        const qa_q2_item_definition *definition = q2_item_by_id(g, g->items[weapon.q2_weapon]);
        if (definition) help_icon = definition->icon;
    }
    if (help_icon && !qa_builtin_resource(&g->services, help_icon, &view.help_icon, e))
        return false;
    if (!q2_actor_live(g, a->id)) return true;
    if (rr && !g->options.deathmatch) {
        /* The built-in catalogs contain 15 keys across all Q2 products. */
        const qa_q2_item_definition *keys[15];
        size_t keys_held = 0;
        for (size_t i = 0; i < qa_q2_item_count(g); ++i) {
            const qa_q2_item_definition *definition = qa_q2_item_at(g, i);
            if (definition->kind != QA_Q2_ITEM_KEY) continue;
            int owned;
            if (!q2_count(g, a->id, definition->item, &owned, e)) return false;
            if (!q2_actor_live(g, a->id)) return true;
            if (owned) keys[keys_held++] = definition;
        }
        size_t offset = keys_held > 3 ? (size_t)((g->now_ns / (5 * Q2_NS)) % keys_held) : 0;
        for (size_t i = 0; i < keys_held && i < 3; ++i) {
            const qa_q2_item_definition *definition = keys[(i + offset) % keys_held];
            if (!qa_builtin_resource(&g->services, definition->icon, &view.key_icons[i], e))
                return false;
            if (!q2_actor_live(g, a->id)) return true;
        }
    }
    view.layouts = (s->rule.show_scores || s->rule.show_help || combat.health <= 0 || intermission ? 1 : 0) |
                   (s->rule.show_inventory && combat.health > 0 ? 2 : 0);
    if (intermission) {
        view.offset = view.kick_angles = qa_v3(0, 0, 0);
        view.blend = (qa_vec4){0};
        view.underwater = false;
    }
    if (players->fade_ns)
        view.blend = (qa_vec4){
            0, 0, 0, q2_clamp(1 - (q2_seconds_left(players->fade_ns, g->now_ns) - .3f), 0, 1)};
    a->wire_view = (qa_q2_wire_view){.view = view, .frame = g->wire_frame,
        .time_ns = g->now_ns, .present = true};
    if (!q2_player_emit(
            g, &(qa_q2_player_event){.kind = QA_Q2_PLAYER_VIEW, .actor = a->id, .view = view}, e))
        return false;
    if (!q2_actor_live(g, a->id) || intermission)
        return true;
    if (!s->rule.event && m->grounded && speed > 225 &&
        (int)truncf(s->rule.bob_time + s->rule.bob_move) !=
            (int)truncf(m->ducked ? s->rule.bob_time * 4 : s->rule.bob_time))
        s->rule.event = 2;
    if (s->rule.event) {
        uint32_t event = s->rule.event;
        a->wire_event = event;
        a->wire_event_frame = g->wire_frame;
        s->rule.event = 0;
        if (!qa_builtin_emit(&g->services,
                             &(qa_builtin_event){.kind = QA_BUILTIN_Q2_ENTITY_EVENT,
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
    animation(g, s, &s->rule.visual, m, speed);
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
            s->rule.visual.alpha = 1;
        if (!q2_publish_visual(g, a->id, &s->rule.visual, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    s->rule.old_velocity = body.velocity;
    s->rule.old_view_angles = view.angles;
    if (s->rule.show_scores && q2_actor_live(g, a->id) &&
        (((g->now_ns + 50 * Q2_MS) / (100 * Q2_MS)) & 31) == 0)
        return q2_player_scoreboard(g, a, false, e);
    return true;
}
