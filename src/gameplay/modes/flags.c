#include "internal.h"

static bool recent(uint64_t now, uint64_t then, unsigned seconds, bool set) {
    return set && now >= then && now - then < (uint64_t)seconds * MODE_SECOND;
}
static bool points(qa_modes *m, mode_instance *v, qa_actor_id actor, int32_t amount, qa_error *e) {
    return !amount || qa_modes_add_score(m, v->id, actor, amount, e);
}
bool mode_flag_reset(qa_modes *m, mode_instance *v, mode_object *o, bool announce, qa_error *e) {
    qa_actor_id previous = o->value.carrier;
    mode_member *member = mode_member_get(m, v, previous);
    if (member)
        member->flag = (qa_actor_id){0};
    if (mode_live(m, previous) && !mode_object_count(m, v, o, previous, 0, e))
        return false;
    if (mode_live(m, o->dropped_actor)) {
        qa_actor_id drop = o->dropped_actor;
        o->dropped_actor = (qa_actor_id){0};
        if (!qa_session_release(m->options.services.session, drop, e))
            return false;
    }
    o->value.carrier = (qa_actor_id){0};
    o->value.previous_owner = (qa_actor_id){0};
    o->value.phase = QA_OBJECTIVE_HOME;
    o->value.deadline_ns = 0;
    o->expire_ns = 0;
    o->dropped = false;
    o->physics.motion = QA_PHYSICS_STATIONARY;
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    body.origin = o->home;
    body.velocity = (qa_vec3){0};
    body.angles = o->spec.angles;
    if (!qa_world_body_write(m->options.services.world, o->actor, &body, e) ||
        !mode_object_hide(m, o, false, e))
        return false;
    if (announce && !mode_event(m, v, QA_MODE_FLAG_RETURNED, (qa_actor_id){0}, (qa_actor_id){0},
                                o->actor, o->spec.team, 0, 0, e))
        return false;
    return mode_object_notify(m, v, o, e);
}
static bool capture(qa_modes *m, mode_instance *v, mode_object *flag, qa_actor_id actor,
                    qa_team_id team, qa_error *e) {
    qa_mode_source source = v->value.rules.source;
    uint64_t now = v->value.time_ns;
    mode_member *carrier = mode_member_get(m, v, actor);
    if (!carrier)
        return true;
    int index = mode_team_index(v, team);
    if (index < 0)
        return true;
    bool alternate =
        source == QA_MODE_ROGUE && v->value.rules.teamplay == 6 && team == v->value.rules.teams[2];
    int personal = source == QA_MODE_LMCTF        ? 5
                   : source == QA_MODE_TEAM_ARENA ? 100
                   : source == QA_MODE_Q3         ? 5
                   : alternate                    ? 8
                                                  : 15;
    int team_bonus = source == QA_MODE_TEAM_ARENA ? 25
                     : source == QA_MODE_Q3       ? 0
                     : alternate                  ? 4
                                                  : 10;
    if (source == QA_MODE_LMCTF && (v->value.rules.flags & 512u)) {
        int allies = 0, enemies = 0;
        for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
            uint32_t i = m->players_order.ids[ordinal].slot;
            mode_member *p = &v->members[i];
            mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
            qa_team_id t;
            if (!player || player->value.spectator || !qa_modes_team(m, p->actor, &t, NULL))
                continue;
            if (t == team)
                ++allies;
            else if (t)
                ++enemies;
        }
        team_bonus = 10 * (enemies + 1) / (allies + 1);
    }
    mode_stat_add(v, &carrier->stats.captures, 1);
    v->value.team_captures[index] = mode_add_i32(v->value.team_captures[index], 1);
    if (source >= QA_MODE_Q3 && !qa_modes_team_score(m, v->id, team, 1, e))
        return false;
    if (!points(m, v, actor, personal, e))
        return false;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        qa_team_id t;
        if (!player || !qa_modes_team(m, p->actor, &t, NULL))
            continue;
        if (t != team) {
            p->stats.hurt_carrier = false;
            continue;
        }
        bool same = qa_actor_id_equal(p->actor, actor);
        if ((!same || source == QA_MODE_LMCTF) && !points(m, v, p->actor, team_bonus, e))
            return false;
        if (alternate)
            continue;
        unsigned returned_window = source == QA_MODE_LMCTF      ? 3
                                   : source <= QA_MODE_Q1_HORDE ? 4
                                                                : 10;
        unsigned frag_window = source == QA_MODE_LMCTF || source <= QA_MODE_Q1_HORDE ? 6 : 10;
        bool returned = !(source == QA_MODE_ROGUE && v->value.rules.teamplay == 5) &&
                        recent(now, p->stats.returned_ns, returned_window, p->stats.returned);
        bool frag = recent(now, p->stats.carrier_killed_ns, frag_window, p->stats.carrier_killed);
        int assist = 0;
        if (source == QA_MODE_LMCTF) {
            assist = (returned ? 1 : 0) + (frag ? 1 : 0) +
                     (recent(now, p->stats.defended_ns, 2, p->stats.defended) ? 1 : 0);
            p->stats.returned = false;
            p->stats.carrier_killed = false;
            p->stats.defended = false;
        } else if (!same || source >= QA_MODE_Q3 || source == QA_MODE_ROGUE) {
            if (returned)
                assist += source == QA_MODE_TEAM_ARENA ? 10 : 1;
            if (frag && (!returned || source < QA_MODE_Q3))
                assist += source == QA_MODE_TEAM_ARENA ? 10 : 2;
        }
        if (assist) {
            if (!points(m, v, p->actor, assist, e))
                return false;
            mode_stat_add(v, source >= QA_MODE_Q3 ? &carrier->stats.assists : &p->stats.assists, 1);
            mode_stat_add(v, &p->stats.assist_awards, 1);
            if (!mode_event(m, v, QA_MODE_AWARD, p->actor, actor, flag->actor, team, assist,
                            0x20000, e))
                return false;
        }
    }
    if (!mode_event(m, v, QA_MODE_FLAG_CAPTURED, actor, (qa_actor_id){0}, flag->actor, team,
                    personal, 0, e))
        return false;
    for (int i = 0; i < 3; ++i) {
        mode_object *base = mode_object_get(m, v->bases[i]);
        if (alternate && base != flag)
            continue;
        if (base && base->spec.kind == QA_MODE_OBJECT_FLAG &&
            !mode_flag_reset(m, v, base, false, e))
            return false;
    }
    return qa_modes_rank(m, v->id, e);
}
bool mode_flag_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                     bool *accepted, qa_error *e) {
    mode_member *p = mode_member_get(m, v, actor);
    qa_team_id team;
    if (!p || !qa_modes_team(m, actor, &team, e))
        return p == NULL;
    int own = mode_team_index(v, team);
    if (own < 0)
        return true;
    qa_mode_source source = v->value.rules.source;
    if (source == QA_MODE_Q2_CTF &&
        (v->value.phase == QA_MODE_SETUP || v->value.phase == QA_MODE_COUNTDOWN))
        return true;
    if (source == QA_MODE_LMCTF &&
        (v->value.phase == QA_MODE_COUNTDOWN || (v->value.rules.referee_flags & (1u << own)) ||
         (v->value.rules.flags & 256u)))
        return true;
    if (source == QA_MODE_Q2_CTF && !o->targets_used) {
        o->targets_used = true;
        if (o->spec.target && m->options.services.use_targets &&
            !m->options.services.use_targets(m->options.services.context, o->actor, actor,
                                             o->spec.target, 0, 0, e))
            return false;
    }
    mode_object *carried = mode_object_get(m, p->flag);
    bool one = v->value.rules.kind == QA_MODE_ONE_FLAG ||
               (source == QA_MODE_ROGUE && v->value.rules.teamplay == 5);
    bool alternate =
        source == QA_MODE_ROGUE && v->value.rules.teamplay == 6 && team == v->value.rules.teams[2];
    if (o->spec.kind == QA_MODE_OBJECT_FLAG_BASE) {
        if (carried && ((one && o->spec.team && o->spec.team != team) ||
                        (alternate && o->spec.team && carried->spec.team != o->spec.team))) {
            *accepted = true;
            return capture(m, v, carried, actor, team, e);
        }
        return true;
    }
    if (carried && one && o->spec.team && o->spec.team != team) {
        *accepted = true;
        return capture(m, v, carried, actor, team, e);
    }
    if (o->spec.team == team && !alternate) {
        if (o->value.phase == QA_OBJECTIVE_DROPPED) {
            int bonus = source == QA_MODE_TEAM_ARENA ? 10 : 1;
            p->stats.returned = true;
            p->stats.returned_ns = v->value.time_ns;
            mode_stat_add(v, &p->stats.recoveries, 1);
            if (!points(m, v, actor, bonus, e))
                return false;
            if (source == QA_MODE_LMCTF)
                for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                    uint32_t i = m->players_order.ids[ordinal].slot;
                    mode_member *member = &v->members[i];
                    qa_team_id t;
                    if (member->joined && qa_modes_team(m, member->actor, &t, NULL) && t == team &&
                        recent(v->value.time_ns, member->stats.carrier_killed_ns, 6,
                               member->stats.carrier_killed)) {
                        member->stats.carrier_killed = false;
                        if (!points(m, v, member->actor, 1, e))
                            return false;
                    }
                }
            *accepted = true;
            return mode_flag_reset(m, v, o, true, e);
        }
        if (carried && o->value.phase == QA_OBJECTIVE_HOME && !one) {
            *accepted = true;
            return capture(m, v, carried, actor, team, e);
        }
        return true;
    }
    if (carried || o->value.phase == QA_OBJECTIVE_CARRIED || (one && o->spec.team))
        return true;
    if (qa_actor_id_equal(o->value.previous_owner, actor) && v->value.time_ns < o->owner_until_ns)
        return true;
    if (!mode_object_count(m, v, o, actor, 1, e))
        return false;
    if (mode_live(m, o->dropped_actor)) {
        qa_actor_id drop = o->dropped_actor;
        o->dropped_actor = (qa_actor_id){0};
        if (!qa_session_release(m->options.services.session, drop, e))
            return false;
    }
    p->flag = o->actor;
    p->stats.flag_since_ns = v->value.time_ns;
    o->value.phase = QA_OBJECTIVE_CARRIED;
    o->value.carrier = actor;
    o->value.deadline_ns = 0;
    o->expire_ns = 0;
    o->physics.motion = QA_PHYSICS_STATIONARY;
    if (source == QA_MODE_TEAM_ARENA && !points(m, v, actor, 10, e))
        return false;
    if (!mode_object_hide(m, o, source > QA_MODE_Q1_HORDE, e))
        return false;
    *accepted = true;
    return mode_event(m, v, QA_MODE_FLAG_TAKEN, actor, (qa_actor_id){0}, o->actor, team, 0, 0, e) &&
           mode_object_notify(m, v, o, e);
}
static bool award_defense(qa_modes *m, mode_instance *v, mode_member *p, qa_actor_id victim,
                          int amount, bool carrier, qa_error *e) {
    if (carrier)
        mode_stat_add(v, &p->stats.carrier_defenses, 1);
    else
        mode_stat_add(v, &p->stats.defenses, 1);
    p->stats.defended = true;
    p->stats.defended_ns = v->value.time_ns;
    return points(m, v, p->actor, amount, e) &&
           mode_event(m, v, QA_MODE_AWARD, p->actor, victim, (qa_actor_id){0}, p->last_team, amount,
                      0x10000, e);
}
static bool near_center(qa_modes *m, qa_actor_id center_actor, qa_actor_id point_actor,
                        float radius) {
    qa_body_state center, point;
    if (!qa_world_body_read(m->options.services.world, center_actor, &center, NULL) ||
        !qa_world_body_read(m->options.services.world, point_actor, &point, NULL))
        return false;
    qa_vec3 delta = qa_vec_sub(
        qa_vec_add(center.origin,
                   qa_vec_scale(qa_vec_add(center.bounds.mins, center.bounds.maxs), .5f)),
        point.origin);
    return qa_vec_dot(delta, delta) <= radius * radius;
}
static bool threewave_defense(qa_modes *m, mode_instance *v, mode_member *killer, mode_member *dead,
                              qa_team_id team, int index, qa_error *e) {
    bool carrier_bonus = false, flag_bonus = false;
    if (recent(v->value.time_ns, dead->stats.hurt_carrier_ns, 4, dead->stats.hurt_carrier) &&
        !killer->flag.registry) {
        if (!award_defense(m, v, killer, dead->actor, 2, true, e))
            return false;
        carrier_bonus = true;
    }
    qa_actor_id centers[] = {killer->actor, dead->actor};
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t ordinal = 0; ordinal < m->players_order.count && !carrier_bonus; ++ordinal) {
            qa_actor_id actor = m->players_order.ids[ordinal];
            mode_member *member = mode_member_get(m, v, actor);
            mode_player *player = mode_player_get(m, actor);
            qa_team_id current;
            if (!member || !player || player->value.spectator ||
                qa_actor_id_equal(actor, killer->actor) || !member->flag.registry ||
                !qa_modes_team(m, actor, &current, NULL) || current != team ||
                !near_center(m, actor, centers[pass], 550))
                continue;
            if (!award_defense(m, v, killer, dead->actor, 1, true, e))
                return false;
            carrier_bonus = true;
        }
        mode_object *base = mode_object_get(m, v->bases[index]);
        if ((!flag_bonus || (index == 0 && pass == 1)) && base && base->value.visible &&
            base->value.phase != QA_OBJECTIVE_CARRIED &&
            near_center(m, base->actor, centers[pass], 550)) {
            if (!award_defense(m, v, killer, dead->actor, 1, false, e))
                return false;
            flag_bonus = true;
        }
    }
    return true;
}
static bool rogue_defense(qa_modes *m, mode_instance *v, mode_member *killer, mode_member *dead,
                          qa_team_id team, qa_error *e) {
    bool carrier_bonus = false, flag_bonus = false;
    if (recent(v->value.time_ns, dead->stats.hurt_carrier_ns, 4, dead->stats.hurt_carrier) &&
        !killer->flag.registry) {
        if (!award_defense(m, v, killer, dead->actor, 2, true, e))
            return false;
        carrier_bonus = true;
    }
    qa_actor_id centers[] = {killer->actor, dead->actor};
    for (size_t pass = 0; pass < 2; ++pass)
        for (size_t i = m->observations.count; i > 0; --i) {
            qa_actor_id actor = m->observations.ids[i - 1];
            if (!near_center(m, actor, centers[pass], 400))
                continue;
            mode_member *p = mode_member_get(m, v, actor);
            qa_team_id current;
            if (p && p->flag.registry && !carrier_bonus &&
                !qa_actor_id_equal(actor, killer->actor) &&
                qa_modes_team(m, actor, &current, NULL) && current == team) {
                if (!award_defense(m, v, killer, dead->actor, 1, true, e))
                    return false;
                carrier_bonus = true;
            }
            mode_object *flag = mode_object_get(m, actor);
            if (flag && flag->mode.slot == v->id.slot &&
                flag->mode.generation == v->id.generation &&
                flag->spec.kind == QA_MODE_OBJECT_FLAG &&
                ((flag->spec.team == team &&
                  (team == v->value.rules.teams[0] || team == v->value.rules.teams[1])) ||
                 (!flag->spec.team && (!pass || !flag_bonus)))) {
                if (!award_defense(m, v, killer, dead->actor, 1, false, e))
                    return false;
                flag_bonus = true;
            }
        }
    return true;
}
bool mode_flag_bonus(qa_modes *m, mode_instance *v, qa_actor_id attacker, qa_actor_id victim,
                     qa_error *e) {
    mode_member *killer = mode_member_get(m, v, attacker), *dead = mode_member_get(m, v, victim);
    if (!killer || !dead || qa_actor_id_equal(attacker, victim) ||
        (v->value.rules.source != QA_MODE_ROGUE && qa_modes_same_team(m, attacker, victim)))
        return true;
    qa_team_id team;
    if (!qa_modes_team(m, attacker, &team, e))
        return false;
    int index = mode_team_index(v, team);
    if (index < 0)
        return true;
    qa_mode_source source = v->value.rules.source;
    uint64_t now = v->value.time_ns;
    bool victim_carrier = mode_object_get(m, dead->flag) != NULL;
    bool lm = source == QA_MODE_LMCTF, q1 = source <= QA_MODE_Q1_HORDE,
         ta = source == QA_MODE_TEAM_ARENA;
    int carrier_points = ta ? 20 : 2;
    if (v->value.rules.kind == QA_MODE_HARVESTER && dead->stats.tokens > 0) {
        int64_t amount = (int64_t)dead->stats.tokens * dead->stats.tokens * 20;
        return points(m, v, attacker, amount > INT32_MAX ? INT32_MAX : (int32_t)amount, e);
    }
    if (victim_carrier && !lm && !qa_modes_same_team(m, attacker, victim)) {
        killer->stats.carrier_killed = true;
        killer->stats.carrier_killed_ns = now;
        if ((!q1 || now - dead->stats.flag_since_ns >= 2 * MODE_SECOND) &&
            !points(m, v, attacker, carrier_points, e))
            return false;
        if (!q1) {
            for (uint32_t i = 0; i < m->actor_capacity; ++i) {
                qa_team_id t;
                mode_member *p = &v->members[i];
                if (p->joined && qa_modes_team(m, p->actor, &t, NULL) && t == team)
                    p->stats.hurt_carrier = false;
            }
            return true;
        }
    }
    if (source == QA_MODE_THREEWAVE)
        return threewave_defense(m, v, killer, dead, team, index, e);
    if (source == QA_MODE_ROGUE)
        return rogue_defense(m, v, killer, dead, team, e);
    unsigned danger_window = lm ? 2 : q1 ? 4 : 8;
    bool danger =
        recent(now, dead->stats.hurt_carrier_ns, danger_window, dead->stats.hurt_carrier) &&
        !killer->flag.registry;
    mode_object *base =
        mode_object_get(m, v->bases[v->value.rules.kind == QA_MODE_HARVESTER ? 2 : index]);
    qa_actor_id base_actor = base ? base->actor : (qa_actor_id){0};
    float radius = q1 ? (source == QA_MODE_ROGUE ? 400 : 550) : source >= QA_MODE_Q3 ? 1000 : 400;
    bool near_base = base && (mode_near(m, attacker, base_actor, radius) ||
                              mode_near(m, victim, base_actor, radius));
    if (source >= QA_MODE_Q3)
        near_base = base && ((mode_near(m, attacker, base_actor, radius) &&
                              mode_visible(m, attacker, base_actor, true)) ||
                             (mode_near(m, victim, base_actor, radius) &&
                              mode_visible(m, victim, base_actor, true)));
    if (source == QA_MODE_Q2_CTF)
        near_base = near_base || (base && (mode_visible(m, attacker, base_actor, false) ||
                                           mode_visible(m, victim, base_actor, false)));
    if (!lm && danger) {
        dead->stats.hurt_carrier = false;
        if (!award_defense(m, v, killer, victim, ta ? 5 : 2, true, e))
            return false;
        if (!q1)
            return true;
    }
    if (lm && base) {
        bool home = base->value.phase == QA_OBJECTIVE_HOME;
        float r = home ? 800 : 600;
        qa_body_state a, b;
        bool home_near = qa_world_body_read(m->options.services.world, attacker, &a, NULL) &&
                         qa_world_body_read(m->options.services.world, victim, &b, NULL) &&
                         (qa_vec_length(qa_vec_sub(a.origin, base->home)) < r ||
                          qa_vec_length(qa_vec_sub(b.origin, base->home)) < r);
        if (home_near && !award_defense(m, v, killer, victim, home ? 2 : 1, false, e))
            return false;
        if (!home &&
            (mode_near(m, attacker, base_actor, 400) || mode_near(m, victim, base_actor, 400))) {
            killer->stats.defended = true;
            killer->stats.defended_ns = now;
        }
    } else if (near_base) {
        if (!award_defense(m, v, killer, victim, ta ? 10 : 1, false, e))
            return false;
        if (!q1)
            return true;
    }
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        qa_team_id t;
        if (!p->joined || !p->flag.registry || qa_actor_id_equal(p->actor, attacker) ||
            !qa_modes_team(m, p->actor, &t, NULL) || t != team)
            continue;
        bool near = mode_near(m, attacker, p->actor, lm ? 500 : radius) ||
                    mode_near(m, victim, p->actor, lm ? 500 : radius);
        if (source == QA_MODE_Q2_CTF)
            near = near || mode_visible(m, attacker, p->actor, false) ||
                   mode_visible(m, victim, p->actor, false);
        if (source >= QA_MODE_Q3)
            near = (mode_near(m, attacker, p->actor, radius) &&
                    mode_visible(m, attacker, p->actor, true)) ||
                   (base && mode_near(m, attacker, base_actor, radius) &&
                    mode_visible(m, victim, p->actor, true));
        if (lm && danger) {
            if (!award_defense(m, v, killer, victim, 3, true, e))
                return false;
        } else if (near && !award_defense(m, v, killer, victim, lm ? 2 : ta ? 2 : 1, true, e))
            return false;
        break;
    }
    if (lm && victim_carrier) {
        killer->stats.carrier_killed = true;
        killer->stats.carrier_killed_ns = now;
        if (!points(m, v, attacker, 2, e))
            return false;
    }
    return true;
}
