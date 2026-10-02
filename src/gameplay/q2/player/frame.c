#include "internal.h"

static bool weapon_selected(qa_q2_game *g, q2_actor *a) {
    qa_q2_player_services *services = &g->player_runtime->services;
    return services->weapon_selected ? services->weapon_selected(services->context, a->id)
                                     : a->client->use_weapons;
}
static bool weapon_turn(qa_q2_game *g, q2_actor *a, bool latched, qa_error *e) {
    qa_q2_player_services *services = &g->player_runtime->services;
    qa_q2_weapon_input input = a->input;
    if (services->weapon_input && !services->weapon_input(services->context, a->id, &input, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    input.latched_attack = latched;
    input.spectator = a->client->info.spectator;
    input.hand = a->client->hand;
    input.notarget = a->client->info.notarget;
    input.view_height = a->client->info.view_height;
    input.weapon_thunk = a->client->weapon_thunk;
    return qa_q2_weapon_tick(g, a->id, &input, g->now_ns, g->frame_ns, e);
}
bool q2_client_early_weapon_turn(qa_q2_game *g, q2_actor *a,
                                 const qa_q2_weapon_input *input, qa_error *e) {
    q2_client_state *s = a->client;
    if (s->corpse || !s->info.connected)
        return true;
    s->latched_buttons |= input->attack && !(s->buttons & 1) ? 1u : 0u;
    s->buttons = (s->buttons & ~1u) | (input->attack ? 1u : 0u);
    if (g->player_runtime->intermission || input->spectator || s->info.spectator)
        return true;
    bool selected = weapon_selected(g, a);
    if (!q2_actor_live(g, a->id) || !selected || !(s->latched_buttons & 1) || s->weapon_thunk)
        return true;
    s->weapon_thunk = true;
    qa_q2_weapon_input early = *input;
    early.latched_attack = early.weapon_thunk = true;
    return qa_q2_weapon_tick(g, a->id, &early, g->now_ns, g->frame_ns, e);
}
bool q2_client_weapon_frame(qa_q2_game *g, q2_actor *a,
                             const qa_q2_weapon_input *input, qa_error *e) {
    q2_client_state *s = a->client;
    if (s->corpse || !s->info.connected || g->player_runtime->intermission)
        return true;
    bool selected = weapon_selected(g, a);
    if (!q2_actor_live(g, a->id))
        return true;
    if (selected && !input->spectator && !s->info.spectator && !s->weapon_thunk) {
        qa_q2_weapon_input turn = *input;
        turn.latched_attack = (s->latched_buttons & 1) != 0;
        turn.weapon_thunk = false;
        if (!qa_q2_weapon_tick(g, a->id, &turn, g->now_ns, g->frame_ns, e))
            return false;
    } else
        s->weapon_thunk = false;
    if (q2_actor_live(g, a->id) && !s->info.dead)
        s->latched_buttons &= ~1u;
    return true;
}
bool q2_client_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->projectile.kind != Q2_PROJECTILE_NONE || !a->client || a->client->corpse ||
        !a->client->info.connected || g->player_runtime->intermission)
        return true;
    if (qa_q2_player_controlled(g, a->id))
        return !q2_actor_live(g, a->id) || qa_q2_clear_input(g, a->id, e);
    q2_client_state *s = a->client;
    q2_players *p = g->player_runtime;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (!s->character_configured && rr && s->awaiting_respawn)
        return (g->now_ns / Q2_MS) % 500 == 0 ? qa_q2_player_spawn(g, a->id, true, NULL, e) : true;
    if (!s->character_configured && g->options.deathmatch &&
        s->requested_spectator != s->info.spectator &&
        g->now_ns >= q2_deadline(s->respawn_ns, 5 * Q2_NS)) {
        qa_q2_connection_result result;
        if (!qa_q2_player_connect(g, s->userinfo, s->bot, &result, e))
            return false;
        if (!result.allowed) {
            s->requested_spectator = s->info.spectator;
            char text[144];
            snprintf(text, sizeof(text), "%s\n", result.reason);
            if (!q2_player_print(g, a->id, 2, text, e))
                return false;
            snprintf(text, sizeof(text), "spectator %d\n", s->info.spectator ? 1 : 0);
            return !q2_actor_live(g, a->id) ||
                   q2_player_emit(g,
                                  &(qa_q2_player_event){
                                      .kind = QA_Q2_PLAYER_STUFFTEXT, .actor = a->id, .text = text},
                                  e);
        }
        s->info.score = 0;
        if (!qa_q2_player_spawn(g, a->id, true, NULL, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->respawn_ns = g->now_ns;
        char text[128];
        snprintf(text, sizeof(text), "%s %s\n", s->info.name,
                 s->info.spectator ? "has moved to the sidelines" : "joined the game");
        if (!s->info.spectator)
            s->event = 6;
        return q2_player_print(g, (qa_actor_id){0}, 2, text, e);
    }
    bool selected = weapon_selected(g, a);
    if (!q2_actor_live(g, a->id))
        return true;
    if (selected && !s->info.spectator && !s->weapon_thunk) {
        if (!weapon_turn(g, a, (s->latched_buttons & 1) != 0, e))
            return false;
    } else
        s->weapon_thunk = false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->info.dead) {
        if (s->character_configured) {
            if (g->now_ns > s->respawn_ns &&
                ((s->latched_buttons & (g->options.deathmatch ? 1u : UINT32_MAX)) ||
                 (g->options.deathmatch && (g->options.deathmatch_flags & 1024)))) {
                s->latched_buttons = 0;
                if (!p->services.request_respawn) {
                    qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                                 "Q2 CHARACTER has no selected campaign respawn service");
                    return false;
                }
                return p->services.request_respawn(p->services.context, a->id, e);
            }
            return true;
        }
        if (g->now_ns <= s->respawn_ns || p->restart_ns)
            return true;
        if (rr && g->options.cooperative && (p->rules.coop_squad_respawn || p->rules.coop_lives))
            return q2_player_coop_respawn(g, a, e);
        if ((s->latched_buttons & (g->options.deathmatch ? 1u : UINT32_MAX)) ||
            (g->options.deathmatch &&
             (rr ? p->rules.force_respawn : (g->options.deathmatch_flags & 1024) != 0))) {
            s->latched_buttons = 0;
            return qa_q2_player_respawn(g, a->id, e);
        }
        return true;
    }
    if (!g->options.deathmatch) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        if (!q2_player_emit(g,
                            &(qa_q2_player_event){.kind = QA_Q2_PLAYER_TRAIL,
                                                  .actor = a->id,
                                                  .origin = body.origin,
                                                  .time_ns = g->now_ns},
                            e))
            return false;
    }
    s->latched_buttons = 0;
    return true;
}
static bool after_movement(void *context, qa_actor_id id, qa_error *e) {
    qa_q2_game *g = context;
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    if (a->projectile.kind != Q2_PROJECTILE_NONE)
        return true;
    if (qa_q2_player_controlled(g, id))
        return !q2_actor_live(g, id) || qa_q2_clear_input(g, id, e);
    qa_q2_player_movement m;
    if (!q2_player_observe(g, a, &m, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    q2_client_state *s = a->client;
    q2_players *p = g->player_runtime;
    s->latched_buttons |= m.buttons & ~s->buttons;
    s->buttons = m.buttons;
    if (p->intermission) {
        if (g->now_ns > q2_deadline(p->intermission_ns, 5 * Q2_NS) && m.buttons && p->next_map &&
            (g->options.product != QA_Q2_N64 || g->options.deathmatch || p->camera_set))
            p->exit = true;
        return true;
    }
    bool selected = weapon_selected(g, a);
    if (!q2_actor_live(g, id))
        return true;
    if (s->info.spectator) {
        if (s->latched_buttons & 1) {
            s->latched_buttons &= ~1u;
            if (!qa_q2_player_chase(g, id, 1, true, e))
                return false;
        }
    } else if (selected && (s->latched_buttons & 1) && !s->weapon_thunk) {
        s->weapon_thunk = true;
        if (!weapon_turn(g, a, true, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *watcher = g->actors[i];
        if (watcher && watcher->client &&
            qa_actor_id_equal(watcher->client->info.chase_target, id) &&
            !q2_player_update_chase(g, watcher, e))
            return false;
    }
    if (!s->character_configured && g->options.edition == QA_Q2_RERELEASE &&
        q2_actor_live(g, id))
        return q2_player_falling(g, a, &m, e);
    return true;
}
bool qa_q2_player_after_movement(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    return qa_q2_run_actor(g, id, after_movement, g, e);
}
static bool end_frame(void *context, qa_actor_id id, qa_error *e) {
    qa_q2_game *g = context;
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    if (a->projectile.kind != Q2_PROJECTILE_NONE)
        return true;
    bool controlled = qa_q2_player_controlled(g, id);
    if (!q2_actor_live(g, id)) return true;
    if (controlled && !qa_q2_clear_input(g, id, e)) return false;
    qa_q2_player_movement m;
    if (!q2_player_observe(g, a, &m, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (controlled) return q2_player_build_view(g, a, &m, e);
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (rr && !qa_q2_entities_player_begin(g, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!g->player_runtime->intermission) {
        if (!a->client->character_configured && !q2_player_environment(g, a, &m, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        float speed = hypotf(body.velocity.x, body.velocity.y);
        q2_client_state *s = a->client;
        if (speed < 5) {
            s->bob_time = 0;
            s->bob_move = 0;
        } else if (m.grounded)
            s->bob_move = g->options.edition == QA_Q2_RERELEASE
                              ? (float)((double)g->frame_ns / Q2_NS) / (speed > 210   ? .4f
                                                                        : speed > 100 ? .8f
                                                                                      : 1.6f)
                          : speed > 210 ? .25f
                          : speed > 100 ? .125f
                                        : .0625f;
        s->bob_time += s->bob_move;
        if (!s->character_configured && g->options.edition == QA_Q2_CLASSIC &&
            !q2_player_falling(g, a, &m, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    if (!q2_player_build_view(g, a, &m, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (rr && !qa_q2_entities_player_frame(g, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!q2_player_compass_update(g, a, false, e))
        return false;
    if (!rr || !q2_actor_live(g, id))
        return true;
    q2_client_state *s = a->client;
    qa_combat_state combat;
    qa_q2_powerups powers;
    if (!qa_combat_read(g->services.combat, id, &combat, e) ||
        !qa_q2_powerups_read(g, id, &powers, e))
        return false;
    bool playing = !g->player_runtime->intermission;
    s->visual.alpha =
        playing && combat.health > 0 && powers.invisibility_until_ns > g->now_ns
            ? q2_clamp(q2_seconds_left(s->invisibility_fade_ns, g->now_ns) / 2, .1f, 1)
            : 1;
    if (!q2_player_emit(g,
                        &(qa_q2_player_event){
                            .kind = QA_Q2_PLAYER_ALPHA, .actor = id, .alpha = s->visual.alpha},
                        e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!q2_player_emit(
            g,
            &(qa_q2_player_event){.kind = QA_Q2_PLAYER_FLASHLIGHT,
                                  .actor = id,
                                  .hand = s->hand,
                                  .visible = playing && s->info.flashlight && combat.health > 0},
            e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (playing && g->options.cooperative && g->player_runtime->rules.coop_player_collision &&
        !s->player_collision && combat.can_take_damage) {
        qa_body_state body;
        qa_trace_result hit;
        if (!qa_world_body_read(g->services.world, id, &body, e) ||
            !q2_player_trace(g, id, body.origin, body.origin, &body.bounds, Q2_PLAYER_CONTENTS,
                             &hit, e))
            return false;
        if (!hit.start_solid && !hit.all_solid) {
            s->player_collision = true;
            if (a->physics_bound)
                a->physics.clip_mask |= Q2_PLAYER_CONTENTS;
            qa_q2_player_services *services = &g->player_runtime->services;
            return !services->player_collision ||
                   services->player_collision(services->context, id, true, e);
        }
    }
    return true;
}
bool q2_client_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    (void)g;
    (void)contact;
    (void)e;
    return true;
}
bool q2_client_reaction(qa_q2_game *g, const qa_damage_outcome *outcome, qa_error *e) {
    q2_actor *a = q2_actor_get(g, outcome->request.target, false, NULL);
    if (!a || !a->client)
        return true;
    if (outcome->result.reaction == QA_REACTION_DEATH)
        return q2_player_death(g, a, outcome, e);
    q2_client_state *s = a->client;
    const qa_damage_result *r = &outcome->result;
    bool q2 = r->has_feedback && r->feedback_family == QA_GAME_Q2;
    s->damage_blood += q2 ? r->blood : r->applied_damage;
    s->damage_armor += q2 ? r->armor_saved : 0;
    s->damage_power += q2 ? r->power_saved : 0;
    s->damage_knockback += q2 ? r->knockback : outcome->request.knockback;
    s->damage_from = outcome->request.point;
    if (q2 && r->power_saved > 0)
        s->power_armor_ns = q2_deadline(g->now_ns, 200 * Q2_MS);
    if (q2)
        s->last_damage_ns = q2_deadline(g->now_ns, 2 * Q2_NS);
    if (g->options.edition == QA_Q2_RERELEASE && s->info.connected) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        return q2_player_emit(g,
                              &(qa_q2_player_event){.kind = QA_Q2_PLAYER_DIRECTIONAL_DAMAGE,
                                                    .actor = a->id,
                                                    .direction = qa_vec_normalize(qa_vec_sub(
                                                        outcome->request.point, body.origin)),
                                                    .damage = r->applied_damage,
                                                    .health = r->blood > 0,
                                                    .armor = r->armor_saved > 0,
                                                    .shield = r->power_saved > 0},
                              e);
    }
    return true;
}
bool qa_q2_player_end_frame(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    return qa_q2_run_actor(g, id, end_frame, g, e);
}
bool qa_q2_player_weapon_fired(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    a->client->last_firing_ns = q2_deadline(g->now_ns, 2500 * Q2_MS);
    return true;
}
bool qa_q2_player_animation(qa_q2_game *g, qa_actor_id id, int priority, int first, int last,
                            qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    a->client->animation_priority = priority;
    a->client->visual.frame = first;
    a->client->animation_end = last;
    a->client->animation_ns = 0;
    return true;
}
bool q2_player_noise(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, bool secondary, qa_error *e) {
    (void)e;
    if (g->options.deathmatch || !q2_actor_live(g, id))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (!g->services.actor_traits || !g->services.actor_traits(g->services.context, id, &traits) ||
        !traits.player || traits.no_target)
        return true;
    g->player_runtime->noise[secondary ? 1 : 0] = (qa_q2_player_noise_record){
        .owner = id, .origin = origin, .time_ns = g->now_ns, .present = true};
    return true;
}
bool qa_q2_player_noise_read(const qa_q2_game *g, bool secondary, qa_q2_player_noise_record *out) {
    if (!g || !out)
        return false;
    *out = g->player_runtime->noise[secondary ? 1 : 0];
    if (out->present && !qa_actors_get(qa_session_actors(g->services.session), out->owner))
        out->present = false;
    return out->present;
}
bool q2_player_tracker_pain(qa_q2_game *g, qa_actor_id id, uint64_t until, qa_error *e) {
    (void)e;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (a && a->client)
        a->client->tracker_ns = until;
    return true;
}
bool q2_player_invisibility_reveal(qa_q2_game *g, qa_actor_id id, uint64_t until, qa_error *e) {
    (void)e;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (a && a->client)
        a->client->invisibility_fade_ns = until;
    return true;
}
bool q2_player_nuke_blind(qa_q2_game *g, qa_actor_id id, uint64_t until, bool inside, qa_error *e) {
    (void)e;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (a && a->client) {
        a->client->nuke_ns = until;
        a->client->nuke_inside = inside;
    }
    return true;
}
bool q2_client_item_received(qa_q2_game *g, qa_actor_id id, const qa_q2_item_definition *d,
                             qa_error *e) {
    (void)e;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client)
        return true;
    q2_client_state *s = a->client;
    s->bonus_alpha = .25f;
    if (d->kind == QA_Q2_ITEM_POWER || d->kind == QA_Q2_ITEM_POWER_ARMOR ||
        d->kind == QA_Q2_ITEM_SPHERE || d->kind == QA_Q2_ITEM_DECOY || d->kind == QA_Q2_ITEM_NUKE)
        s->info.selected_item = d->item;
    return true;
}

bool q2_player_damage_view(qa_q2_game *g, qa_actor_id id, float pitch, float roll, uint64_t until,
                           qa_error *e) {
    (void)e;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (a && a->client) {
        a->client->damage_pitch = pitch;
        a->client->damage_roll = roll;
        a->client->damage_ns = until;
    }
    return true;
}
