#include "internal.h"
#include "qa/modes_q1_source.h"

bool qa_modes_spawnpoints(qa_modes *m, qa_mode_id id, const qa_mode_spawnpoint *points,
                          size_t count, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || m->callback_depth || (count && !points) || count > SIZE_MAX / sizeof(*points))
        return mode_fail(e, "invalid mode spawnpoints");
    for (size_t i = 0; i < count; ++i)
        if (!qa_vec_finite(points[i].origin) || !qa_vec_finite(points[i].angles))
            return mode_fail(e, "nonfinite mode spawnpoint");
    qa_mode_spawnpoint *copy = count ? malloc(count * sizeof(*copy)) : NULL;
    if (count && !copy) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating mode spawnpoints");
        return false;
    }
    if (count)
        memcpy(copy, points, count * sizeof(*copy));
    free(v->spawns);
    v->spawns = copy;
    v->spawn_count = count;
    return true;
}
static float distance_to_players(qa_modes *m, mode_instance *v, qa_vec3 point, qa_actor_id except) {
    float best = 9999999.0f * 9999999.0f;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (!p || member->player.spectator || qa_actor_id_equal(p->value.actor, except) ||
            !mode_alive(m, p->value.actor))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(m->options.services.world, p->value.actor, &body, NULL))
            continue;
        qa_vec3 delta = qa_vec_sub(body.origin, point);
        float distance = qa_vec_dot(delta, delta);
        if (distance < best)
            best = distance;
    }
    return best;
}
static bool classname(qa_modes *m, const qa_mode_spawnpoint *p, const char *name) {
    const char *actual =
        qa_strings_cstr(qa_session_strings(m->options.services.session), p->classname);
    return actual && !strcmp(actual, name);
}
static bool spawn_group(const qa_mode_spawnpoint *p, qa_team_id team) { return p->team == team; }
static size_t q2_choose(qa_modes *m, mode_instance *v, qa_team_id team, bool farthest,
                        bool exact_two) {
    size_t first = SIZE_MAX, second = SIZE_MAX, best = SIZE_MAX, fallback = SIZE_MAX, count = 0;
    float first_range = 99999.0f * 99999.0f, second_range = first_range, farthest_range = 0;
    for (size_t i = 0; i < v->spawn_count; ++i) {
        const qa_mode_spawnpoint *p = &v->spawns[i];
        if (!spawn_group(p, team) || (!team && !classname(m, p, "info_player_deathmatch")))
            continue;
        if (fallback == SIZE_MAX)
            fallback = i;
        float range = distance_to_players(m, v, p->origin, (qa_actor_id){0});
        ++count;
        if (range > farthest_range) {
            farthest_range = range;
            best = i;
        }
        if (range < first_range) {
            if (exact_two) {
                second = first;
                second_range = first_range;
            }
            first_range = range;
            first = i;
        } else if (range < second_range) {
            second_range = range;
            second = i;
        }
    }
    if (farthest)
        return best == SIZE_MAX ? fallback : best;
    size_t choices = count > 2 ? count - 2 : count;
    if (!choices)
        return SIZE_MAX;
    size_t selected = (size_t)(mode_random_float(m) * (float)choices);
    for (size_t i = 0; i < v->spawn_count; ++i) {
        const qa_mode_spawnpoint *p = &v->spawns[i];
        if (!spawn_group(p, team) || (!team && !classname(m, p, "info_player_deathmatch")))
            continue;
        if (count > 2 && (i == first || i == second))
            continue;
        if (!selected--)
            return i;
    }
    return SIZE_MAX;
}
static bool telefrag(qa_modes *m, qa_vec3 origin, bool *blocked, qa_error *e) {
    qa_builtin_snapshot_frame *frame =
        qa_builtin_snapshot_acquire(&m->snapshot_frames, m->actor_capacity, e);
    if (!frame) return false;
    size_t count;
    bool overflow;
    if (!qa_world_query(m->options.services.world,
                        qa_bounds_translate((qa_bounds){{-15, -15, -24}, {15, 15, 32}}, origin),
                        QA_COLLISION_SOLID, frame->snapshot.ids, frame->snapshot.capacity,
                        &count, &overflow, e)) {
        qa_builtin_snapshot_release(frame);
        return false;
    }
    if (overflow) {
        qa_builtin_snapshot_release(frame);
        return mode_fail(e, "spawn overlap exceeds source actor capacity");
    }
    *blocked = false;
    for (size_t i = 0; i < count; ++i) {
        qa_builtin_actor_traits traits = {0};
        if (m->options.services.actor_traits &&
            m->options.services.actor_traits(m->options.services.context,
                frame->snapshot.ids[i], &traits) &&
            traits.player) {
            *blocked = true;
            break;
        }
    }
    qa_builtin_snapshot_release(frame);
    return true;
}
static bool q3_choose(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_team_id team,
                      bool initial, bool filter_player, size_t *selected, qa_error *e) {
    size_t points[64], count = 0, fallback = SIZE_MAX;
    float distances[64];
    qa_body_state player = {0};
    if (actor.registry && !qa_world_body_read(m->options.services.world, actor, &player, e))
        return false;
    mode_player *roster = mode_player_get(m, actor);
    int team_index = mode_team_index(v, team);
    const char *native = team_index == 0 ? (initial ? "team_CTF_redplayer" : "team_CTF_redspawn")
                                         : (initial ? "team_CTF_blueplayer" : "team_CTF_bluespawn");
    bool native_team = false;
    if (team)
        for (size_t i = 0; i < v->spawn_count; ++i)
            if (classname(m, &v->spawns[i], native))
                native_team = true;
    for (size_t i = 0; i < v->spawn_count; ++i) {
        qa_mode_spawnpoint *p = &v->spawns[i];
        if (filter_player && roster && (roster->value.bot ? p->no_bots : p->no_humans))
            continue;
        if (team ? (p->team != team || (native_team && !classname(m, p, native)))
                 : (p->team || !classname(m, p, "info_player_deathmatch")))
            continue;
        if (fallback == SIZE_MAX)
            fallback = i;
        bool occupied;
        if (!telefrag(m, p->origin, &occupied, e))
            return false;
        if (occupied)
            continue;
        if (team) {
            points[count++] = i;
            if (count == 32)
                break;
            continue;
        }
        if (initial && (p->flags & 1u)) {
            *selected = i;
            return true;
        }
        float distance =
            qa_vec_length(qa_vec_sub(p->origin, initial ? (qa_vec3){0} : player.origin));
        size_t insertion = 0;
        while (insertion < count && distance <= distances[insertion])
            ++insertion;
        if (insertion == 64)
            continue;
        size_t end = count < 64 ? count : 63;
        for (size_t j = end; j > insertion; --j) {
            points[j] = points[j - 1];
            distances[j] = distances[j - 1];
        }
        points[insertion] = i;
        distances[insertion] = distance;
        if (count < 64)
            ++count;
    }
    if (count)
        *selected = points[team ? mode_random(m) % count
                                : (size_t)(mode_random_float(m) * (float)(count / 2))];
    else
        *selected = fallback;
    if (*selected == SIZE_MAX && team)
        return q3_choose(m, v, actor, 0, false, filter_player, selected, e);
    return true;
}
static bool q1_spawn_pose(qa_modes *m, const qa_mode_spawnpoint *point, bool physical,
    qa_mode_spawnpoint *out, qa_error *e) {
    *out = *point;
    if (!physical) return true;
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, point->actor, &body, e)) return false;
    out->origin = body.origin;
    out->angles = body.angles;
    return true;
}
static bool q1_spawnpoint(qa_modes *m, mode_instance *v, mode_member *member,
    qa_actor_id actor, bool physical, qa_mode_spawnpoint *out, bool *selected, qa_error *e) {
    *selected = false;
    for (size_t i = 0; i < v->spawn_count; ++i)
        if ((!physical || mode_live(m, v->spawns[i].actor)) &&
            classname(m, &v->spawns[i], "testplayerstart")) {
            if (!q1_spawn_pose(m, &v->spawns[i], physical, out, e)) return false;
            *selected = true;
            return true;
        }
    qa_team_id team = 0;
    double killed = member ? member->spawn_state : 0;
    int side;
    if (physical && v->value.origin == QA_MODE_NATIVE_Q1_COMPOSITION) {
        qa_mode_id id = v->id;
        if (v->value.rules.source == QA_MODE_ROGUE) {
            double source_team;
            if (!qa_modes_q1_rogue_initialize(m, id, actor, e) ||
                !qa_modes_q1_source_read(m, id, actor, QA_Q1_ROGUE_KILLED, &killed, e) ||
                !qa_modes_q1_source_read(m, id, actor, QA_Q1_ROGUE_STEAM, &source_team, e))
                return false;
            side = source_team == 5 ? 0 : source_team == 14 ? 1 : -1;
        } else {
            qa_combat_state combat;
            if (!qa_modes_q1_source_read(m, id, actor, QA_Q1_CTF_KILLED, &killed, e) ||
                !qa_combat_read_traits(m->options.services.combat, actor, &combat, e))
                return false;
            team = combat.team;
            side = team == v->value.rules.teams[0] ? 0 :
                team == v->value.rules.teams[1] ? 1 : -1;
        }
        bool observer;
        if (!mode_q1_source_current(m, v, actor, &observer, e)) return false;
        if (mode_get(m, id) != v || mode_member_get(m, v, actor) != member)
            return mode_fail(e, "Q1 source spawn participant changed during source reads");
        if (killed != 0) side = -1;
    } else {
        if (member) {
            if (!qa_modes_team(m, v->id, actor, &team, e)) return false;
            if (mode_get(m, v->id) != v || mode_member_get(m, v, actor) != member || !mode_live(m, actor))
                return mode_fail(e, "Q1 source spawn participant retired during team read");
        }
        side = !member || killed == 0 ? mode_team_index(v, team) : -1;
    }
    size_t cursor = side == 0 ? 0 : side == 1 ? 1 : 2;
    if (v->value.rules.source == QA_MODE_THREEWAVE && v->value.rules.start_map &&
        member && killed != 0) cursor = 3;
    const char *wanted = cursor == 0 ? "info_player_team1" : cursor == 1 ? "info_player_team2" :
        cursor == 2 ? "info_player_deathmatch" : "info_vote_destination";
    size_t last = SIZE_MAX;
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (qa_actor_id_equal(v->spawns[i].actor, v->last_spawns[cursor]) &&
            classname(m, &v->spawns[i], wanted)) last = i;
    for (size_t step = 1; step <= v->spawn_count; ++step) {
        size_t index = last == SIZE_MAX ? step - 1 :
            step < v->spawn_count - last ? last + step : step - (v->spawn_count - last);
        const qa_mode_spawnpoint *point = &v->spawns[index];
        if ((physical && !mode_live(m, point->actor)) || !classname(m, point, wanted)) continue;
        qa_mode_spawnpoint pose;
        if (!q1_spawn_pose(m, point, physical, &pose, e)) return false;
        bool occupied = false;
        if (v->value.rules.source == QA_MODE_ROGUE && index != last)
            for (size_t j = 0; j < m->players_order.count; ++j) {
                if (physical && v->value.origin == QA_MODE_NATIVE_Q1_COMPOSITION &&
                    !mode_member_get(m, v, m->players_order.ids[j])) continue;
                qa_body_state body;
                qa_error read_error = {0};
                if (!qa_world_body_read(m->options.services.world, m->players_order.ids[j],
                    &body, &read_error)) {
                    if (read_error.code == QA_ERROR_NOT_FOUND) continue;
                    if (e) *e = read_error;
                    return false;
                }
                qa_vec3 center = qa_vec_add(body.origin,
                    qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
                if (qa_vec_length(qa_vec_sub(center, pose.origin)) <= 32) {
                    occupied = true;
                    break;
                }
            }
        if (occupied) continue;
        v->last_spawns[cursor] = point->actor;
        *out = pose;
        *selected = true;
        return true;
    }
    if (v->value.rules.source == QA_MODE_THREEWAVE)
        for (unsigned pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < v->spawn_count; ++i)
                if ((!physical || mode_live(m, v->spawns[i].actor)) && classname(m, &v->spawns[i],
                    pass == 0 ? "info_player_deathmatch" : "info_player_start")) {
                    if (!q1_spawn_pose(m, &v->spawns[i], physical, out, e)) return false;
                    *selected = true;
                    return true;
                }
    return true;
}

bool qa_modes_q1_spawnpoint(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_spawnpoint *out, bool *selected, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!v || !member || !out || !selected || !mode_live(m, actor) ||
        (v->value.rules.source != QA_MODE_THREEWAVE && v->value.rules.source != QA_MODE_ROGUE))
        return mode_fail(e, "Q1 source spawn has no actual expansion participant");
    return q1_spawnpoint(m, v, member, actor, true, out, selected, e);
}

bool qa_modes_q3_deathmatch_spawnpoint(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_mode_spawnpoint *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_player *player = mode_player_get(m, actor);
    if (!v || !player || !out || !v->spawn_count || m->callback_depth == UINT_MAX)
        return mode_fail(e, "Q3 teleporter has no current player or deathmatch spawn roster");
    size_t selected = SIZE_MAX;
    if (!MODE_CALLBACK(m, q3_choose(m, v, actor, 0, false, false, &selected, e))) return false;
    if (mode_get(m, id) != v || mode_player_get(m, actor) != player)
        return mode_fail(e, "Q3 teleporter player retired during deathmatch selection");
    if (selected == SIZE_MAX)
        return mode_fail(e, "Q3 teleporter has no deathmatch spawnpoint");
    *out = v->spawns[selected];
    out->origin.z += 9;
    return true;
}

bool qa_modes_spawnpoint(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool farthest,
                         qa_mode_spawnpoint *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !out || !v->spawn_count)
        return mode_fail(e, "mode has no spawnpoints");
    qa_team_id team = 0;
    if (mode_player_get(m, actor) && !qa_modes_team(m, v->id, actor, &team, e))
        return false;
    mode_member *member = mode_member_get(m, v, actor);
    bool initial = !member || !member->spawn_state;
    qa_mode_source source = v->value.rules.source;
    size_t selected = SIZE_MAX;
    if (source >= QA_MODE_Q3) {
        if (!q3_choose(m, v, actor, team, initial, true, &selected, e))
            return false;
    } else if (source == QA_MODE_THREEWAVE || source == QA_MODE_ROGUE) {
        bool found;
        if (!q1_spawnpoint(m, v, member, actor, false, out, &found, e)) return false;
        if (found) return true;
    } else if (source == QA_MODE_LMCTF) {
        size_t best_team = q2_choose(m, v, team, true, true);
        if (initial)
            selected = best_team;
        if (selected == SIZE_MAX) {
            size_t dm = q2_choose(m, v, 0, farthest, false);
            selected = dm;
            if (best_team != SIZE_MAX &&
                (dm == SIZE_MAX ||
                 distance_to_players(m, v, v->spawns[best_team].origin, (qa_actor_id){0}) >=
                     distance_to_players(m, v, v->spawns[dm].origin, (qa_actor_id){0})))
                selected = best_team;
        }
    } else {
        if (team && (initial || v->value.rules.kind == QA_MODE_DEATHBALL))
            selected = q2_choose(m, v, team, v->value.rules.kind == QA_MODE_DEATHBALL, true);
        if (selected == SIZE_MAX)
            selected = q2_choose(m, v, 0, farthest, false);
    }
    if (selected == SIZE_MAX && source == QA_MODE_LMCTF)
        for (int i = 0; i < 2; ++i) {
            mode_object *flag = mode_object_get(m, v->bases[i]);
            if (!flag)
                continue;
            qa_body_state body;
            if (!qa_world_body_read(m->options.services.world, flag->actor, &body, e))
                return false;
            *out = (qa_mode_spawnpoint){.actor = flag->actor,
                                        .origin = body.origin,
                                        .angles = body.angles,
                                        .team = flag->spec.team};
            out->origin.z += 9;
            if (member)
                member->spawn_state = 1;
            return true;
        }
    if (selected == SIZE_MAX)
        for (size_t i = 0; i < v->spawn_count; ++i)
            if (classname(m, &v->spawns[i], "info_player_deathmatch") ||
                classname(m, &v->spawns[i], "info_player_start")) {
                selected = i;
                break;
            }
    if (selected == SIZE_MAX)
        return mode_fail(e, "mode has no applicable player spawn");
    *out = v->spawns[selected];
    if (member && v->value.rules.source >= QA_MODE_Q2)
        member->spawn_state = 1;
    if (actor.registry)
        out->origin.z += source >= QA_MODE_Q3                                                  ? 9
                         : source == QA_MODE_LMCTF || v->value.rules.kind == QA_MODE_DEATHBALL ? 9
                         : source == QA_MODE_Q2_CTF                                            ? 10
                                                                                               : 0;
    return true;
}
