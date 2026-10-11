#include "qa/q2_sound.h"
#include "internal.h"
#include "qa/game_q2_items.h"
#include "qa/game_q2_player.h"
#include "qa/game_q2_source.h"

static bool run(q2_weapon_call *, qa_error *);
static q2_actor *weapon_actor(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (a != NULL && !a->weapon_bound) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Actor has no Q2 arsenal");
        return NULL;
    }
    return a;
}
bool qa_q2_weapon_bind(qa_q2_game *g, qa_actor_id id, qa_q2_weapon weapon, qa_error *e) {
    if (weapon != QA_Q2_WEAPON_NONE && qa_q2_weapon_definition_at(g, weapon) == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 weapon is absent from this arsenal");
        return false;
    }
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    if (a->weapon_bound) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 arsenal already bound");
        return false;
    }
    a->weapon = (qa_q2_weapon_state){
        .weapon = weapon, .phase = QA_Q2_ACTIVATING, .gun_rate = 10, .kick_seconds = 0.2f};
    a->weapon_bound = true;
    return true;
}
bool qa_q2_weapon_read(qa_q2_game *g, qa_actor_id id, qa_q2_weapon_state *out, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || out == NULL)
        return false;
    *out = a->weapon;
    return true;
}
bool q2_weapon_validate(qa_q2_game *g, const qa_q2_weapon_state *s, qa_error *e) {
    if (s == NULL || (unsigned)s->phase > QA_Q2_DROPPING ||
        (unsigned)s->handoff > QA_Q2_PRIMARY_HOLSTERED || s->frame < 0 || s->frame > 255 ||
        s->machinegun_shots < 0 || s->machinegun_shots > 9 ||
        (unsigned)s->hand_reservation > QA_Q2_HAND_INFINITE || !qa_vec_finite(s->kick_origin) ||
        !qa_vec_finite(s->kick_angles) || !isfinite(s->gun_rate) || s->gun_rate <= 0 ||
        !isfinite(s->kick_seconds) || s->kick_seconds < 0) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 weapon checkpoint");
        return false;
    }
    qa_q2_weapon weapons[] = {s->weapon, s->last_weapon, s->pending};
    for (size_t i = 0; i < 3; ++i)
        if (weapons[i] != QA_Q2_WEAPON_NONE && qa_q2_weapon_definition_at(g, weapons[i]) == NULL) {
            qa_error_set(e, QA_ERROR_FORMAT, 0,
                         "Checkpoint weapon is unavailable in selected arsenal");
            return false;
        }
    return true;
}
bool qa_q2_weapon_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_state *s, qa_error *e) {
    if (!q2_weapon_validate(g, s, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->weapon = *s;
    a->weapon_bound = true;
    return true;
}
bool qa_q2_weapon_select(qa_q2_game *g, qa_actor_id id, qa_q2_weapon weapon, bool allow_empty,
                         qa_q2_selection *out, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || out == NULL)
        return false;
    if (g->options.product == QA_Q2_XATRIX && g->options.edition == QA_Q2_CLASSIC) {
        qa_q2_weapon alternative = weapon == QA_Q2_HYPERBLASTER ? QA_Q2_IONRIPPER
                                   : weapon == QA_Q2_RAILGUN    ? QA_Q2_PHALANX
                                                                : QA_Q2_WEAPON_NONE;
        int owned = 0;
        if (alternative != QA_Q2_WEAPON_NONE) {
            if (!q2_count(g, id, g->items[alternative], &owned, e))
                return false;
            if (owned > 0 && a->weapon.weapon == weapon)
                weapon = alternative;
            else if (owned > 0 && alternative == QA_Q2_PHALANX) {
                int slugs, mags;
                if (!q2_count(g, id, g->ammo[QA_Q2_RAILGUN], &slugs, e) ||
                    !q2_count(g, id, g->ammo[QA_Q2_PHALANX], &mags, e))
                    return false;
                if (slugs == 0 && mags > 0)
                    weapon = alternative;
            }
        }
    }
    const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(g, weapon);
    if (d == NULL) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 weapon is unavailable");
        return false;
    }
    if (a->weapon.weapon == weapon) {
        *out = QA_Q2_CURRENT;
        if (weapon != QA_Q2_LMCTF_PLASMA)
            return true;
        a->lmctf_plasma_bounce = !a->lmctf_plasma_bounce;
        return q2_lmctf_plasma_mode(g, a, e);
    }
    int count;
    if (!q2_count(g, id, g->items[weapon], &count, e))
        return false;
    if (count < 1) {
        *out = QA_Q2_NOT_OWNED;
        return true;
    }
    if (!allow_empty) {
        float selected;
        if (!qa_q2_source_value(g, QA_Q2_SOURCE_SELECT_EMPTY, 0, &selected, e)) return false;
        if (!q2_actor_live(g, id)) { *out = QA_Q2_NOT_OWNED; return true; }
        allow_empty = selected != 0;
    }
    if (!allow_empty && g->ammo[weapon] != 0 && g->ammo[weapon] != g->items[weapon]) {
        if (!q2_count(g, id, g->ammo[weapon], &count, e))
            return false;
        if (count < d->quantity) {
            *out = count <= 0 ? QA_Q2_NO_AMMO : QA_Q2_INSUFFICIENT_AMMO;
            return true;
        }
    }
    a->weapon.pending = weapon;
    *out = QA_Q2_SELECTED;
    return true;
}
bool qa_q2_weapon_can_drop(qa_q2_game *g, qa_actor_id id, qa_q2_weapon weapon, bool *out,
                           qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || out == NULL || qa_q2_weapon_definition_at(g, weapon) == NULL)
        return false;
    int count;
    if (!q2_count(g, id, g->items[weapon], &count, e))
        return false;
    *out = (g->options.deathmatch_flags & 4u) == 0 && count > 0 &&
           !((a->weapon.weapon == weapon || a->weapon.pending == weapon) && count == 1);
    return true;
}
bool qa_q2_weapon_holster(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL)
        return false;
    if (a->weapon.handoff == QA_Q2_PRIMARY_ACTIVE) {
        a->weapon.handoff = a->weapon.weapon == QA_Q2_WEAPON_NONE ? QA_Q2_PRIMARY_HOLSTERED
                                                                  : QA_Q2_PRIMARY_HOLSTERING;
        a->weapon.pending = QA_Q2_WEAPON_NONE;
    }
    return true;
}
bool qa_q2_weapon_silencer(qa_q2_game *g, qa_actor_id id, int charges, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || charges < 0 || charges > INT_MAX - a->silencer)
        return false;
    a->silencer += charges;
    return true;
}
static bool context(qa_q2_game *g, q2_actor *a, const qa_q2_weapon_input *in, uint64_t now,
                    uint64_t frame, q2_weapon_call *out) {
    *out = (q2_weapon_call){.game = g,
                            .actor = a,
                            .state = &a->weapon,
                            .input = *in,
                            .definition = qa_q2_weapon_definition_at(g, a->weapon.weapon),
                            .now_ns = now,
                            .frame_ns = frame,
                            .rerelease = g->options.edition == QA_Q2_RERELEASE,
                            .silenced = a->silencer > 0};
    return out->definition != NULL;
}
bool q2_no_ammo(q2_weapon_call *c, bool sound, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (sound && c->now_ns >= c->state->empty_sound_ns) {
        if (!q2_sound(c, QA_Q2_SOUND_WEAPONS_NOAMMO, c->rerelease ? 1 : 2, 1, e))
            return false;
        c->state->empty_sound_ns = q2_deadline(c->now_ns, Q2_NS);
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
    }
    static const qa_q2_weapon base[] = {QA_Q2_RAILGUN,    QA_Q2_HYPERBLASTER, QA_Q2_CHAINGUN,
                                        QA_Q2_MACHINEGUN, QA_Q2_SUPERSHOTGUN, QA_Q2_SHOTGUN,
                                        QA_Q2_BLASTER};
    static const qa_q2_weapon rogue[] = {QA_Q2_RAILGUN,  QA_Q2_HEATBEAM,   QA_Q2_ETF_RIFLE,
                                         QA_Q2_CHAINGUN, QA_Q2_MACHINEGUN, QA_Q2_SUPERSHOTGUN,
                                         QA_Q2_SHOTGUN,  QA_Q2_BLASTER};
    static const qa_q2_weapon rerelease[] = {
        QA_Q2_DISINTEGRATOR,   QA_Q2_RAILGUN,      QA_Q2_HEATBEAM,  QA_Q2_IONRIPPER,
        QA_Q2_HYPERBLASTER,    QA_Q2_ETF_RIFLE,    QA_Q2_CHAINGUN,  QA_Q2_MACHINEGUN,
        QA_Q2_SUPERSHOTGUN,    QA_Q2_SHOTGUN,      QA_Q2_PHALANX,   QA_Q2_ROCKETLAUNCHER,
        QA_Q2_GRENADELAUNCHER, QA_Q2_PROXLAUNCHER, QA_Q2_CHAINFIST, QA_Q2_BLASTER};
    const qa_q2_weapon *order = base;
    size_t fallback_count = sizeof(base) / sizeof(*base);
    if (c->rerelease) {
        order = rerelease;
        fallback_count = sizeof(rerelease) / sizeof(*rerelease);
    } else if (c->game->options.product == QA_Q2_ROGUE) {
        order = rogue;
        fallback_count = sizeof(rogue) / sizeof(*rogue);
    }
    for (size_t i = 0; i < fallback_count; ++i) {
        qa_q2_weapon w = order[i];
        const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(c->game, w);
        if (d == NULL)
            continue;
        int count;
        if (w != QA_Q2_BLASTER) {
            if (!q2_count(c->game, c->actor->id, c->game->items[w], &count, e))
                return false;
            if (count <= 0)
                continue;
        }
        if (!q2_count(c->game, c->actor->id, c->game->ammo[w], &count, e))
            return false;
        if (count >= d->quantity) {
            c->state->pending = w;
            return true;
        }
    }
    return true;
}
bool q2_change_weapon(q2_weapon_call *c, qa_error *e) {
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    qa_q2_weapon_state *s = c->state;
    if (c->equipment) {
        s->handoff = QA_Q2_PRIMARY_HOLSTERED;
        s->pending = QA_Q2_WEAPON_NONE;
        s->latched_attack = s->fire_buffered = false;
        return true;
    }
    qa_combat_state combat;
    if (!qa_combat_read(c->game->services.combat, c->actor->id, &combat, e))
        return false;
    if (s->handoff == QA_Q2_PRIMARY_ACTIVE && c->rerelease && combat.health > 0 &&
        !c->input.instant_switch && c->input.holster)
        return true;
    if (s->grenade_ns != 0 && (s->hand_reservation != QA_Q2_HAND_UNRESERVED || c->rerelease ||
                               (s->handoff == QA_Q2_PRIMARY_HOLSTERING &&
                                (s->weapon == QA_Q2_TRAP || s->weapon == QA_Q2_TESLA)))) {
        if (!c->rerelease)
            s->grenade_ns = c->now_ns;
        if (!q2_throw(c, false, e))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
    }
    bool refund = s->hand_reservation == QA_Q2_HAND_FINITE && s->grenade_ns == 0;
    s->hand_reservation = QA_Q2_HAND_UNRESERVED;
    if (refund) {
        qa_inventory_entry entry;
        qa_item_id item = c->game->ammo[QA_Q2_GRENADES];
        if (!qa_inventory_entry_read(c->game->services.inventory, c->actor->id, item, &entry, e))
            return false;
        entry.count += 1;
        if (!qa_inventory_configure(c->game->services.inventory, c->actor->id, &entry, NULL, NULL,
                                    e))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
        if (c->game->hooks.ammo_changed != NULL &&
            !c->game->hooks.ammo_changed(c->game->hooks.context, c->actor->id, item, e))
            return false;
        if (!q2_actor_live(c->game, c->actor->id))
            return true;
    }
    s->grenade_ns = 0;
    if (!qa_combat_read(c->game->services.combat, c->actor->id, &combat, e))
        return false;
    if (s->handoff == QA_Q2_PRIMARY_HOLSTERING && combat.health > 0) {
        s->handoff = QA_Q2_PRIMARY_HOLSTERED;
        s->pending = QA_Q2_WEAPON_NONE;
        s->latched_attack = s->fire_buffered = false;
        return q2_loop(c, "", e);
    }
    s->handoff = QA_Q2_PRIMARY_ACTIVE;
    if (s->weapon != QA_Q2_WEAPON_NONE && s->pending != QA_Q2_WEAPON_NONE &&
        s->pending != s->weapon && c->rerelease)
        if (!q2_sound(c, QA_Q2_SOUND_WEAPONS_CHANGE, 1, 1, e))
            return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    s->last_weapon = s->weapon;
    s->weapon = s->pending;
    s->pending = QA_Q2_WEAPON_NONE;
    s->machinegun_shots = 0;
    s->view_model = 0;
    s->view_skin = 0;
    c->definition = qa_q2_weapon_definition_at(c->game, s->weapon);
    if (!q2_loop(c, "", e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id))
        return true;
    if (c->definition == NULL)
        return true;
    s->phase = QA_Q2_ACTIVATING;
    s->frame = 0;
    if (!q2_animation(c, 1, c->input.ducked ? 169 : 62, c->input.ducked ? 172 : 65, e))
        return false;
    if (c->rerelease && c->input.instant_switch)
        return run(c, e);
    return true;
}
bool qa_q2_weapon_resume(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_input *in,
                         qa_q2_weapon weapon, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || in == NULL)
        return false;
    if (a->weapon.handoff == QA_Q2_PRIMARY_ACTIVE)
        return true;
    if (a->weapon.handoff != QA_Q2_PRIMARY_HOLSTERED) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 weapon must finish holstering before resumption");
        return false;
    }
    q2_weapon_call c;
    context(g, a, in, g->now_ns, g->frame_ns, &c);
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    a->weapon.pending = QA_Q2_WEAPON_NONE;
    if (combat.health > 0) {
        if (weapon == QA_Q2_WEAPON_NONE)
            weapon = a->weapon.weapon;
        const qa_q2_weapon_definition *d = qa_q2_weapon_definition_at(g, weapon);
        int owned = 0, ammo = 0;
        if (d != NULL && (!q2_count(g, id, g->items[weapon], &owned, e) ||
                          !q2_count(g, id, g->ammo[weapon], &ammo, e)))
            return false;
        if (d != NULL && (weapon == QA_Q2_BLASTER || owned > 0) && ammo >= d->quantity)
            a->weapon.pending = weapon;
        else if (!q2_no_ammo(&c, false, e))
            return false;
    }
    return q2_change_weapon(&c, e);
}
static bool chainfist_smoke(q2_weapon_call *c, qa_error *e) {
    if ((c->state->frame == 42 || c->state->frame == 51) && (int)(q2_random(c->game) * 8) != 0 &&
        c->input.hand != QA_Q2_CENTER_HAND && q2_random(c->game) < 0.4f) {
        qa_vec3 start, dir;
        if (!q2_project(c, c->input.angles, qa_v3(8, 8, -4), &start, &dir, e))
            return false;
        return q2_projectile_event(c->game, c->actor->id, QA_BUILTIN_IMPACT, c->game->runtime_names[Q2_NAME_RESOURCE_Q2_CHAINFIST_SMOKE],
                                   0, start, qa_v3(0, 0, 0), e);
    }
    return true;
}
static bool run(q2_weapon_call *c, qa_error *e) {
    c->definition = qa_q2_weapon_definition_at(c->game, c->state->weapon);
    if (c->definition == NULL || c->state->handoff == QA_Q2_PRIMARY_HOLSTERED)
        return true;
    if (c->rerelease)
        c->silenced = c->actor->silencer > 0;
    qa_q2_weapon w = c->state->weapon;
    qa_q2_weapon_state *s = c->state;
    if (w == QA_Q2_GRAPPLE || w == QA_Q2_LMCTF_HOOK)
        return q2_grapple_weapon(c, e);
    if (w == QA_Q2_LMCTF_PLASMA)
        return q2_lmctf_plasma_weapon(c, e);
    if (w == QA_Q2_GRENADES || w == QA_Q2_TRAP || w == QA_Q2_TESLA) {
        if (w == QA_Q2_TESLA && !c->rerelease) {
            s->view_model = 0;
            if (s->frame > 1 && s->frame < 9 &&
                !qa_builtin_resource(&c->game->services, "models/weapons/v_tesla2/tris.md2",
                                     &s->view_model, e))
                return false;
        }
        return q2_throw_frame(c, e);
    }
    int last_sequence = 0;
    if (!c->rerelease) {
        if (w == QA_Q2_CHAINFIST) {
            if (s->frame == 13 || s->frame == 23)
                s->frame = 32;
            else if (!chainfist_smoke(c, e))
                return false;
            if (!q2_loop(c,
                         s->phase == QA_Q2_FIRING     ? QA_Q2_SOUND_WEAPONS_SAWHIT
                         : s->phase == QA_Q2_DROPPING ? ""
                                                      : QA_Q2_SOUND_WEAPONS_SAWIDLE,
                         e))
                return false;
        } else if (w == QA_Q2_ETF_RIFLE && s->phase == QA_Q2_FIRING) {
            int ammo;
            if (!q2_ammo(c, &ammo, e))
                return false;
            if (ammo <= 0)
                s->frame = 8;
        } else if (w == QA_Q2_HEATBEAM) {
            s->view_model = 0;
            if (s->phase == QA_Q2_FIRING) {
                if (!q2_loop(c, QA_Q2_SOUND_WEAPONS_BFG__L1A, e))
                    return false;
                int ammo;
                if (!q2_ammo(c, &ammo, e))
                    return false;
                if (ammo >= 2 && q2_continues(c)) {
                    if (s->frame >= 13)
                        s->frame = 9;
                    if (!qa_builtin_resource(&c->game->services,
                                             "models/weapons/v_beamer2/tris.md2", &s->view_model,
                                             e))
                        return false;
                } else
                    s->frame = 13;
            } else if (!q2_loop(c, "", e))
                return false;
        }
    }
    if (!q2_generic(c, e))
        return false;
    if (!q2_actor_live(c->game, c->actor->id) || s->handoff == QA_Q2_PRIMARY_HOLSTERED)
        return true;
    if (w == QA_Q2_CHAINFIST) {
        if (c->rerelease)
            return chainfist_smoke(c, e) &&
                   q2_loop(c,
                           s->phase == QA_Q2_FIRING     ? QA_Q2_SOUND_WEAPONS_SAWHIT
                           : s->phase == QA_Q2_DROPPING ? ""
                                                        : QA_Q2_SOUND_WEAPONS_SAWIDLE,
                           e);
        if (q2_continues(c) && (s->frame == 13 || s->frame == 23 || s->frame == 32)) {
            last_sequence = s->frame;
            s->frame = 6;
        }
        if (s->frame == 6) {
            float chance = q2_random(c->game);
            if (last_sequence == 13)
                chance -= 0.34f;
            else if (last_sequence == 23)
                chance += 0.33f;
            else if (last_sequence == 32 && chance >= 0.33f)
                chance += 0.34f;
            if (chance < 0.33f)
                s->frame = 14;
            else if (chance < 0.66f)
                s->frame = 24;
        }
    } else if (!c->rerelease && w == QA_Q2_ETF_RIFLE && s->frame == 8 && q2_continues(c))
        s->frame = 6;
    return true;
}
bool q2_weapon_powerups(q2_weapon_call *c, qa_error *e) {
    if (!c->equipment && c->game->hooks.source_weapon_powerups != NULL) {
        qa_builtin_powerups powers;
        bool handled = false;
        qa_actor_id actor = c->actor->id;
        if (!c->game->hooks.source_weapon_powerups(c->game->hooks.context, actor,
                                                   &powers, &handled, e)) return false;
        if (q2_actor_get(c->game, actor, false, e) != c->actor) return false;
        if (handled) {
            c->input.quad_until_ns = powers.quad_until_ns;
            c->input.double_until_ns = powers.double_until_ns;
            c->input.quad_fire_until_ns = powers.quad_fire_until_ns;
            return true;
        }
    }
    if (c->game->services.powerups != NULL) {
        qa_builtin_powerups powers;
        if (!c->game->services.powerups(c->game->services.context,
                c->game->options.owner, c->actor->id, &powers, e))
            return false;
        c->input.quad_until_ns = powers.quad_until_ns;
        c->input.double_until_ns = powers.double_until_ns;
        c->input.quad_fire_until_ns = powers.quad_fire_until_ns;
        return true;
    }
    qa_q2_powerups powers;
    if (!qa_q2_powerups_read(c->game, c->actor->id, &powers, e))
        return false;
    if (powers.quad_until_ns > c->input.quad_until_ns)
        c->input.quad_until_ns = powers.quad_until_ns;
    if (powers.double_until_ns > c->input.double_until_ns)
        c->input.double_until_ns = powers.double_until_ns;
    if (powers.quad_fire_until_ns > c->input.quad_fire_until_ns)
        c->input.quad_fire_until_ns = powers.quad_fire_until_ns;
    return true;
}
bool qa_q2_weapon_controls_read(qa_q2_game *g, qa_actor_id id, qa_q2_weapon_input *out,
                                 qa_error *e) {
    if (!g || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 weapon control observation");
        return false;
    }
    q2_actor *a = weapon_actor(g, id, e);
    if (!a)
        return false;
    *out = a->input;
    return true;
}
bool qa_q2_weapon_controls(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_input *in,
                            qa_error *e) {
    if (!g || !in || !qa_vec_finite(in->angles) || !isfinite(in->gravity) ||
        !isfinite(in->view_height) || (unsigned)in->hand > QA_Q2_CENTER_HAND ||
        (unsigned)in->source_rules > QA_Q2_WEAPON_RULES_LMCTF) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 weapon controls");
        return false;
    }
    q2_actor *a = weapon_actor(g, id, e);
    if (!a)
        return false;
    a->input = *in;
    a->weapon.latched_attack |= in->latched_attack;
    return true;
}
static bool weapon_tick(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_input *in, uint64_t now,
                        uint64_t frame, qa_error *e) {
    q2_actor *a = weapon_actor(g, id, e);
    if (a == NULL || in == NULL || !qa_vec_finite(in->angles) || !isfinite(in->gravity) ||
        !isfinite(in->view_height) || (unsigned)in->hand > QA_Q2_CENTER_HAND ||
        (unsigned)in->source_rules > QA_Q2_WEAPON_RULES_LMCTF || frame == 0)
        return false;
    if (qa_q2_player_controlled(g, id))
        return !q2_actor_live(g, id) || qa_q2_clear_input(g, id, e);
    g->now_ns = now;
    g->frame_ns = frame;
    q2_weapon_call c;
    context(g, a, in, now, frame, &c);
    if (!q2_weapon_powerups(&c, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    a->input = c.input;
    a->weapon.latched_attack |= in->latched_attack;
    if (in->spectator)
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    if (combat.health < 1) {
        if (a->weapon.grenade_ns != 0 && (a->weapon.weapon == QA_Q2_GRENADES
                                              ? a->weapon.hand_reservation != QA_Q2_HAND_UNRESERVED
                                              : c.rerelease)) {
            if (!c.rerelease)
                a->weapon.grenade_ns = now;
            if (!q2_throw(&c, c.rerelease, e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
        }
        a->weapon.pending = QA_Q2_WEAPON_NONE;
        if (!q2_change_weapon(&c, e))
            return false;
    } else if (a->weapon.handoff != QA_Q2_PRIMARY_HOLSTERED) {
        if (a->weapon.weapon == QA_Q2_WEAPON_NONE) {
            if (a->weapon.pending != QA_Q2_WEAPON_NONE && !q2_change_weapon(&c, e))
                return false;
        } else {
            if (!run(&c, e))
                return false;
            if (!q2_actor_live(g, id))
                return true;
            if (c.input.source_rules == QA_Q2_WEAPON_RULES_LMCTF) {
                if (c.input.haste) {
                    if (a->weapon.source_firing && !q2_sound(&c, QA_Q2_SOUND_PLAYER_LAVA1, 3, 1, e))
                        return false;
                    if (a->weapon.frame != 0 && !run(&c, e))
                        return false;
                } else if (c.input.rune_damage && a->weapon.source_firing &&
                           !q2_sound(&c, QA_Q2_SOUND_CTF_STRENGTH, 3, 1, e))
                    return false;
                if (!q2_actor_live(g, id))
                    return true;
            }
            if (c.rerelease && frame > 33 * Q2_MS && c.definition != NULL) {
                uint64_t interval;
                if (!q2_animation_time(&c, &interval, e)) return false;
                if (interval && interval < frame) {
                    uint64_t end = q2_deadline(now, frame);
                    uint64_t remaining = end > a->weapon.think_ns ? end - a->weapon.think_ns : 0;
                    while (remaining > 0) {
                        a->weapon.think_ns =
                            a->weapon.think_ns > interval ? a->weapon.think_ns - interval : 0;
                        a->weapon.fire_finished_ns = a->weapon.fire_finished_ns > interval
                                                         ? a->weapon.fire_finished_ns - interval
                                                         : 0;
                        if (!run(&c, e))
                            return false;
                        if (!q2_actor_live(g, id))
                            return true;
                        remaining = remaining > interval ? remaining - interval : 0;
                    }
                }
            } else if (!c.rerelease && c.input.quad_fire_until_ns > now && !run(&c, e))
                return false;
        }
    }
    return !q2_actor_live(g, id) || q2_present(&c, e);
}
typedef struct weapon_tick_call {
    qa_q2_game *game;
    const qa_q2_weapon_input *input;
    uint64_t now_ns, frame_ns;
} weapon_tick_call;
static bool run_weapon_tick(void *context, qa_actor_id id, qa_error *e) {
    weapon_tick_call *call = context;
    return weapon_tick(call->game, id, call->input, call->now_ns, call->frame_ns, e);
}
bool qa_q2_weapon_tick(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_input *in, uint64_t now,
                       uint64_t frame, qa_error *e) {
    weapon_tick_call call = {.game = g, .input = in, .now_ns = now, .frame_ns = frame};
    return qa_q2_run_actor(g, id, run_weapon_tick, &call, e);
}

typedef struct weapon_turn_call {
    qa_q2_game *game;
    qa_q2_weapon_input input;
    uint64_t now_ns, frame_ns;
    bool early;
} weapon_turn_call;
static void reconcile_firing_credit(q2_actor *a) {
    if (a->weapon_turn.firing_weapon != a->weapon.weapon) {
        a->weapon_turn.firing_weapon = QA_Q2_WEAPON_NONE;
        a->weapon_turn.firing_credit = 0;
    }
}
static bool prepare_firing_credit(qa_q2_game *g, q2_actor *a,
    const qa_q2_weapon_input *input, qa_error *e) {
    reconcile_firing_credit(a);
    if (g->options.edition != QA_Q2_CLASSIC || input->spectator ||
        a->weapon.phase != QA_Q2_FIRING || a->weapon.weapon == QA_Q2_WEAPON_NONE) return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, a->id, &combat, e)) return false;
    if (combat.health <= 0) return true;
    qa_actor_id actor = a->id;
    qa_q2_weapon weapon = a->weapon.weapon;
    q2_weapon_call c;
    context(g, a, input, g->now_ns, g->frame_ns, &c);
    uint64_t interval;
    if (!q2_interval(&c, g->frame_ns, &interval, e)) return false;
    if (q2_actor_get(g, actor, false, e) != a || !a->weapon_bound || a->weapon.weapon != weapon) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Selected Q2 cadence replaced its actual firing owner");
        return false;
    }
    if (interval != g->frame_ns) {
        double credit = a->weapon_turn.firing_credit + (double)g->frame_ns / (double)interval - 1;
        if (!isfinite(credit)) {
            qa_error_set(e, QA_ERROR_FORMAT, 0, "Selected Q2 cadence produced nonfinite firing credit");
            return false;
        }
        a->weapon_turn.firing_weapon = weapon;
        a->weapon_turn.firing_credit = credit;
    }
    return true;
}
static bool finish_firing_credit(qa_q2_game *g, q2_actor *a, qa_actor_id actor, qa_error *e) {
    qa_q2_weapon_turn_state *turn = &a->weapon_turn;
    qa_q2_weapon weapon = turn->firing_weapon;
    while (weapon != QA_Q2_WEAPON_NONE && turn->firing_credit >= 1 &&
        q2_actor_live(g, actor) && g->actors[actor.slot] == a && a->weapon_bound &&
        a->weapon.weapon == weapon && a->weapon.phase == QA_Q2_FIRING) {
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, actor, &combat, e)) return false;
        if (combat.health <= 0) break;
        turn->firing_credit -= 1;
        qa_q2_weapon_input input = a->input;
        if (g->hooks.selected_weapon_input &&
            !g->hooks.selected_weapon_input(g->hooks.context, actor, &input, e)) return false;
        if (!q2_actor_live(g, actor) || g->actors[actor.slot] != a) return true;
        input.latched_attack = input.weapon_thunk = false;
        if (input.spectator) break;
        if (!weapon_tick(g, actor, &input, g->now_ns, g->frame_ns, e)) return false;
    }
    if (q2_actor_live(g, actor) && g->actors[actor.slot] == a) {
        reconcile_firing_credit(a);
        if (turn->firing_weapon != QA_Q2_WEAPON_NONE) turn->firing_credit = fmod(turn->firing_credit, 1);
    }
    return true;
}
bool qa_q2_weapon_turn_read(qa_q2_game *g, qa_actor_id actor,
    qa_q2_weapon_turn_state *out, qa_error *e) {
    q2_actor *a = out ? weapon_actor(g, actor, e) : NULL;
    if (!a) return false;
    *out = a->weapon_turn;
    if (a->client) {
        out->attack = (a->client->rule.buttons & 1u) != 0;
        out->latched_attack = (a->client->rule.latched_buttons & 1u) != 0;
        out->weapon_thunk = a->client->rule.weapon_thunk;
    }
    if (out->firing_weapon != a->weapon.weapon) {
        out->firing_weapon = QA_Q2_WEAPON_NONE;
        out->firing_credit = 0;
    }
    return true;
}
static bool run_weapon_turn(void *context, qa_actor_id id, qa_error *e) {
    weapon_turn_call *call = context;
    qa_q2_game *g = call->game;
    qa_q2_weapon_input input = call->input;
    input.latched_attack = false;
    input.weapon_thunk = false;
    if (!qa_q2_weapon_controls(g, id, &input, e))
        return false;
    q2_actor *a = weapon_actor(g, id, e);
    bool controlled = qa_q2_player_controlled(g, id);
    if (!q2_actor_live(g, id))
        return true;
    if (controlled)
        return qa_q2_clear_input(g, id, e);
    if (!call->early) {
        g->now_ns = call->now_ns;
        g->frame_ns = call->frame_ns;
    }
    if (!call->early && !prepare_firing_credit(g, a, &input, e)) return false;
    if (a->client) {
        bool ok = call->early ? q2_client_early_weapon_turn(g, a, &input, e)
                             : q2_client_weapon_frame(g, a, &input, e);
        if (!ok || !q2_actor_live(g, id) || g->actors[id.slot] != a) return ok;
        if (call->early) { reconcile_firing_credit(a); return true; }
        return finish_firing_credit(g, a, id, e);
    }
    qa_q2_weapon_turn_state *turn = &a->weapon_turn;
    if (call->early) {
        turn->latched_attack |= input.attack && !turn->attack;
        turn->attack = input.attack;
        if (input.spectator || !turn->latched_attack || turn->weapon_thunk)
            return true;
        turn->weapon_thunk = true;
        input.latched_attack = input.weapon_thunk = true;
        bool ok = weapon_tick(g, id, &input, g->now_ns, g->frame_ns, e);
        if (ok && q2_actor_live(g, id) && g->actors[id.slot] == a) reconcile_firing_credit(a);
        return ok;
    }
    if (!input.spectator && !turn->weapon_thunk) {
        input.latched_attack = turn->latched_attack;
        if (!weapon_tick(g, id, &input, g->now_ns, g->frame_ns, e))
            return false;
    } else
        turn->weapon_thunk = false;
    if (!q2_actor_live(g, id) || g->actors[id.slot] != a)
        return true;
    if (!finish_firing_credit(g, a, id, e)) return false;
    if (!q2_actor_live(g, id) || g->actors[id.slot] != a) return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, id, &combat, e))
        return false;
    if (q2_actor_live(g, id) && combat.health > 0)
        turn->latched_attack = false;
    return true;
}
bool qa_q2_weapon_early_turn(qa_q2_game *g, qa_actor_id id,
                             const qa_q2_weapon_input *input, qa_error *e) {
    if (!g || !input || !g->frame_ns) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 early weapon turn requires actual source timing");
        return false;
    }
    weapon_turn_call call = {.game = g, .input = *input, .early = true};
    return qa_q2_run_actor(g, id, run_weapon_turn, &call, e);
}
bool qa_q2_weapon_frame(qa_q2_game *g, qa_actor_id id, const qa_q2_weapon_input *input,
                         uint64_t now_ns, uint64_t frame_ns, qa_error *e) {
    if (!g || !input || !frame_ns) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 weapon frame requires actual source timing");
        return false;
    }
    weapon_turn_call call = {.game = g, .input = *input, .now_ns = now_ns, .frame_ns = frame_ns};
    return qa_q2_run_actor(g, id, run_weapon_turn, &call, e);
}
