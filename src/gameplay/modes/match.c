#include "internal.h"

static int rank_compare(const void *left, const void *right) {
    const mode_rank_entry *a = left, *b = right;
    if (a->group != b->group)
        return a->group < b->group ? -1 : 1;
    if (a->group == 1) {
        if (a->q3_source && b->q3_source && a->q3_spectator_time != b->q3_spectator_time)
            return a->q3_spectator_time < b->q3_spectator_time ? -1 : 1;
        if (!a->q3_source && a->spectator_since != b->spectator_since)
            return a->spectator_since < b->spectator_since ? -1 : 1;
    }
    if (!a->group && a->score != b->score)
        return a->score > b->score ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}
bool qa_modes_rank(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "stale ranked mode");
    bool q3 = v->value.rules.source >= QA_MODE_Q3;
    qa_actor_owner native_owner;
    bool native = q3 && m->options.hooks.q3_native_source &&
        m->options.hooks.q3_native_source(m->options.hooks.context, id, &native_owner);
    qa_mode_q3_rank_counts source_counts = {0};
    if (native && (!m->options.hooks.q3_rank_counts ||
        !MODE_CALLBACK(m, m->options.hooks.q3_rank_counts(m->options.hooks.context,
            id, &source_counts, e)))) return false;
    size_t n = 0, playing = 0, voting = 0;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (!p)
            continue;
        qa_match_player value = p->value;
        int32_t source_team = -1;
        if (native && (!m->options.hooks.q3_rank_client ||
            !MODE_CALLBACK(m, m->options.hooks.q3_rank_client(m->options.hooks.context,
                id, value.actor, &value.connected, &value.connecting, &value.bot, &source_team, e))))
            return false;
        if (!value.connected) continue;
        int32_t score;
        if (!qa_modes_score(m, v->id, member->actor, &score, e))
            return false;
        if (!mode_player_get(m, member->actor))
            continue;
        const qa_mode_player_state *state = &member->player;
        uint32_t source_order = (uint32_t)ordinal;
        if (native) {
            qa_actor_owner owner;
            if (!m->options.hooks.q3_client_slot ||
                !MODE_CALLBACK(m, m->options.hooks.q3_client_slot(m->options.hooks.context,
                    id, value.actor, &owner, &source_order, e)) ||
                owner != native_owner || source_order >= 64)
                return mode_fail(e, "native Q3 ranks lost their physical source client");
        }
        uint8_t group = (q3 ? state->q3_spectator_state == QA_MODE_Q3_SPECTATOR_SCOREBOARD ||
                              state->q3_spectator_client < 0 : state->scoreboard)
            ? 3 : value.connecting ? 2 : state->spectator ? 1 : 0;
        v->ranks[n++] = (mode_rank_entry){.actor = value.actor, .score = score,
            .spectator_since = state->spectator_since_ns,
            .q3_spectator_time = state->q3_spectator_time_ms, .q3_source = q3,
            .order = source_order, .group = group};
        if (!state->spectator && !value.connecting) {
            ++playing;
            if (!value.bot)
                ++voting;
        }
    }
    qsort(v->ranks, n, sizeof(*v->ranks), rank_compare);
    if (native) {
        if (n != (size_t)source_counts.connected)
            return mode_fail(e, "native Q3 ranks differ from the actual fixed source roster");
        playing = (size_t)source_counts.playing;
        voting = (size_t)source_counts.voting;
    }
    v->value.playing = playing;
    v->value.voting = voting;
    v->value.sorted_count = n;
    int32_t rank = 0;
    for (size_t i = 0; i < n; ++i) {
        mode_rank_entry *entry = &v->ranks[i];
        v->sorted[i] = entry->actor;
        mode_member *member = mode_member_get(m, v, entry->actor);
        if (!member)
            continue;
        if (v->value.rules.source >= QA_MODE_Q3 && v->value.rules.kind >= QA_MODE_TEAM_DEATHMATCH &&
            v->value.rules.kind <= QA_MODE_HARVESTER) {
            member->stats.rank = v->value.team_scores[0] == v->value.team_scores[1]  ? 2
                                 : v->value.team_scores[0] > v->value.team_scores[1] ? 0
                                                                                     : 1;
        } else {
            if (i == 0 || entry->score != v->ranks[i - 1].score)
                rank = (int32_t)i;
            member->stats.rank = rank;
            if (i && entry->score == v->ranks[i - 1].score && !entry->group) {
                member->stats.rank |= 0x4000;
                v->members[v->ranks[i - 1].actor.slot].stats.rank |= 0x4000;
            }
            if (v->value.rules.kind == QA_MODE_SINGLE_PLAYER && playing == 1)
                member->stats.rank |= 0x4000;
        }
    }
    return true;
}
bool qa_modes_sorted(qa_modes *m, qa_mode_id id, qa_actor_id *out, size_t capacity, size_t *count,
                     qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !count || (capacity && !out))
        return mode_fail(e, "invalid ranked output");
    *count = v->value.sorted_count;
    if (capacity < *count)
        return mode_fail(e, "rank output too small");
    if (*count)
        memcpy(out, v->sorted, *count * sizeof(*out));
    return true;
}
bool mode_set_phase(qa_modes *m, mode_instance *v, qa_mode_phase phase, uint64_t deadline,
                    qa_error *e) {
    v->value.phase = phase;
    v->value.deadline_ns = deadline;
    v->countdown_announced = false;
    return mode_event(m, v, QA_MODE_PHASE, (qa_actor_id){0}, (qa_actor_id){0}, (qa_actor_id){0}, 0,
                      phase, 0, e);
}
bool qa_modes_q3_source_phase(qa_modes *m, qa_mode_id id, qa_mode_phase phase,
                             uint64_t source_time_ns, uint64_t deadline_ns, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || m->source_restored ||
        v->value.rules.source < QA_MODE_Q3 ||
        v->value.rules.source > QA_MODE_TEAM_ARENA ||
        v->value.rules.kind < QA_MODE_FFA ||
        v->value.rules.kind > QA_MODE_HARVESTER ||
        phase < QA_MODE_WAITING || phase > QA_MODE_FINISHED ||
        source_time_ns < v->value.time_ns)
        return mode_fail(e, "invalid selected Q3 source phase effect");
    v->value.time_ns = source_time_ns;
    return mode_set_phase(m, v, phase, deadline_ns, e);
}
bool qa_modes_q3_source_reset_teams(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || m->source_restored ||
        v->value.rules.source < QA_MODE_Q3 ||
        v->value.rules.source > QA_MODE_TEAM_ARENA ||
        v->value.rules.kind < QA_MODE_FFA || v->value.rules.kind > QA_MODE_HARVESTER)
        return mode_fail(e, "invalid selected Q3 source team reset");
    v->value.team_scores[0] = v->value.team_scores[1] = 0;
    return true;
}
static size_t playing_team(qa_modes *m, mode_instance *v, qa_team_id team) {
    size_t count = 0;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        qa_team_id current;
        if (player && player->value.connected && !player->value.connecting &&
            !p->player.spectator && qa_modes_team(m, v->id, p->actor, &current, NULL) &&
            current == team)
            ++count;
    }
    return count;
}
bool qa_modes_ready(qa_modes *m, qa_mode_id id, qa_actor_id actor, bool ready, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    mode_player *p = mode_player_get(m, actor);
    if (!member || !p)
        return mode_fail(e, "unknown ready player");
    member->player.ready = ready;
    if (v->value.rules.source == QA_MODE_Q2_CTF && v->value.rules.competition > 1) {
        if (!ready && v->value.phase == QA_MODE_COUNTDOWN)
            return mode_set_phase(
                m, v, QA_MODE_SETUP,
                v->value.time_ns + (uint64_t)v->value.rules.setup_seconds * MODE_SECOND, e);
        if (v->value.phase != QA_MODE_SETUP || !ready ||
            !playing_team(m, v, v->value.rules.teams[0]) ||
            !playing_team(m, v, v->value.rules.teams[1]))
            return true;
        for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
            uint32_t i = m->players_order.ids[ordinal].slot;
            mode_member *a = &v->members[i];
            mode_player *x = a->joined ? mode_player_get(m, a->actor) : NULL;
            if (x && !a->player.spectator && !a->player.ready)
                return true;
        }
        return mode_set_phase(
            m, v, QA_MODE_COUNTDOWN,
            v->value.time_ns + (uint64_t)v->value.rules.countdown_seconds * MODE_SECOND, e);
    }
    return true;
}
static bool ghost_assign(qa_modes *m, mode_instance *v, mode_member *p, qa_error *e) {
    uint32_t code = 10000u + mode_random(m) % 90000u;
    for (uint32_t attempts = 0; attempts < 90000; ++attempts) {
        bool occupied = false;
        for (uint32_t i = 0; i < m->actor_capacity; ++i)
            if (v->ghosts[i].code == code) {
                occupied = true;
                break;
            }
        if (!occupied)
            break;
        code = code == 99999 ? 10000 : code + 1;
        if (attempts == 89999)
            return mode_fail(e, "ghost code space exhausted");
    }
    qa_team_id team;
    int32_t score;
    if (!qa_modes_team(m, v->id, p->actor, &team, e) || !qa_modes_score(m, v->id, p->actor, &score, e))
        return false;
    mode_player *player = mode_player_get(m, p->actor);
    if (!player)
        return true;
    p->ghost_code = code;
    v->ghosts[p->actor.slot] = (mode_ghost){.actor = p->actor,
                                            .name = player->value.name,
                                            .team = team,
                                            .stats = p->stats,
                                            .code = code,
                                            .score = score};
    return mode_event(m, v, QA_MODE_GHOST_CODE, p->actor, (qa_actor_id){0}, (qa_actor_id){0}, team,
                      (int32_t)code, 0, e);
}
static bool begin_play(qa_modes *m, mode_instance *v, qa_error *e) {
    bool reset = v->value.rules.source == QA_MODE_Q2_CTF || v->value.rules.source == QA_MODE_LMCTF;
    v->value.started_ns = v->value.time_ns;
    v->restart_sent = false;
    v->remaining_seconds = v->value.rules.source == QA_MODE_LMCTF
                               ? (int32_t)v->value.rules.time_limit_minutes * 60
                               : v->value.rules.match_seconds;
    if (reset) {
        memset(v->value.team_scores, 0, sizeof(v->value.team_scores));
        memset(v->value.team_captures, 0, sizeof(v->value.team_captures));
        memset(v->ghosts, 0, m->actor_capacity * sizeof(*v->ghosts));
    }
    if (!mode_set_phase(m, v, QA_MODE_PLAYING,
                        reset && v->remaining_seconds > 0
                            ? v->value.time_ns + (uint64_t)v->remaining_seconds * MODE_SECOND
                            : 0,
                        e))
        return false;
    if (v->value.rules.auto_lock)
        v->value.rules.match_lock = true;
    for (size_t ordinal = 0; reset && ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        if (!player)
            continue;
        if (!qa_modes_drop(m, v->id, p->actor, true, e) ||
            !qa_modes_set_score(m, v->id, p->actor, 0, e))
            return false;
        p->stats = (qa_mode_statistics){0};
        p->player.ready = false;
        p->spawn_state = 0;
        if (p->player.spectator)
            continue;
        if (v->value.rules.source == QA_MODE_Q2_CTF) {
            if (!ghost_assign(m, v, p, e))
                return false;
            p->respawn_ns =
                v->value.time_ns + MODE_SECOND + (mode_random(m) % 30u) * (MODE_SECOND / 10);
            if (m->options.hooks.spectator &&
                !m->options.hooks.spectator(m->options.hooks.context, v->id, p->actor, true, e))
                return false;
        } else if (m->options.hooks.respawn &&
                   !m->options.hooks.respawn(m->options.hooks.context, v->id, p->actor, false, e))
            return false;
    }
    return qa_modes_rank(m, v->id, e);
}
bool qa_modes_start(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "stale match start");
    v->ready_since_ns = 0;
    v->value.ready_exit = false;
    if (v->value.rules.source == QA_MODE_LMCTF) {
        v->remaining_seconds = v->value.rules.countdown_seconds;
        v->next_second_ns = v->value.time_ns + MODE_SECOND;
        if (v->value.rules.auto_lock)
            v->value.rules.match_lock = true;
    }
    if (v->value.rules.source == QA_MODE_LMCTF || v->value.rules.source == QA_MODE_Q2_CTF)
        return mode_set_phase(
            m, v, QA_MODE_COUNTDOWN,
            v->value.time_ns + (uint64_t)v->value.rules.countdown_seconds * MODE_SECOND, e);
    return begin_play(m, v, e);
}
bool qa_modes_cancel(qa_modes *m, qa_mode_id id, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "stale match cancel");
    v->ready_since_ns = 0;
    v->remaining_seconds = 0;
    v->restart_sent = false;
    v->value.rules.paused = false;
    if (v->value.rules.auto_lock)
        v->value.rules.match_lock = false;
    return mode_set_phase(
        m, v, v->value.rules.source == QA_MODE_LMCTF ? QA_MODE_WAITING : QA_MODE_SETUP,
        v->value.time_ns + (uint64_t)v->value.rules.setup_seconds * MODE_SECOND, e);
}
bool qa_modes_end(qa_modes *m, qa_mode_id id, qa_string_id reason, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v)
        return mode_fail(e, "stale match end");
    qa_actor_owner native_owner;
    if (v->value.rules.source >= QA_MODE_Q3 && m->options.hooks.q3_native_source &&
        m->options.hooks.q3_native_source(m->options.hooks.context, id, &native_owner)) {
        if (!m->options.hooks.q3_source_match_exit)
            return mode_fail(e, "native Q3 match end has no real source exit service");
        return MODE_CALLBACK(m, m->options.hooks.q3_source_match_exit(
            m->options.hooks.context, id, reason, e));
    }
    if (v->value.phase >= QA_MODE_EXIT_PENDING)
        return true;
    if (v->value.rules.source == QA_MODE_THREEWAVE)
        v->value.ctf_pregame_over = true;
    qa_mode_event event = {
        .kind = QA_MODE_MESSAGE, .mode = id, .text = reason, .time_ns = v->value.time_ns};
    if (m->options.hooks.event &&
        !MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e)))
        return false;
    uint64_t delay =
        v->value.rules.source == QA_MODE_TEAM_ARENA && v->value.rules.single_player_active
            ? 5 * MODE_SECOND
            : MODE_SECOND;
    if (v->value.rules.source == QA_MODE_LMCTF)
        return mode_set_phase(m, v, QA_MODE_FINISHED, v->value.time_ns + 300 * MODE_SECOND, e);
    if (v->value.rules.source < QA_MODE_Q3) {
        if (!mode_set_phase(m, v, QA_MODE_FINISHED, 0, e))
            return false;
        v->restart_sent = true;
        return mode_intent(m, v, QA_MATCH_NEXT_MAP, (qa_actor_id){0}, 0, 0, e);
    }
    return mode_set_phase(m, v, QA_MODE_EXIT_PENDING, v->value.time_ns + delay, e);
}
static bool intermission(qa_modes *m, mode_instance *v, qa_error *e) {
    if (!mode_set_phase(m, v, QA_MODE_INTERMISSION, v->value.time_ns + 5 * MODE_SECOND, e))
        return false;
    v->ready_since_ns = 0;
    v->value.ready_exit = false;
    if (v->value.rules.kind == QA_MODE_DUEL && v->value.playing >= 2) {
        mode_member *winner = mode_member_get(m, v, v->sorted[0]),
                    *loser = mode_member_get(m, v, v->sorted[1]);
        if (winner && loser) {
            winner->player.wins = mode_add_i32(winner->player.wins, 1);
            loser->player.losses = mode_add_i32(loser->player.losses, 1);
        }
    }
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        if (!player)
            continue;
        p->player.ready = false;
        if (!mode_alive(m, p->actor) && m->options.hooks.respawn &&
            !m->options.hooks.respawn(m->options.hooks.context, v->id, p->actor, false, e))
            return false;
        if (m->options.hooks.intermission &&
            !m->options.hooks.intermission(m->options.hooks.context, v->id, p->actor, e))
            return false;
    }
    return true;
}
static bool exit_intermission(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->restart_sent)
        return true;
    v->restart_sent = true;
    if (v->value.rules.kind == QA_MODE_DUEL) {
        if (v->value.playing >= 2) {
            mode_member *loser = mode_member_get(m, v, v->sorted[1]);
            if (loser) {
                qa_actor_id actor = loser->actor;
                loser->player.spectator = true;
                loser->player.spectator_since_ns = v->value.time_ns;
                if (!mode_q3_session_team(m, v, actor, true, e))
                    return false;
                loser = mode_member_get(m, v, actor);
                if (!loser)
                    return mode_fail(e, "Q3 tournament loser retired during session transition");
                if (m->options.hooks.spectator &&
                    !m->options.hooks.spectator(m->options.hooks.context, v->id, actor, true,
                                                e))
                    return false;
            }
        }
        return mode_intent(m, v, QA_MATCH_RESTART_MAP, (qa_actor_id){0}, 0, 0, e);
    }
    return mode_intent(m, v, QA_MATCH_NEXT_MAP, (qa_actor_id){0}, 0, 0, e);
}
static bool tied(qa_modes *m, mode_instance *v) {
    if (v->value.playing < 2)
        return false;
    if (v->value.rules.kind >= QA_MODE_TEAM_DEATHMATCH && v->value.rules.kind <= QA_MODE_HARVESTER)
        return v->value.team_scores[0] == v->value.team_scores[1];
    int32_t a, b;
    return qa_modes_score(m, v->id, v->sorted[0], &a, NULL) && qa_modes_score(m, v->id, v->sorted[1], &b, NULL) &&
           a == b;
}
static bool duel_promote(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->value.playing >= 2)
        return true;
    mode_member *oldest = NULL;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (p && p->value.connected && !p->value.connecting && member->player.spectator &&
            (v->value.rules.source >= QA_MODE_Q3
                ? member->player.q3_spectator_state != QA_MODE_Q3_SPECTATOR_SCOREBOARD &&
                  member->player.q3_spectator_client >= 0
                : !member->player.scoreboard) &&
            (!oldest || (v->value.rules.source >= QA_MODE_Q3
                ? member->player.q3_spectator_time_ms < oldest->player.q3_spectator_time_ms
                : member->player.spectator_since_ns < oldest->player.spectator_since_ns)))
            oldest = member;
    }
    if (!oldest)
        return true;
    oldest->player.spectator = false;
    if (!mode_q3_session_team(m, v, oldest->actor, false, e))
        return false;
    if (m->options.hooks.spectator &&
        !m->options.hooks.spectator(m->options.hooks.context, v->id, oldest->actor, false, e))
        return false;
    if (m->options.hooks.respawn &&
        !m->options.hooks.respawn(m->options.hooks.context, v->id, oldest->actor, false, e))
        return false;
    return qa_modes_rank(m, v->id, e);
}
static bool lmctf_frame(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->value.phase == QA_MODE_WAITING || v->value.time_ns < v->next_second_ns)
        return true;
    v->next_second_ns = v->value.time_ns + MODE_SECOND;
    if (v->value.rules.paused)
        return true;
    if (v->value.phase == QA_MODE_COUNTDOWN) {
        if (v->remaining_seconds == 60 || v->remaining_seconds == 30 ||
            v->remaining_seconds == 15 || v->remaining_seconds == 10)
            if (!mode_event(m, v, QA_MODE_PHASE, (qa_actor_id){0}, (qa_actor_id){0},
                            (qa_actor_id){0}, 0, QA_MODE_COUNTDOWN, v->remaining_seconds, e))
                return false;
        if (v->remaining_seconds <= 0) {
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                qa_actor_id actor = m->players_order.ids[ordinal];
                mode_member *p = mode_member_get(m, v, actor);
                mode_player *player = mode_player_get(m, actor);
                if (!p || !player || p->player.spectator)
                    continue;
                qa_damage_request request = {
                    .target = actor,
                    .amount = 100000,
                    .attack = {.attacker = actor,
                               .inflictor = actor,
                               .cause = {.kind = QA_CAUSE_Q2,
                                         .source.q2 = {.means_of_death = 23,
                                                       .flags = 32,
                                                       .native = QA_Q2_CAUSE_CLASSIC}}}};
                if (!mode_damage(m, v, QA_GAME_Q2, &request, e))
                    return false;
                p = mode_member_get(m, v, actor);
                if (!p)
                    continue;
                p->stats = (qa_mode_statistics){0};
                p->spawn_state = 0;
                if (!qa_modes_set_score(m, v->id, actor, 0, e))
                    return false;
            }
            v->remaining_seconds = (int32_t)v->value.rules.time_limit_minutes * 60;
            v->value.started_ns = v->value.time_ns;
            if (!mode_set_phase(m, v, QA_MODE_PLAYING, 0, e))
                return false;
        }
    } else if (v->value.phase == QA_MODE_PLAYING) {
        if (v->remaining_seconds > 10 && v->value.rules.frag_limit) {
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                mode_member *p = mode_member_get(m, v, m->players_order.ids[ordinal]);
                if (p && p->stats.score >= v->value.rules.frag_limit) {
                    v->remaining_seconds = 10;
                    break;
                }
            }
        }
        if (v->remaining_seconds <= 0) {
            v->remaining_seconds = 300;
            v->value.rules.match_lock = false;
            return mode_set_phase(m, v, QA_MODE_FINISHED, 0, e);
        }
    } else if (v->value.phase == QA_MODE_FINISHED && v->remaining_seconds <= 0)
        return mode_set_phase(m, v, QA_MODE_WAITING, 0, e);
    --v->remaining_seconds;
    v->value.deadline_ns =
        v->value.time_ns +
        (uint64_t)(v->remaining_seconds > 0 ? v->remaining_seconds : 0) * MODE_SECOND;
    return true;
}
bool mode_match_frame(qa_modes *m, mode_instance *v, uint64_t elapsed, qa_error *e) {
    if (!qa_modes_rank(m, v->id, e))
        return false;
    uint64_t now = v->value.time_ns;
    qa_mode_rules *r = &v->value.rules;
    if (r->source == QA_MODE_LMCTF)
        return lmctf_frame(m, v, e);
    if (r->paused) {
        if (v->value.deadline_ns)
            v->value.deadline_ns += elapsed;
        if (r->auto_lock)
            r->match_lock = false;
        return true;
    }
    if (v->value.phase == QA_MODE_EXIT_PENDING)
        return now >= v->value.deadline_ns ? intermission(m, v, e) : true;
    if (v->value.phase == QA_MODE_INTERMISSION) {
        if (r->kind == QA_MODE_SINGLE_PLAYER) return true;
        size_t ready = 0, not_ready = 0;
        qa_actor_owner native_owner;
        bool native = r->source >= QA_MODE_Q3 && m->options.hooks.q3_native_source &&
            m->options.hooks.q3_native_source(m->options.hooks.context, v->id, &native_owner);
        int32_t ready_mask = 0;
        for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
            uint32_t i = m->players_order.ids[ordinal].slot;
            mode_member *member = &v->members[i];
            mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
            if (!p) continue;
            bool client_ready = member->player.ready;
            if (native) {
                bool eligible;
                if (!m->options.hooks.q3_intermission_client ||
                    !MODE_CALLBACK(m, m->options.hooks.q3_intermission_client(m->options.hooks.context,
                        v->id, p->value.actor, &eligible, &client_ready, e))) return false;
                if (!eligible) continue;
                member = mode_member_get(m, v, p->value.actor);
                if (!member) return mode_fail(e, "native ready client retired during source access");
            } else if (p->value.bot || !p->value.connected)
                continue;
            if (client_ready) {
                ++ready;
                if (native) {
                    qa_actor_owner owner;
                    uint32_t slot;
                    if (!m->options.hooks.q3_client_slot ||
                        !MODE_CALLBACK(m, m->options.hooks.q3_client_slot(m->options.hooks.context,
                            v->id, p->value.actor, &owner, &slot, e)) || owner != native_owner || slot >= 64)
                        return mode_fail(e, "intermission readiness lost its native source client");
                    if (slot < 16) ready_mask |= (int32_t)(1u << slot);
                }
            } else
                ++not_ready;
        }
        if (native) {
            if (!m->options.hooks.q3_intermission_ready_publish ||
                !MODE_CALLBACK(m, m->options.hooks.q3_intermission_ready_publish(m->options.hooks.context,
                    v->id, ready_mask, e))) return false;
        }
        if (now < v->value.deadline_ns) return true;
        if (!ready) {
            v->value.ready_exit = false;
            v->ready_since_ns = 0;
            return true;
        }
        if (!not_ready)
            return exit_intermission(m, v, e);
        if (!v->value.ready_exit) {
            v->value.ready_exit = true;
            v->ready_since_ns = now;
        }
        return now - v->ready_since_ns >= 10 * MODE_SECOND ? exit_intermission(m, v, e) : true;
    }
    if (v->value.phase == QA_MODE_FINISHED)
        return true;
    if (v->value.phase == QA_MODE_SETUP) {
        if (now >= v->value.deadline_ns) {
            if (r->competition < 3) {
                r->competition = 1;
                return begin_play(m, v, e);
            }
            v->value.deadline_ns = now + (uint64_t)r->setup_seconds * MODE_SECOND;
        }
        return true;
    }
    if (r->kind == QA_MODE_DUEL && !duel_promote(m, v, e))
        return false;
    if (v->q3_settings_present) {
        if (!mode_q3_warmup_frame(m, v, e)) return false;
        if (tied(m, v)) return true;
        return mode_q3_limits(m, v, e);
    }
    if (v->value.phase == QA_MODE_WAITING) {
        if (r->kind == QA_MODE_HORDE)
            return true;
        bool enough = r->kind > QA_MODE_TEAM_DEATHMATCH && r->kind <= QA_MODE_HARVESTER
                          ? playing_team(m, v, r->teams[0]) && playing_team(m, v, r->teams[1])
                          : v->value.playing >= 2;
        if (!enough)
            return true;
        return mode_set_phase(
            m, v, QA_MODE_COUNTDOWN,
            now + (uint64_t)(r->warmup_seconds > 0 ? r->warmup_seconds - 1 : 0) * MODE_SECOND, e);
    }
    if (v->value.phase == QA_MODE_COUNTDOWN) {
        if (r->source >= QA_MODE_Q3) {
            bool enough = r->kind > QA_MODE_TEAM_DEATHMATCH && r->kind <= QA_MODE_HARVESTER
                              ? playing_team(m, v, r->teams[0]) && playing_team(m, v, r->teams[1])
                              : v->value.playing >= 2;
            if (!enough) {
                v->restart_sent = false;
                return mode_set_phase(m, v, QA_MODE_WAITING, 0, e);
            }
        }
        if (now <= v->value.deadline_ns)
            return true;
        if (r->source >= QA_MODE_Q3) {
            if (v->restart_sent)
                return true;
            v->restart_sent = true;
            return mode_intent(m, v, QA_MATCH_RESTART_MAP, (qa_actor_id){0}, 0, 0, e);
        }
        return begin_play(m, v, e);
    }
    if (r->source == QA_MODE_Q2_CTF && r->competition > 1 && v->value.deadline_ns &&
        now >= v->value.deadline_ns)
        return qa_modes_end(m, v->id, 0, e);
    if (r->source == QA_MODE_Q2_CTF && r->competition > 1)
        return true;
    if (r->source >= QA_MODE_Q3 && tied(m, v))
        return true;
    if (r->time_limit_minutes > 0 &&
        now - v->value.started_ns >= (uint64_t)(r->time_limit_minutes * 60.0f * (float)MODE_SECOND))
        return qa_modes_end(m, v->id, 0, e);
    if (r->capture_limit > 0 && r->kind >= QA_MODE_CTF && r->kind <= QA_MODE_HARVESTER) {
        for (int i = 0; i < 3; ++i)
            if (v->value.team_captures[i] >= r->capture_limit ||
                (r->source >= QA_MODE_Q3 && v->value.team_scores[i] >= r->capture_limit))
                return qa_modes_end(m, v->id, 0, e);
    }
    if (r->frag_limit > 0 && (r->source < QA_MODE_Q3 || v->value.playing >= 2) &&
        r->kind < QA_MODE_CTF) {
        if (r->source >= QA_MODE_Q3 && r->kind == QA_MODE_TEAM_DEATHMATCH) {
            if (v->value.team_scores[0] >= r->frag_limit ||
                v->value.team_scores[1] >= r->frag_limit)
                return qa_modes_end(m, v->id, 0, e);
        } else
            for (size_t i = 0; i < v->value.playing; ++i) {
                int32_t score;
                if (!qa_modes_score(m, v->id, v->sorted[i], &score, e))
                    return false;
                if (score >= r->frag_limit)
                    return qa_modes_end(m, v->id, 0, e);
            }
    }
    return true;
}
bool qa_modes_ghost_rejoin(qa_modes *m, qa_mode_id id, qa_actor_id actor, uint32_t code,
                           qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_player *player = mode_player_get(m, actor);
    if (!v || !player || v->value.rules.source != QA_MODE_Q2_CTF ||
        v->value.phase != QA_MODE_PLAYING)
        return mode_fail(e, "ghost restore requires an active CTF match");
    qa_team_id current;
    if (!qa_modes_team(m, v->id, actor, &current, e))
        return false;
    if (current)
        return mode_fail(e, "ghost restore requires an unassigned player");
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_ghost *ghost = &v->ghosts[i];
        if (!code || ghost->code != code)
            continue;
        if (mode_live(m, ghost->actor)) {
            mode_member *old = mode_member_get(m, v, ghost->actor);
            if (old)
                old->ghost_code = 0;
        }
        if (!mode_join(m, v, actor, ghost->team, false, true, e) ||
            !qa_modes_set_score(m, id, actor, ghost->score, e))
            return false;
        mode_member *member = mode_member_get(m, v, actor);
        if (!member)
            return true;
        member->stats = ghost->stats;
        member->ghost_code = code;
        ghost->actor = actor;
        return !m->options.hooks.respawn ||
               m->options.hooks.respawn(m->options.hooks.context, v->id, actor, true, e);
    }
    return mode_fail(e, "unknown ghost code");
}
