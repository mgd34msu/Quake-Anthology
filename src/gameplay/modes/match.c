#include "internal.h"
#include <inttypes.h>
#include <stdio.h>

static bool ctf_match_status_refresh(qa_modes *m, mode_instance *v, qa_error *e) {
    mode_match_status *status = &v->match_status;
    qa_mode_phase phase = v->value.phase;
    bool ctf = v->value.rules.enabled && v->value.rules.source == QA_MODE_Q2_CTF;
    bool timed_play = phase == QA_MODE_PLAYING && v->value.deadline_ns;
    bool post = phase >= QA_MODE_EXIT_PENDING;
    bool retained_post = post && v->value.rules.competition > 1 && v->remaining_seconds > 0;
    bool visible = ctf && (phase == QA_MODE_SETUP || phase == QA_MODE_COUNTDOWN ||
                           timed_play || (post && status->visible) || retained_post);
    uint64_t seconds = v->value.deadline_ns > v->value.time_ns
        ? (v->value.deadline_ns - v->value.time_ns) / MODE_SECOND : 0;
    bool setup_timed = v->value.rules.competition < 3;
    bool changed = status->visible != visible;
    if (visible && post && !status->initialized && retained_post) {
        snprintf(status->text, sizeof(status->text), "%02d:%02d MATCH",
            v->remaining_seconds / 60, v->remaining_seconds % 60);
        changed = true;
    }
    if (visible && !post && seconds &&
        (!status->initialized || status->phase != phase || status->seconds != seconds ||
         status->setup_timed != setup_timed || status->dirty)) {
        char text[sizeof(status->text)] = {0};
        if (phase == QA_MODE_SETUP) {
            size_t not_ready = 0;
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                qa_actor_id actor = m->players_order.actors[ordinal];
                mode_member *member = mode_member_get(m, v, actor);
                mode_player *player = member ? mode_player_get(m, actor) : NULL;
                if (!player || !player->connected || member->player.ready)
                    continue;
                qa_team_id team;
                if (!qa_modes_team(m, v->id, actor, &team, e))
                    return false;
                member = mode_member_get(m, v, actor);
                if (member && team && !member->player.ready)
                    ++not_ready;
            }
            if (setup_timed)
                snprintf(text, sizeof(text), "%02" PRIu64 ":%02" PRIu64
                    " SETUP: %zu not ready", seconds / 60, seconds % 60, not_ready);
            else
                snprintf(text, sizeof(text), "SETUP: %zu not ready", not_ready);
        } else
            snprintf(text, sizeof(text), "%02" PRIu64 ":%02" PRIu64 " %s",
                seconds / 60, seconds % 60,
                phase == QA_MODE_COUNTDOWN ? "UNTIL START" : "MATCH");
        if (memcmp(status->text, text, sizeof(text))) {
            memcpy(status->text, text, sizeof(text));
            changed = true;
        }
        v->remaining_seconds = (int32_t)seconds;
    }
    status->initialized = true;
    status->visible = visible;
    status->phase = phase;
    status->seconds = seconds;
    status->setup_timed = setup_timed;
    status->dirty = false;
    if (changed)
        ++status->revision;
    return true;
}
static bool ctf_match_status_publish(qa_modes *m, mode_instance *v, qa_error *e) {
    mode_match_status *status = &v->match_status;
    if (status->revision == status->emitted_revision || !m->options.hooks.event)
        return true;
    uint64_t revision = status->revision;
    qa_mode_event event = {.kind = QA_MODE_MATCH_STATUS, .mode = v->id,
        .time_ns = v->value.time_ns, .status_text = status->text};
    if (!MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e)))
        return false;
    status->emitted_revision = revision;
    return true;
}
bool qa_modes_q2_match_status(qa_modes *m, qa_mode_id id, const char **text,
    bool *visible, uint64_t *revision, qa_error *e) {
    *text = "";
    *visible = false;
    *revision = 0;
    mode_instance *v = mode_get(m, id);
    if (!v || !v->value.rules.enabled || v->value.rules.source != QA_MODE_Q2_CTF)
        return true;
    if (!ctf_match_status_refresh(m, v, e))
        return false;
    *text = v->match_status.text;
    *visible = v->match_status.visible;
    *revision = v->match_status.revision;
    return true;
}

static bool lmctf_clock_message(qa_modes *m, mode_instance *v, mode_name text, int32_t value,
                                 qa_error *e) {
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_NUMBER,
                                       .value.number = value};
    return mode_message(m, v, (qa_actor_id){0}, m->runtime_names[text], &argument, 1, e);
}
static bool match_team_scores(qa_modes *m, mode_instance *v, int64_t scores[2], qa_error *e) {
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.actors[ordinal];
        mode_member *p = mode_member_get(m, v, actor);
        mode_player *player = p ? mode_player_get(m, actor) : NULL;
        qa_team_id team;
        if (!player || !player->connected || player->connecting ||
            !qa_modes_team(m, v->id, actor, &team, NULL))
            continue;
        p = mode_member_get(m, v, actor);
        int index = mode_team_index(v, team);
        if (p && index >= 0 && index < 2) {
            int32_t score = p->stats.score;
            if (v->value.rules.source == QA_MODE_Q2_CTF &&
                !qa_modes_score(m, v->id, actor, &score, e))
                return false;
            scores[index] = v->value.rules.source == QA_MODE_Q2_CTF
                ? mode_add_i32((int32_t)scores[index], score) : scores[index] + score;
        }
    }
    return true;
}
static bool lmctf_result_message(qa_modes *m, mode_instance *v, qa_error *e) {
    int64_t scores[2] = {0};
    if (!match_team_scores(m, v, scores, e))
        return false;
    bool blue = scores[1] > scores[0];
    bool tied = scores[0] == scores[1];
    qa_builtin_message_arg arguments[2] = {
        {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = (double)scores[blue ? 1 : 0]},
        {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = (double)scores[blue ? 0 : 1]}};
    return mode_message(m, v, (qa_actor_id){0}, m->runtime_names[tied ? MODE_NAME_LMCTF_MATCH_TIED :
        blue ? MODE_NAME_LMCTF_MATCH_BLUE_WINS : MODE_NAME_LMCTF_MATCH_RED_WINS],
        arguments, tied ? 1 : 2, e);
}
static bool team_scores_update(qa_modes *m, mode_instance *v, qa_error *e) {
    int64_t scores[2] = {0};
    if (!match_team_scores(m, v, scores, e))
        return false;
    v->value.team_scores[0] = (int32_t)scores[0];
    v->value.team_scores[1] = (int32_t)scores[1];
    return true;
}
static bool ctf_result_message(qa_modes *m, mode_instance *v, qa_error *e) {
    if (!mode_message_level(m, v, (qa_actor_id){0},
            m->runtime_names[MODE_NAME_CTF_MATCH_COMPLETED], NULL, 0, 3, e) ||
        !team_scores_update(m, v, e))
        return false;
    int32_t scores[2] = {v->value.team_scores[0], v->value.team_scores[1]};
    qa_string_id teams[2] = {m->runtime_names[MODE_NAME_CTF_RED],
                            m->runtime_names[MODE_NAME_CTF_BLUE]};
    for (int i = 0; i < 2; ++i) {
        qa_builtin_message_arg arguments[3] = {
            {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = teams[i]},
            {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = v->value.team_captures[i]},
            {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = (double)scores[i]}};
        if (!mode_message(m, v, (qa_actor_id){0}, m->runtime_names[MODE_NAME_CTF_TEAM_RESULT],
                          arguments, 3, e))
            return false;
    }
    bool captures = v->value.team_captures[0] != v->value.team_captures[1];
    if (!captures && scores[0] == scores[1])
        return mode_message_level(m, v, (qa_actor_id){0}, m->runtime_names[MODE_NAME_CTF_TIED],
                                  NULL, 0, 3, e);
    int winner = captures ? v->value.team_captures[1] > v->value.team_captures[0]
                          : scores[1] > scores[0];
    double margin = captures
        ? (double)v->value.team_captures[winner] - v->value.team_captures[!winner]
        : (double)scores[winner] - (double)scores[!winner];
    qa_builtin_message_arg arguments[3] = {
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = teams[winner]},
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = teams[!winner]},
        {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = margin}};
    return mode_message_level(m, v, (qa_actor_id){0},
        m->runtime_names[captures ? MODE_NAME_CTF_WON_CAPTURES : MODE_NAME_CTF_WON_POINTS],
        arguments, 3, 3, e);
}
static bool threewave_result_message(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->value.rules.start_map)
        return true;
    int32_t captures[2] = {v->value.team_captures[0], v->value.team_captures[1]};
    bool tied = captures[0] == captures[1];
    unsigned winner = captures[1] > captures[0];
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.actors[ordinal];
        if (!mode_player_get(m, actor))
            continue;
        for (unsigned result = 0; result < (tied ? 1u : 2u); ++result) {
            unsigned team = winner ^ result;
            mode_name text = tied ? MODE_NAME_THREEWAVE_MATCH_TIED
                : team == 0 ? result ? MODE_NAME_THREEWAVE_RED_LOST : MODE_NAME_THREEWAVE_RED_WON
                : result ? MODE_NAME_THREEWAVE_BLUE_LOST : MODE_NAME_THREEWAVE_BLUE_WON;
            qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_NUMBER,
                .value.number = captures[team]};
            if (!mode_message(m, v, actor, m->runtime_names[text], &argument, 1, e))
                return false;
        }
    }
    return true;
}

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
        uint32_t i = m->players_order.actors[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (!p)
            continue;
        qa_match_player value = mode_player_connection(p);
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
    if (!ctf_match_status_refresh(m, v, e) || !ctf_match_status_publish(m, v, e))
        return false;
    return mode_event(m, v, QA_MODE_PHASE, (qa_actor_id){0}, (qa_actor_id){0}, (qa_actor_id){0}, 0,
                      (int32_t)phase, 0, e);
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
        uint32_t i = m->players_order.actors[ordinal].slot;
        mode_member *p = &v->members[i];
        mode_player *player = p->joined ? mode_player_get(m, p->actor) : NULL;
        qa_team_id current;
        if (player && player->connected && !player->connecting &&
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
    if (v->value.rules.source == QA_MODE_Q2_CTF) {
        qa_team_id team;
        if (!qa_modes_team(m, id, actor, &team, e))
            return false;
        mode_name refusal;
        if (!team)
            refusal = MODE_NAME_CTF_PICK_TEAM;
        else if (v->value.phase != QA_MODE_SETUP &&
                 (ready || v->value.phase != QA_MODE_COUNTDOWN))
            refusal = MODE_NAME_CTF_NOT_SETUP;
        else if (member->player.ready == ready)
            refusal = ready ? MODE_NAME_CTF_ALREADY_READY : MODE_NAME_CTF_NOT_READY;
        else {
            member->player.ready = ready;
            v->match_status.dirty = true;
            qa_builtin_message_arg name = {.kind = QA_BUILTIN_MESSAGE_STRING,
                                           .value.text = p->identity->name};
            if (!mode_message(m, v, (qa_actor_id){0}, m->runtime_names[ready
                    ? MODE_NAME_CTF_PLAYER_READY : MODE_NAME_CTF_PLAYER_NOT_READY], &name, 1, e))
                return false;
            if (!ready && v->value.phase == QA_MODE_COUNTDOWN)
                return mode_message_level(m, v, (qa_actor_id){0},
                    m->runtime_names[MODE_NAME_CTF_MATCH_HALTED], NULL, 0, 3, e) &&
                    mode_set_phase(m, v, QA_MODE_SETUP,
                        v->value.time_ns + (uint64_t)v->value.rules.setup_seconds * MODE_SECOND, e);
            if (!ready ||
                !playing_team(m, v, v->value.rules.teams[0]) ||
                !playing_team(m, v, v->value.rules.teams[1]))
                return true;
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                uint32_t i = m->players_order.actors[ordinal].slot;
                mode_member *a = &v->members[i];
                mode_player *x = a->joined ? mode_player_get(m, a->actor) : NULL;
                qa_team_id current;
                if (x && x->connected && !x->connecting &&
                    qa_modes_team(m, id, a->actor, &current, NULL) && current && !a->player.ready)
                    return true;
            }
            return mode_message_level(m, v, (qa_actor_id){0},
                m->runtime_names[MODE_NAME_CTF_ALL_READY], NULL, 0, 3, e) &&
                mode_set_phase(m, v, QA_MODE_COUNTDOWN,
                    v->value.time_ns + (uint64_t)v->value.rules.countdown_seconds * MODE_SECOND, e);
        }
        return mode_message(m, v, actor, m->runtime_names[refusal], NULL, 0, e);
    }
    member->player.ready = ready;
    return true;
}
bool mode_ctf_ghost_assign(qa_modes *m, mode_instance *v, mode_member *p, qa_error *e) {
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
    qa_builtin_player_info info;
    uint32_t number = m->options.services.player_info &&
        m->options.services.player_info(m->options.services.context, p->actor, &info)
        ? info.slot + 1 : 0;
    v->ghosts[p->actor.slot] = (mode_ghost){.actor = p->actor,
                                            .name = player->identity->name,
                                            .team = team,
                                            .stats = p->stats,
                                            .code = code,
                                            .number = number,
                                            .score = score};
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_NUMBER,
                                       .value.number = code};
    return mode_event(m, v, QA_MODE_GHOST_CODE, p->actor, (qa_actor_id){0}, (qa_actor_id){0}, team,
                      (int32_t)code, 0, e) &&
           mode_message_level(m, v, p->actor, m->runtime_names[MODE_NAME_CTF_GHOST_CODE],
                              &argument, 1, 3, e) &&
           mode_message(m, v, p->actor, m->runtime_names[MODE_NAME_CTF_GHOST_HELP],
                        &argument, 1, e);
}
static bool begin_play(qa_modes *m, mode_instance *v, qa_error *e) {
    bool reset = v->value.rules.source == QA_MODE_Q2_CTF || v->value.rules.source == QA_MODE_LMCTF;
    bool ctf_match = v->value.rules.source == QA_MODE_Q2_CTF &&
                     v->value.phase == QA_MODE_COUNTDOWN;
    v->value.started_ns = v->value.time_ns;
    v->restart_sent = false;
    v->remaining_seconds = v->value.rules.source == QA_MODE_LMCTF
                               ? (int32_t)v->value.rules.time_limit_minutes * 60
                               : v->value.rules.match_seconds;
    if (reset) {
        memset(v->value.team_scores, 0, sizeof(v->value.team_scores));
        memset(v->value.team_captures, 0, sizeof(v->value.team_captures));
        v->value.last_capture_ns = 0;
        v->value.last_capture_team = 0;
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
        uint32_t i = m->players_order.actors[ordinal].slot;
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
        if (ctf_match) {
            qa_builtin_event message = {.kind = QA_BUILTIN_CENTERPRINT, .actor = p->actor,
                                        .text = m->runtime_names[MODE_NAME_CTF_MATCH_STARTED]};
            if (!mode_builtin_emit(m, v, &message, e))
                return false;
        }
        if (p->player.spectator)
            continue;
        if (v->value.rules.source == QA_MODE_Q2_CTF) {
            if (!mode_ctf_ghost_assign(m, v, p, e))
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
bool mode_ctf_reset(qa_modes *m, mode_instance *v, bool setup, bool change_competition, qa_error *e) {
    int32_t competition = !change_competition ? v->value.rules.competition :
        setup && v->value.rules.competition >= 3 ? v->value.rules.competition : setup ? 2 : 1;
    v->value.rules.competition = competition;
    char value[16];
    snprintf(value, sizeof(value), "%" PRId32, competition);
    if (!mode_set_phase(m, v, setup ? QA_MODE_SETUP : QA_MODE_PLAYING,
            setup ? v->value.time_ns + (uint64_t)v->value.rules.setup_seconds * MODE_SECOND : 0, e) ||
        (change_competition && m->options.hooks.source_cvar_set &&
         !MODE_CALLBACK(m, m->options.hooks.source_cvar_set(m->options.hooks.context, v->id,
             m->runtime_names[MODE_NAME_CVAR_COMPETITION], value, e))))
        return false;
    v->value.started_ns = v->value.time_ns;
    v->remaining_seconds = 0;
    v->ready_since_ns = 0;
    v->restart_sent = false;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.actors[ordinal];
        mode_member *member = mode_member_get(m, v, actor);
        if (!member || !mode_player_get(m, actor))
            continue;
        if (m->options.hooks.release_grapple &&
            !MODE_CALLBACK(m, m->options.hooks.release_grapple(m->options.hooks.context, actor, e)))
            return false;
        if (!mode_live(m, actor))
            continue;
        if (!mode_ui_close(m, v, actor, e) || !qa_modes_drop(m, v->id, actor, true, e) ||
            !mode_join(m, v, actor, 0, true, false, e))
            return false;
        member = mode_member_get(m, v, actor);
        if (!member)
            continue;
        member->player.ready = false;
        member->spawn_state = 0;
        member->respawn_ns = 0;
        member->ghost_code = 0;
        qa_combat_state state;
        if (!qa_combat_read_traits(m->options.services.combat, actor, &state, e))
            return false;
        state.invulnerable = false;
        if (!qa_combat_set_traits(m->options.services.combat, actor, &state, e) ||
            (m->options.hooks.respawn &&
             !MODE_CALLBACK(m, m->options.hooks.respawn(m->options.hooks.context,
                 v->id, actor, false, e))))
            return false;
    }
    if (!qa_builtin_observations(&m->options.services, &m->observations, e))
        return false;
    for (size_t ordinal = 0; ordinal < m->observations.count; ++ordinal) {
        mode_object *object = mode_object_get(m, m->observations.actors[ordinal]);
        if (!object || object->mode.slot != v->id.slot ||
            object->mode.generation != v->id.generation)
            continue;
        if (object->spec.kind == QA_MODE_OBJECT_FLAG) {
            if (!mode_flag_reset(m, v, object, false, e))
                return false;
        } else if (object->spec.kind == QA_MODE_OBJECT_RELIC &&
                   !qa_session_release(m->options.services.session, object->actor, e))
            return false;
    }
    v->relic_spawn_ns = 0;
    return mode_relic_spawn_all(m, v, e) && qa_modes_rank(m, v->id, e);
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
        .kind = QA_MODE_MESSAGE, .mode = id, .text = reason, .time_ns = v->value.time_ns,
        .print_level = reason && qa_ruleset_read(v->value.rules.ruleset)->family == QA_GAME_Q2
            ? 2 : 0};
    if (m->options.hooks.event &&
        !MODE_CALLBACK(m, m->options.hooks.event(m->options.hooks.context, &event, e)))
        return false;
    uint64_t delay =
        v->value.rules.source == QA_MODE_TEAM_ARENA && v->value.rules.single_player_active
            ? 5 * MODE_SECOND
            : MODE_SECOND;
    if (v->value.rules.source == QA_MODE_LMCTF)
        return lmctf_result_message(m, v, e) &&
               mode_set_phase(m, v, QA_MODE_FINISHED, v->value.time_ns + 300 * MODE_SECOND, e);
    if (v->value.rules.source < QA_MODE_Q3) {
        bool ctf_match = v->value.rules.source == QA_MODE_Q2_CTF &&
                         v->value.phase == QA_MODE_PLAYING && v->value.deadline_ns;
        if (v->value.rules.source == QA_MODE_Q2_CTF && !ctf_match &&
            !team_scores_update(m, v, e))
            return false;
        if (!mode_set_phase(m, v, QA_MODE_FINISHED, 0, e))
            return false;
        if (ctf_match && !ctf_result_message(m, v, e))
            return false;
        if (v->value.rules.source == QA_MODE_THREEWAVE &&
            !threewave_result_message(m, v, e))
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
        uint32_t i = m->players_order.actors[ordinal].slot;
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
static bool ctf_setting_write(qa_modes *m, mode_instance *v, mode_name name,
                              int64_t value, qa_error *e) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRId64, value);
    return !m->options.hooks.source_cvar_set || MODE_CALLBACK(m,
        m->options.hooks.source_cvar_set(m->options.hooks.context, v->id,
                                       m->runtime_names[name], text, e));
}
bool mode_ui_ctf_settings_apply(qa_modes *m, mode_instance *v, qa_actor_id actor,
                                const qa_mode_ctf_settings *settings, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    mode_player *player = member ? mode_player_get(m, actor) : NULL;
    qa_mode_source_settings source;
    if (!player || !member->admin || !m->options.hooks.source_settings ||
        !m->options.hooks.source_settings(m->options.hooks.context, v->id, &source))
        return true;
    qa_mode_rules rules = v->value.rules;
    const int32_t selected[] = {settings->match_minutes, settings->setup_minutes,
                               settings->start_seconds};
    const int32_t current[] = {rules.match_seconds, rules.setup_seconds,
                              rules.countdown_seconds};
    const mode_name variables[] = {MODE_NAME_CVAR_MATCHTIME, MODE_NAME_CVAR_MATCHSETUPTIME,
                                   MODE_NAME_CVAR_MATCHSTARTTIME};
    const mode_name notices[] = {MODE_NAME_CTF_SETTING_MATCH, MODE_NAME_CTF_SETTING_SETUP,
                                 MODE_NAME_CTF_SETTING_START};
    int32_t seconds[3];
    for (unsigned i = 0; i < 3; ++i) {
        int64_t value = (int64_t)selected[i] * (i == 2 ? 1 : 60);
        if (value < 0 || value > INT32_MAX)
            return true;
        seconds[i] = (int32_t)value;
    }
    qa_builtin_message_arg arguments[] = {
        {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = player->identity->name},
        {.kind = QA_BUILTIN_MESSAGE_NUMBER}
    };
    for (unsigned i = 0; i < 3; ++i) {
        if (seconds[i] == current[i])
            continue;
        arguments[1].value.number = selected[i];
        if (!ctf_setting_write(m, v, variables[i], selected[i], e) ||
            !mode_message(m, v, (qa_actor_id){0}, m->runtime_names[notices[i]],
                          arguments, 2, e))
            return false;
    }
    const bool chosen[] = {settings->weapons_stay, settings->instant_items,
        settings->quad_drop, settings->instant_weapons, settings->match_lock};
    const bool previous[] = {source.ctf.weapons_stay, source.ctf.instant_items,
        source.ctf.quad_drop, source.instant_weapons_integer, source.match_lock_integer};
    const uint32_t bits[] = {4u, 16u, 16384u};
    const mode_name labels[] = {MODE_NAME_CTF_SETTING_WEAPONS_STAY, MODE_NAME_CTF_SETTING_INSTANT_ITEMS,
        MODE_NAME_CTF_SETTING_QUAD_DROP, MODE_NAME_CTF_SETTING_INSTANT_WEAPONS,
        MODE_NAME_CTF_SETTING_MATCH_LOCK};
    uint32_t flags = source.deathmatch_flags;
    qa_builtin_message_arg toggles[] = {
        arguments[0], {.kind = QA_BUILTIN_MESSAGE_STRING}, {.kind = QA_BUILTIN_MESSAGE_STRING}
    };
    for (unsigned i = 0; i < 5; ++i) {
        if (chosen[i] == previous[i])
            continue;
        mode_name name = i < 3 ? MODE_NAME_CVAR_DMFLAGS :
                         i == 3 ? MODE_NAME_CVAR_INSTANTWEAP : MODE_NAME_CVAR_MATCHLOCK;
        if (i < 3)
            flags = chosen[i] ? flags | bits[i] : flags & ~bits[i];
        toggles[1].value.text = m->runtime_names[chosen[i]
            ? MODE_NAME_CTF_SETTING_ON : MODE_NAME_CTF_SETTING_OFF];
        toggles[2].value.text = m->runtime_names[labels[i]];
        if (!ctf_setting_write(m, v, name, i < 3 ? flags : chosen[i], e) ||
            !mode_message(m, v, (qa_actor_id){0}, m->runtime_names[MODE_NAME_CTF_SETTING_TOGGLE],
                          toggles, 3, e))
            return false;
    }
    rules.match_seconds = seconds[0];
    rules.setup_seconds = seconds[1];
    rules.countdown_seconds = seconds[2];
    rules.match_lock = settings->match_lock;
    return qa_modes_configure(m, v->id, &rules, e);
}
bool mode_ui_ctf_match(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    mode_member *member = mode_member_get(m, v, actor);
    if (!member || !member->admin)
        return true;
    if (v->value.phase == QA_MODE_SETUP)
        return mode_message_level(m, v, (qa_actor_id){0},
            m->runtime_names[MODE_NAME_CTF_ADMIN_MATCH_FORCED_START], NULL, 0, 3, e) &&
            mode_set_phase(m, v, QA_MODE_COUNTDOWN,
                v->value.time_ns + (uint64_t)v->value.rules.countdown_seconds * MODE_SECOND, e);
    if (v->value.phase == QA_MODE_PLAYING && v->value.deadline_ns)
        return mode_message_level(m, v, (qa_actor_id){0},
            m->runtime_names[MODE_NAME_CTF_ADMIN_MATCH_FORCED_STOP], NULL, 0, 3, e) &&
            mode_ctf_reset(m, v, true, false, e);
    if (v->value.phase == QA_MODE_PLAYING && v->value.rules.competition)
        return mode_ctf_reset(m, v, true, true, e);
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
        uint32_t i = m->players_order.actors[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (p && p->connected && !p->connecting && member->player.spectator &&
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
            if (!lmctf_clock_message(m, v, MODE_NAME_LMCTF_MATCH_COUNTDOWN, v->remaining_seconds, e) ||
                !mode_event(m, v, QA_MODE_PHASE, (qa_actor_id){0}, (qa_actor_id){0},
                            (qa_actor_id){0}, 0, QA_MODE_COUNTDOWN, v->remaining_seconds, e))
                return false;
        if (v->remaining_seconds <= 0) {
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                qa_actor_id actor = m->players_order.actors[ordinal];
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
            if (!mode_set_phase(m, v, QA_MODE_PLAYING, 0, e) ||
                !lmctf_clock_message(m, v, MODE_NAME_LMCTF_MATCH_MINUTES,
                                    v->remaining_seconds / 60, e))
                return false;
        }
    } else if (v->value.phase == QA_MODE_PLAYING) {
        if (v->remaining_seconds > 10 && v->value.rules.frag_limit) {
            for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
                mode_member *p = mode_member_get(m, v, m->players_order.actors[ordinal]);
                if (p && p->stats.score >= v->value.rules.frag_limit) {
                    v->remaining_seconds = 10;
                    break;
                }
            }
        }
        if (v->remaining_seconds <= 0) {
            if (!lmctf_result_message(m, v, e))
                return false;
            v->remaining_seconds = 300;
            v->value.rules.match_lock = false;
            return mode_set_phase(m, v, QA_MODE_FINISHED, 0, e);
        }
        if (!(v->remaining_seconds % 60) &&
            !lmctf_clock_message(m, v, v->remaining_seconds == 60 ? MODE_NAME_LMCTF_MATCH_MINUTE :
                MODE_NAME_LMCTF_MATCH_MINUTES, v->remaining_seconds / 60, e))
            return false;
    } else if (v->value.phase == QA_MODE_FINISHED && v->remaining_seconds <= 0)
        return mode_set_phase(m, v, QA_MODE_WAITING, 0, e);
    --v->remaining_seconds;
    v->value.deadline_ns =
        v->value.time_ns +
        (uint64_t)(v->remaining_seconds > 0 ? v->remaining_seconds : 0) * MODE_SECOND;
    return true;
}
static bool limit_end(qa_modes *m, mode_instance *v, mode_name classic, mode_name rerelease,
                      qa_error *e) {
    qa_string_id reason = 0;
    if (qa_ruleset_read(v->value.rules.ruleset)->family == QA_GAME_Q2)
        reason = m->runtime_names[v->value.rules.ruleset == QA_RULESET_Q2_RERELEASE
            ? rerelease : classic];
    return qa_modes_end(m, v->id, reason, e);
}
bool mode_match_frame(qa_modes *m, mode_instance *v, uint64_t elapsed, qa_error *e) {
    if (!qa_modes_rank(m, v->id, e))
        return false;
    uint64_t now = v->value.time_ns;
    qa_mode_rules *r = &v->value.rules;
    if (r->source == QA_MODE_LMCTF)
        return lmctf_frame(m, v, e) && team_scores_update(m, v, e);
    if (r->paused) {
        if (v->value.deadline_ns)
            v->value.deadline_ns += elapsed;
        if (r->auto_lock)
            r->match_lock = false;
        return ctf_match_status_refresh(m, v, e) && ctf_match_status_publish(m, v, e);
    }
    if (!ctf_match_status_refresh(m, v, e) || !ctf_match_status_publish(m, v, e))
        return false;
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
            uint32_t i = m->players_order.actors[ordinal].slot;
            mode_member *member = &v->members[i];
            mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
            if (!p) continue;
            bool client_ready = member->player.ready;
            if (native) {
                bool eligible;
                if (!m->options.hooks.q3_intermission_client ||
                    !MODE_CALLBACK(m, m->options.hooks.q3_intermission_client(m->options.hooks.context,
                        v->id, p->actor, &eligible, &client_ready, e))) return false;
                if (!eligible) continue;
                member = mode_member_get(m, v, p->actor);
                if (!member) return mode_fail(e, "native ready client retired during source access");
            } else if (p->identity->bot || !p->connected)
                continue;
            if (client_ready) {
                ++ready;
                if (native) {
                    qa_actor_owner owner;
                    uint32_t slot;
                    if (!m->options.hooks.q3_client_slot ||
                        !MODE_CALLBACK(m, m->options.hooks.q3_client_slot(m->options.hooks.context,
                            v->id, p->actor, &owner, &slot, e)) || owner != native_owner || slot >= 64)
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
        if (now >= v->value.deadline_ns || (r->source == QA_MODE_Q2_CTF &&
            v->value.deadline_ns - now < MODE_SECOND)) {
            if (r->competition < 3) {
                if (r->source == QA_MODE_Q2_CTF)
            return mode_ctf_reset(m, v, false, true, e);
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
    if (r->source == QA_MODE_THREEWAVE && (r->start_map || v->value.ctf_pregame_over))
        return true;
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
        if (r->source == QA_MODE_Q2_CTF ? now < v->value.deadline_ns &&
                v->value.deadline_ns - now >= MODE_SECOND : now <= v->value.deadline_ns)
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
        (now >= v->value.deadline_ns || v->value.deadline_ns - now < MODE_SECOND))
        return qa_modes_end(m, v->id, 0, e);
    if (r->source == QA_MODE_Q2_CTF && r->competition > 1)
        return true;
    if (r->source >= QA_MODE_Q3 && tied(m, v))
        return true;
    double source_seconds = 0, source_minutes = 0, source_captures = 0;
    bool source_limits = r->source == QA_MODE_THREEWAVE && m->options.hooks.source_match_limits &&
        m->options.hooks.source_match_limits(m->options.hooks.context, v->id,
            &source_seconds, &source_minutes, &source_captures);
    double minutes = source_limits ? source_minutes : r->time_limit_minutes;
    double captures = source_limits ? source_captures : r->capture_limit;
    if ((source_limits ? minutes != 0 : minutes > 0) &&
        (source_limits ? source_seconds >= (double)((float)minutes * 60.0f)
            : now - v->value.started_ns >= (uint64_t)(r->time_limit_minutes * 60.0f * (float)MODE_SECOND)))
        return source_limits ? qa_modes_end(m, v->id, 0, e)
            : limit_end(m, v, MODE_NAME_Q2_TIMELIMIT_HIT, MODE_NAME_Q2_RR_TIMELIMIT_HIT, e);
    if ((source_limits ? captures != 0 : captures > 0) &&
        r->kind >= QA_MODE_CTF && r->kind <= QA_MODE_HARVESTER) {
        for (int i = 0; i < (source_limits ? 2 : 3); ++i)
            if (v->value.team_captures[i] >= captures ||
                (r->source >= QA_MODE_Q3 && v->value.team_scores[i] >= captures))
                return qa_modes_end(m, v->id, r->source == QA_MODE_Q2_CTF
                    ? m->runtime_names[MODE_NAME_CTF_CAPTURELIMIT] : 0, e);
    }
    if (r->frag_limit > 0 && (r->source < QA_MODE_Q3 || v->value.playing >= 2) &&
        r->kind < QA_MODE_CTF) {
        if (r->source >= QA_MODE_Q3 && r->kind == QA_MODE_TEAM_DEATHMATCH) {
            if (v->value.team_scores[0] >= r->frag_limit ||
                v->value.team_scores[1] >= r->frag_limit)
                return limit_end(m, v, MODE_NAME_Q2_FRAGLIMIT_HIT, MODE_NAME_Q2_RR_FRAGLIMIT_HIT, e);
        } else
            for (size_t i = 0; i < v->value.playing; ++i) {
                int32_t score;
                if (!qa_modes_score(m, v->id, v->sorted[i], &score, e))
                    return false;
                if (score >= r->frag_limit)
                    return limit_end(m, v, MODE_NAME_Q2_FRAGLIMIT_HIT, MODE_NAME_Q2_RR_FRAGLIMIT_HIT, e);
            }
    }
    return true;
}
bool qa_modes_ghost_rejoin(qa_modes *m, qa_mode_id id, qa_actor_id actor, uint32_t code,
                           qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_player *player = mode_player_get(m, actor);
    if (!v || !player || v->value.rules.source != QA_MODE_Q2_CTF)
        return mode_fail(e, "ghost restore requires an active CTF match");
    qa_team_id current;
    if (!qa_modes_team(m, v->id, actor, &current, e))
        return false;
    if (current)
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_ALREADY_PLAYING],
                            NULL, 0, e);
    if (v->value.phase != QA_MODE_PLAYING || !v->value.deadline_ns)
        return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_NO_MATCH], NULL, 0, e);
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_ghost *ghost = &v->ghosts[i];
        if (!code || ghost->code != code)
            continue;
        if (!mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_GHOST_ACCEPTED], NULL, 0, e))
            return false;
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
        qa_builtin_player_info info;
        if (m->options.services.player_info &&
            m->options.services.player_info(m->options.services.context, actor, &info))
            ghost->number = info.slot + 1;
        if (m->options.hooks.respawn &&
            !m->options.hooks.respawn(m->options.hooks.context, v->id, actor, true, e))
            return false;
        player = mode_player_get(m, actor);
        if (!player)
            return true;
        qa_builtin_message_arg arguments[2] = {
            {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = player->identity->name},
            {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = m->runtime_names[
                ghost->team == v->value.rules.teams[0] ? MODE_NAME_CTF_RED : MODE_NAME_CTF_BLUE]}};
        return mode_message(m, v, (qa_actor_id){0}, m->runtime_names[MODE_NAME_CTF_GHOST_REINSTATED],
                            arguments, 2, e);
    }
    return mode_message(m, v, actor, m->runtime_names[MODE_NAME_CTF_GHOST_INVALID], NULL, 0, e);
}
