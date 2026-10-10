#include "internal.h"

static bool team_death(qa_modes *m, mode_instance *v, qa_actor_id actor, bool clear_god,
                       qa_error *e) {
    if (clear_god) {
        qa_combat_state state;
        if (!qa_combat_read_traits(m->options.services.combat, actor, &state, e))
            return false;
        state.invulnerable = false;
        if (!qa_combat_set_traits(m->options.services.combat, actor, &state, e))
            return false;
    }
    qa_damage_request request = {.target = actor,
                                 .amount = 1000,
                                 .attack = {.attacker = actor,
                                            .inflictor = actor}};
    qa_game_family family = v->value.rules.source <= QA_MODE_Q1_HORDE ? QA_GAME_Q1
                            : v->value.rules.source >= QA_MODE_Q3     ? QA_GAME_Q3
                                                                      : QA_GAME_Q2;
    if (family == QA_GAME_Q1) {
        request.attack.cause.kind = QA_CAUSE_Q1;
        if (!qa_builtin_resource(&m->options.services, "ctf:teamchange",
                                 &request.attack.cause.source.q1.death_type, e))
            return false;
    } else if (family == QA_GAME_Q2) {
        request.amount = 100000;
        request.attack.cause.kind = QA_CAUSE_Q2;
        request.attack.cause.source.q2.means_of_death = 23;
        request.attack.cause.source.q2.flags = 32;
        request.attack.cause.source.q2.native = QA_Q2_CAUSE_CLASSIC;
    } else {
        request.amount = 100000;
        request.attack.cause.kind = QA_CAUSE_Q3;
        request.attack.cause.source.q3.means_of_death = 20;
        request.attack.cause.source.q3.flags = 32;
        if (!m->options.hooks.force_death)
            return mode_fail(e, "team change needs the selected direct death provider");
        if (!mode_damage_prepare(m, v, &request, e))
            return false;
        return MODE_CALLBACK(m,
                             m->options.hooks.force_death(m->options.hooks.context, &request, e));
    }
    return mode_damage(m, v, family, &request, e);
}
static bool request_team(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team,
                         bool observer, bool automatic, bool command, bool *accepted,
                         qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_player *player = mode_player_get(m, actor);
    mode_member *member = mode_member_get(m, v, actor);
    if (!v || !player || !member || !accepted)
        return mode_fail(e, "invalid team command");
    *accepted = false;
    qa_actor_owner native_owner;
    bool native = v->value.rules.source >= QA_MODE_Q3 && m->options.hooks.q3_native_source &&
        m->options.hooks.q3_native_source(m->options.hooks.context, id, &native_owner);
    if (native) {
        bool source_changed;
        if (!m->options.hooks.q3_team_request)
            return mode_fail(e, "native Q3 SetTeam has no actual source producer");
        return MODE_CALLBACK(m, m->options.hooks.q3_team_request(m->options.hooks.context,
            id, actor, team, observer, automatic,
            observer ? QA_MODE_Q3_SPECTATOR_FREE : QA_MODE_Q3_SPECTATOR_NOT,
            0, accepted, &source_changed, e));
    } else if (automatic && !qa_modes_choose_team(m, id, actor, &team, e))
        return false;
    if (observer)
        team = 0;
    if (team && v->value.rules.kind > QA_MODE_TEAM_DEATHMATCH && mode_team_index(v, team) < 0)
        return mode_fail(e, "team command names an unknown objective team");
    qa_team_id previous;
    if (!qa_modes_team(m, v->id, actor, &previous, e))
        return false;
    qa_mode_source source = v->value.rules.source;
    if (command && v->value.rules.source >= QA_MODE_Q3) {
        if (member->team_switch_ns > v->value.time_ns)
            return true;
        member->team_switch_ns = v->value.time_ns + 5 * MODE_SECOND;
        if (v->value.rules.kind == QA_MODE_DUEL && !member->player.spectator) {
            member->player.losses = mode_add_i32(member->player.losses, 1);
        }
    }
    if (previous == team && member->player.spectator == observer &&
        source < QA_MODE_Q3) {
        *accepted = true;
        return true;
    }
    if (!observer && ((v->value.rules.forced_team && team != v->value.rules.forced_team) ||
                      (v->value.rules.match_lock &&
                       (v->value.phase == QA_MODE_COUNTDOWN || v->value.phase == QA_MODE_PLAYING))))
        return true;
    if (source == QA_MODE_LMCTF && !observer &&
        (v->value.rules.match_lock || (v->value.rules.flags & 8u)))
        return true;
    if (source == QA_MODE_LMCTF && !qa_modes_can_move(m, id, actor))
        return true;
    if (source >= QA_MODE_Q3) {
        if (!native && !observer && v->value.rules.force_balance &&
            v->value.rules.kind >= QA_MODE_TEAM_DEATHMATCH) {
            int counts[2] = {0};
            for (size_t i = 0; i < m->players_order.count; ++i) {
                qa_actor_id other = m->players_order.ids[i];
                qa_team_id own;
                if (qa_actor_id_equal(other, actor) || !mode_member_get(m, v, other) ||
                    !qa_modes_team(m, v->id, other, &own, NULL))
                    continue;
                int index = mode_team_index(v, own);
                if (index >= 0 && index < 2)
                    ++counts[index];
            }
            int index = mode_team_index(v, team);
            if (index >= 0 && index < 2 && counts[index] - counts[index ^ 1] > 1)
                return true;
        }
        if (!native && !observer && ((v->value.rules.kind == QA_MODE_DUEL && v->value.playing >= 2) ||
                          (v->value.rules.max_game_players &&
                           v->value.playing >= (size_t)v->value.rules.max_game_players))) {
            observer = true;
            team = 0;
        }
        if (!observer && previous == team && !member->player.spectator) {
            *accepted = true;
            return true;
        }
    }
    if (source == QA_MODE_Q2_CTF && !observer &&
        (v->value.phase == QA_MODE_COUNTDOWN || v->value.phase == QA_MODE_PLAYING))
        return true;
    if (source == QA_MODE_ROGUE && previous && v->value.rules.teamplay >= 4 &&
        !(v->value.rules.flags & 16u)) {
        if (member->suicide_count > 3 && m->options.hooks.disconnect &&
            !MODE_CALLBACK(m, m->options.hooks.disconnect(m->options.hooks.context, actor, e)))
            return false;
        if (!mode_live(m, actor))
            return true;
        ++member->suicide_count;
        member->spawn_state = 2;
        if (!team_death(m, v, actor, false, e))
            return false;
        return qa_modes_set_team(m, v->id, actor, member->last_team, e);
    }
    bool was_spectator = member->player.spectator;
    if (!was_spectator && (!observer || source == QA_MODE_THREEWAVE || source >= QA_MODE_Q3) &&
        !team_death(m, v, actor, source == QA_MODE_Q2_CTF || source >= QA_MODE_Q3, e))
        return false;
    if (!mode_live(m, actor))
        return true;
    if (source == QA_MODE_LMCTF && !observer && !was_spectator) {
        if (!qa_modes_add_score(m, id, actor, 1, e))
            return false;
        member = mode_member_get(m, v, actor);
        if (!member)
            return true;
        mode_stat_add(v, &member->stats.deaths, -1);
    }
    if (!mode_join(m, v, actor, team, observer, false, e))
        return false;
    if (source != QA_MODE_THREEWAVE && source != QA_MODE_LMCTF && source < QA_MODE_Q3 &&
        !qa_modes_set_score(m, id, actor, 0, e))
        return false;
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    if (source >= QA_MODE_Q3 && command)
        member->team_switch_ns = v->value.time_ns + 5 * MODE_SECOND;
    player = mode_player_get(m, actor);
    if (!player)
        return true;
    member->player.leader = false;
    member->player.follow_target = (qa_actor_id){0};
    member->player.automatic_follow = 0;
    member->player.observer_team = 0;
    if (!mode_q3_session_team(m, v, actor, observer, e))
        return false;
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    if (source >= QA_MODE_Q3) {
        qa_team_id affected[] = {previous, team};
        for (int side = 0; side < 2; ++side) {
            if (!affected[side])
                continue;
            mode_member *leader = NULL, *first = NULL, *human = NULL;
            for (size_t i = 0; i < m->players_order.count; ++i) {
                qa_actor_id other = m->players_order.ids[i];
                mode_player *candidate = mode_player_get(m, other);
                mode_member *participant = mode_member_get(m, v, other);
                qa_team_id own;
                if (!candidate || !participant ||
                    !qa_modes_team(m, v->id, other, &own, NULL) || own != affected[side])
                    continue;
                if (!first)
                    first = participant;
                if (!human && !candidate->identity->bot)
                    human = participant;
                if (participant->player.leader)
                    leader = participant;
            }
            mode_player *leader_player = leader ? mode_player_get(m, leader->actor) : NULL;
            if (!leader_player || (side == 1 && leader_player->identity->bot && !player->identity->bot)) {
                if (leader) {
                    leader->player.leader = false;
                }
                mode_member *next = side == 1 ? member : human ? human : first;
                if (next && mode_member_get(m, v, next->actor)) {
                    next->player.leader = true;
                }
            }
        }
    }
    *accepted = true;
    if (!mode_event(m, v, QA_MODE_ROSTER, actor, (qa_actor_id){0}, (qa_actor_id){0}, team,
                    observer ? 0 : 1, 0, e))
        return false;
    return !m->options.hooks.respawn ||
           MODE_CALLBACK(m, m->options.hooks.respawn(m->options.hooks.context, id, actor, true, e));
}
bool qa_modes_request_team(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team,
                           bool observer, bool automatic, bool *accepted, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid team service");
    return MODE_CALLBACK(m,
                         request_team(m, id, actor, team, observer, automatic, true, accepted, e));
}
static bool followable(qa_modes *m, mode_instance *v, qa_actor_id viewer, qa_actor_id actor) {
    mode_player *p = mode_player_get(m, actor);
    mode_member *target = mode_member_get(m, v, actor);
    mode_member *watcher = mode_member_get(m, v, viewer);
    if (watcher && watcher->player.observer_team) {
        qa_team_id team;
        if (!qa_modes_team(m, v->id, actor, &team, NULL) || team != watcher->player.observer_team)
            return false;
    }
    return p && target && p->connected && !p->connecting &&
           !target->player.spectator && !qa_actor_id_equal(actor, viewer);
}
static bool follow(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_actor_id target, int automatic,
                   int cycle, bool *accepted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !accepted || automatic < 0 ||
        automatic > 2 || cycle < -1 || cycle > 1)
        return mode_fail(e, "invalid spectator follow command");
    *accepted = false;
    if (cycle) {
        size_t start = SIZE_MAX;
        for (size_t i = 0; i < m->players_order.count; ++i)
            if (qa_actor_id_equal(m->players_order.ids[i], member->player.follow_target)) {
                start = i;
                break;
            }
        for (size_t step = 1; step <= m->players_order.count; ++step) {
            size_t n = m->players_order.count;
            size_t index = start == SIZE_MAX ? (cycle > 0 ? step - 1 : n - step)
                           : cycle > 0       ? (start + step) % n
                                             : (start + n - step) % n;
            qa_actor_id candidate = m->players_order.ids[index];
            if (followable(m, v, actor, candidate)) {
                target = candidate;
                automatic = 0;
                break;
            }
        }
        if (!followable(m, v, actor, target))
            return true;
    } else if (target.registry && !followable(m, v, actor, target))
        return true;
    if (!member->player.spectator) {
        if (v->value.rules.kind == QA_MODE_DUEL) {
            member->player.losses = mode_add_i32(member->player.losses, 1);
        }
        if (!request_team(m, id, actor, 0, true, false, false, accepted, e))
            return false;
        if (!*accepted)
            return true;
        member = mode_member_get(m, v, actor);
        if (!member)
            return true;
    }
    if (!mode_q3_session_follow(m, v, actor, target, automatic, e))
        return false;
    member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    member->player.follow_target = automatic ? (qa_actor_id){0} : target;
    member->player.automatic_follow = (int8_t)automatic;
    member->player.scoreboard = false;
    *accepted = true;
    qa_actor_owner native_owner;
    if (!target.registry && !automatic && m->options.hooks.q3_stop_following &&
        m->options.hooks.q3_native_source &&
        m->options.hooks.q3_native_source(m->options.hooks.context, id, &native_owner) &&
        !MODE_CALLBACK(m, m->options.hooks.q3_stop_following(m->options.hooks.context, id, actor, e)))
        return false;
    return mode_event(m, v, QA_MODE_ROSTER, actor, target, (qa_actor_id){0}, 0, automatic, 4, e);
}
bool qa_modes_follow(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_actor_id target,
                     int automatic, int cycle, bool *accepted, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid spectator service");
    return MODE_CALLBACK(m, follow(m, id, actor, target, automatic, cycle, accepted, e));
}
bool qa_modes_follow_target(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_actor_id *out,
                            qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !out)
        return mode_fail(e, "invalid spectator target query");
    *out = (qa_actor_id){0};
    if (!member->player.spectator)
        return true;
    if (!member->player.automatic_follow) {
        if (followable(m, v, actor, member->player.follow_target))
            *out = member->player.follow_target;
        return true;
    }
    unsigned count = 0;
    for (size_t i = 0; i < m->players_order.count; ++i)
        if (followable(m, v, actor, m->players_order.ids[i]) &&
            ++count == (unsigned)member->player.automatic_follow) {
            *out = m->players_order.ids[i];
            break;
        }
    return true;
}
bool qa_modes_observe(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id filter,
                      bool *accepted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !accepted || (filter && mode_team_index(v, filter) < 0))
        return mode_fail(e, "invalid observer team");
    if (!qa_modes_request_team(m, id, actor, 0, true, false, accepted, e))
        return false;
    if (!*accepted)
        return true;
    mode_member *member = mode_member_get(m, v, actor);
    if (!member)
        return true;
    member->player.observer_team = filter;
    return true;
}
bool qa_modes_scoreboard(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool visible, qa_error *e) {
    mode_member *member = mode_member_get(m, mode_get(m, id), actor);
    if (!member)
        return mode_fail(e, "unknown scoreboard participant");
    member->player.scoreboard = visible;
    return qa_modes_rank(m, id, e);
}
static bool suicide(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool *handled, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    mode_player *player = mode_player_get(m, actor);
    if (!v || !member || !player || !handled)
        return mode_fail(e, "invalid mode suicide");
    *handled = v->value.rules.source == QA_MODE_THREEWAVE || v->value.rules.source >= QA_MODE_Q3;
    if (v->value.rules.source >= QA_MODE_Q3) {
        qa_combat_state combat;
        if (!qa_combat_read(m->options.services.combat, actor, &combat, e))
            return false;
        return member->player.spectator || combat.health <= 0 || team_death(m, v, actor, true, e);
    }
    if (!*handled || member->player.spectator || v->value.rules.start_map)
        return true;
    if (!m->options.hooks.q1_ctf_suicide_notice)
        return mode_fail(e, "ThreeWave suicide needs its actual source notice producer");
    bool limited = member->suicide_count > 3;
    if (!MODE_CALLBACK(m, m->options.hooks.q1_ctf_suicide_notice(
        m->options.hooks.context, id, actor, limited, e))) return false;
    if (limited) return true;
    if (mode_get(m, id) != v || !mode_member_get(m, v, actor))
        return mode_fail(e, "ThreeWave suicide participant retired during notice");
    if (!qa_modes_drop(m, id, actor, true, e)) return false;
    if (mode_get(m, id) != v || !mode_member_get(m, v, actor))
        return mode_fail(e, "ThreeWave suicide participant retired during drop");
    if (!m->options.hooks.release_grapple || !MODE_CALLBACK(m,
        m->options.hooks.release_grapple(m->options.hooks.context, actor, e)))
        return m->options.hooks.release_grapple ? false :
            mode_fail(e, "ThreeWave suicide needs its actual unhook producer");
    if (mode_get(m, id) != v || !mode_member_get(m, v, actor))
        return mode_fail(e, "ThreeWave suicide participant retired during unhook");
    if (!qa_modes_add_score(m, id, actor, -2, e)) return false;
    member = mode_member_get(m, mode_get(m, id), actor);
    if (mode_get(m, id) != v || !member)
        return mode_fail(e, "ThreeWave suicide participant retired during score");
    ++member->suicide_count;
    if (!m->options.hooks.respawn)
        return mode_fail(e, "ThreeWave suicide needs its actual source respawn producer");
    return MODE_CALLBACK(m,
        m->options.hooks.respawn(m->options.hooks.context, id, actor, true, e));
}
bool qa_modes_suicide(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool *handled, qa_error *e) {
    if (!m) return mode_fail(e, "invalid mode suicide service");
    return MODE_CALLBACK(m, suicide(m, id, actor, handled, e));
}
static bool observer_move(qa_modes *m, mode_instance *v, mode_member *member,
                          const qa_mode_controls *input, qa_error *e) {
    qa_actor_id actor = member->actor;
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, actor, &body, e))
        return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(input->view_angles, &forward, NULL, NULL);
    qa_vec3 horizontal = forward;
    horizontal.z = 0;
    float cosine = qa_vec_length(horizontal), inverse = cosine == 0 ? 0 : 1 / cosine;
    qa_vec3 facing = qa_vec_scale(horizontal, inverse), velocity = body.velocity;
    velocity.z = 0;
    float dot = qa_vec_dot(facing, velocity);
    qa_vec3 parallel = qa_vec_scale(facing, dot), strafe = qa_vec_sub(velocity, parallel);
    qa_vec3 projected =
        qa_vec_scale(forward, (dot < 0 ? -1 : 1) * qa_vec_length(parallel) * inverse);
    projected.z += body.velocity.z * .75f;
    float speed = qa_vec_length(projected), maximum = 320 - 100 * forward.z;
    if (speed > maximum)
        projected = qa_vec_scale(projected, maximum / speed);
    if (fabsf(body.angles.x) == 30)
        projected.z = -projected.z;
    body.velocity = qa_vec_add(projected, strafe);
    if (!qa_world_body_write(m->options.services.world, actor, &body, e))
        return false;
    if (m->options.hooks.observer_nearby &&
        !MODE_CALLBACK(m, m->options.hooks.observer_nearby(m->options.hooks.context, actor, e)))
        return false;
    if (input->jump && !member->observer_jump) {
        qa_mode_spawnpoint point;
        if (!qa_modes_spawnpoint(m, v->id, actor, false, &point, e) ||
            !qa_world_body_read(m->options.services.world, actor, &body, e))
            return false;
        body.origin = point.origin;
        body.origin.z += 1;
        body.angles = point.angles;
        if (!qa_world_body_write(m->options.services.world, actor, &body, e) ||
            !qa_world_link(m->options.services.world, actor, NULL, e))
            return false;
        qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_TELEPORT,
                                           .body = body,
                                           .view_angles = point.angles,
                                           .force_view_angles = true,
                                           .hold_ns = input->teleport_hold_ns};
        if (m->options.services.motion_changed &&
            !MODE_CALLBACK(m, m->options.services.motion_changed(m->options.services.context, actor,
                                                                 &change, e)))
            return false;
    }
    member->observer_jump = input->jump;
    return true;
}
static bool controls(qa_modes *m, qa_mode_id id, qa_actor_id actor, const qa_mode_controls *input,
                     bool *consumed, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *p = mode_member_get(m, v, actor);
    mode_player *player = mode_player_get(m, actor);
    if (!v || !p || !player || !input || !consumed || !qa_vec_finite(input->view_angles))
        return mode_fail(e, "invalid mode controls");
    *consumed = false;
    int impulse = input->impulse;
    if (v->value.rules.source == QA_MODE_ROGUE) {
        qa_team_id current;
        if (!qa_modes_team(m, v->id, actor, &current, e))
            return false;
        bool legal = v->value.rules.teamplay < 4
                         ? current != 0
                         : mode_team_index(v, current) >= 0 &&
                               (current != v->value.rules.teams[2] || v->value.rules.teamplay == 6);
        if (v->value.rules.teamplay < 4)
            p->last_team = current;
        else if (current != p->last_team || !legal) {
            if (p->last_team && mode_team_index(v, p->last_team) >= 0 &&
                !(v->value.rules.flags & 16u)) {
                if (p->suicide_count > 3 && m->options.hooks.disconnect &&
                    !MODE_CALLBACK(m,
                                   m->options.hooks.disconnect(m->options.hooks.context, actor, e)))
                    return false;
                if (!mode_live(m, actor))
                    return true;
                ++p->suicide_count;
                p->spawn_state = 2;
                if (!team_death(m, v, actor, false, e) ||
                    !qa_modes_set_team(m, v->id, actor, p->last_team, e))
                    return false;
            } else {
                if (p->last_team && !team_death(m, v, actor, false, e))
                    return false;
                if (!legal && !qa_modes_choose_team(m, id, actor, &current, e))
                    return false;
                if (!qa_modes_set_score(m, id, actor, 0, e) ||
                    !qa_modes_set_team(m, v->id, actor, current, e))
                    return false;
                p->last_team = current;
            }
        }
        if (impulse != 23)
            return true;
        *consumed = true;
        return mode_event(m, v, QA_MODE_FLAG_STATUS, actor, (qa_actor_id){0}, (qa_actor_id){0},
                          p->last_team, 0, 0, e);
    }
    if (v->value.rules.source != QA_MODE_THREEWAVE)
        return true;
    if (p->introduction_frames < 3 && ++p->introduction_frames == 2 &&
        !mode_event(m, v, p->player.spectator ? QA_MODE_TEAM_PROMPT : QA_MODE_TEAM_RULES, actor,
                    (qa_actor_id){0}, (qa_actor_id){0}, p->last_team, v->value.rules.teamplay, 0,
                    e))
        return false;
    if (player->identity->bot && !p->last_team)
        impulse = 103;
    if ((impulse >= 100 && impulse <= 104) ||
        (!input->prompt_supported && p->player.spectator &&
         ((impulse >= 1 && impulse <= 3) || input->jump))) {
        *consumed = true;
        if (impulse == 100 && (v->value.rules.teamplay & 64))
            return mode_event(m, v, QA_MODE_MESSAGE, actor, (qa_actor_id){0}, (qa_actor_id){0},
                              p->last_team, 0, 3, e);
        bool observer = impulse == 100 || impulse == 104,
             automatic = impulse == 103 || impulse == 3 || input->jump;
        qa_team_id team = impulse == 1 || impulse == 101   ? v->value.rules.teams[0]
                          : impulse == 2 || impulse == 102 ? v->value.rules.teams[1]
                                                           : p->last_team;
        bool accepted;
        if (!qa_modes_request_team(m, id, actor, team, observer, automatic, &accepted, e))
            return false;
        return impulse != 100 || mode_event(m, v, QA_MODE_TEAM_PROMPT, actor, (qa_actor_id){0},
                                            (qa_actor_id){0}, 0, 0, 0, e);
    }
    qa_team_id current;
    if (!qa_modes_team(m, v->id, actor, &current, e))
        return false;
    if (!p->player.spectator && !v->value.rules.start_map && v->value.rules.teamplay >= 0 &&
        (current != p->last_team || p->player.ctf_last_team < 0)) {
        qa_team_id previous = p->player.ctf_last_team < 0 ? 0 : p->last_team;
        if ((v->value.rules.teamplay & 64) && previous) {
            if (p->suicide_count > 3 && m->options.hooks.disconnect &&
                !MODE_CALLBACK(m, m->options.hooks.disconnect(m->options.hooks.context, actor, e)))
                return false;
            if (!mode_live(m, actor))
                return true;
            ++p->suicide_count;
            p->spawn_state = 2;
            if (!team_death(m, v, actor, true, e) || !qa_modes_set_team(m, v->id, actor, previous, e))
                return false;
        } else {
            if (previous && !team_death(m, v, actor, false, e))
                return false;
            if ((!current || p->player.ctf_last_team < 0) &&
                !qa_modes_choose_team(m, id, actor, &current, e))
                return false;
            if (!qa_modes_set_team(m, v->id, actor, current, e) || !qa_modes_set_score(m, id, actor, 0, e))
                return false;
            p->last_team = current;
        }
    }
    p->player.ctf_last_team = v->value.rules.start_map || p->player.spectator ? 1
        : p->last_team == v->value.rules.teams[0] ? 5
        : p->last_team == v->value.rules.teams[1] ? 14 : p->player.ctf_last_team;
    if (p->player.spectator)
        return observer_move(m, v, p, input, e);
    if (impulse == 22 || (impulse == 1 && !input->grapple_selected)) {
        *consumed = true;
        if (v->value.rules.start_map || (v->value.rules.teamplay & 2048))
            return true;
        return m->options.hooks.select_grapple
                   ? MODE_CALLBACK(
                         m, m->options.hooks.select_grapple(m->options.hooks.context, actor, e))
                   : mode_fail(e, "grapple selection requires equipment coordinator");
    }
    if ((impulse == 20 || impulse == 21) && (v->value.rules.teamplay & 128)) {
        *consumed = true;
        return m->options.hooks.drop_arsenal
                   ? MODE_CALLBACK(m, m->options.hooks.drop_arsenal(m->options.hooks.context, actor,
                                                                    impulse == 21, e))
                   : mode_fail(e, "item drop requires selected arsenal");
    }
    if (impulse == 25) {
        *consumed = true;
        return mode_event(m, v, QA_MODE_TEAM_RULES, actor, (qa_actor_id){0}, (qa_actor_id){0},
                          p->last_team, v->value.rules.teamplay, 0, e);
    }
    return true;
}
bool qa_modes_player_controls(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                              const qa_mode_controls *input, bool *consumed, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode controls service");
    return MODE_CALLBACK(m, controls(m, id, actor, input, consumed, e));
}
