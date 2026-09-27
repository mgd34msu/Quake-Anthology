#include "internal.h"

static bool execute_vote(qa_modes *, mode_instance *, qa_mode_vote *, qa_error *);
static int vote_slot(mode_instance *v, qa_team_id team) {
    if (!team)
        return 0;
    int index = mode_team_index(v, team);
    return index < 0 ? -1 : index + 1;
}
static size_t voters(qa_modes *m, mode_instance *v, qa_team_id team) {
    size_t count = 0;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *member = &v->members[i];
        mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
        if (!p || !p->value.connected ||
            (v->value.rules.source >= QA_MODE_Q3 && (p->value.bot || p->value.connecting)))
            continue;
        if (v->value.rules.source >= QA_MODE_Q3 && p->value.spectator)
            continue;
        qa_team_id player_team;
        if (team && (!qa_modes_team(m, member->actor, &player_team, NULL) || player_team != team))
            continue;
        ++count;
    }
    return count;
}
bool qa_modes_vote_start(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team,
                         const qa_match_intent *intent, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!v || !member || !intent || intent->kind < QA_MATCH_NEXT_MAP ||
        intent->kind > QA_MATCH_FRAG_LIMIT || !isfinite(intent->value) ||
        (intent->kind == QA_MATCH_GAME_TYPE &&
         (intent->game_type < QA_MODE_FFA || intent->game_type > QA_MODE_HARVESTER ||
          intent->game_type == QA_MODE_SINGLE_PLAYER)))
        return mode_fail(e, "invalid vote");
    int slot = vote_slot(v, team);
    if (slot < 0)
        return mode_fail(e, "unknown vote team");
    qa_mode_source source = v->value.rules.source;
    if ((intent->kind == QA_MATCH_SELECTED_MAP ||
         (intent->kind == QA_MATCH_START && intent->map)) &&
        (!intent->map || !m->options.hooks.map_allowed ||
         !m->options.hooks.map_allowed(m->options.hooks.context, id, intent->map)))
        return mode_fail(e, "vote map is not admitted");
    mode_player *initiator = mode_player_get(m, actor);
    if (v->value.rules.voting_disabled || !initiator || !initiator->value.connected ||
        (source >= QA_MODE_Q3 &&
         (initiator->value.bot || initiator->value.spectator || initiator->value.connecting)) ||
        (v->value.rules.vote_limit &&
         member->vote_calls[slot] >= (uint32_t)v->value.rules.vote_limit))
        return mode_fail(e, "player cannot initiate this vote");
    if (source == QA_MODE_LMCTF)
        for (uint32_t i = 0; i < m->actor_capacity; ++i)
            v->members[i].ballots[slot] = 0;
    qa_mode_vote *vote = &v->votes[slot];
    if (vote->active)
        return mode_fail(e, "vote already in progress");
    if (vote->passed) {
        if (source < QA_MODE_Q3)
            return mode_fail(e, "vote is waiting to execute");
        if (!execute_vote(m, v, vote, e))
            return false;
    }
    size_t count = voters(m, v, team);
    if (source == QA_MODE_LMCTF && count < 4)
        return mode_fail(e, "LMCTF votes need four players");
    if (source == QA_MODE_Q2_CTF && (count < 2 || v->value.rules.election_percent <= 0))
        return mode_fail(e, "CTF election needs two players and an enabled threshold");
    if (!count)
        return mode_fail(e, "vote has no eligible players");
    if (team) {
        qa_team_id t;
        if (!qa_modes_team(m, actor, &t, e))
            return false;
        if (t != team)
            return mode_fail(e, "vote initiator is outside the voting team");
    }
    if (source != QA_MODE_LMCTF)
        for (uint32_t i = 0; i < m->actor_capacity; ++i)
            v->members[i].ballots[slot] = 0;
    uint64_t duration = (source == QA_MODE_Q2_CTF ? 20 : 30) * MODE_SECOND;
    int32_t needed = source == QA_MODE_Q2_CTF
                         ? (int32_t)(count * (size_t)v->value.rules.election_percent / 100)
                         : (int32_t)(count / 2 + 1);
    if (needed < 1)
        needed = 1;
    *vote = (qa_mode_vote){.intent = *intent,
                           .initiator = actor,
                           .team = team,
                           .deadline_ns = v->value.time_ns + duration,
                           .needed = needed,
                           .active = true};
    vote->intent.mode = id;
    v->vote_started[slot] = v->value.time_ns;
    if (member->vote_calls[slot] != UINT32_MAX)
        ++member->vote_calls[slot];
    if (source != QA_MODE_Q2_CTF) {
        member->ballots[slot] = 1;
        vote->yes = 1;
    }
    return mode_event(m, v, QA_MODE_VOTE_STARTED, actor, (qa_actor_id){0}, (qa_actor_id){0}, team,
                      needed, intent->kind, e);
}
bool qa_modes_vote_cast(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_team_id team, bool yes,
                        qa_error *e) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    int slot = v ? vote_slot(v, team) : -1;
    if (!member || slot < 0 || !v->votes[slot].active)
        return mode_fail(e, "no active vote");
    qa_mode_vote *vote = &v->votes[slot];
    mode_player *p = mode_player_get(m, actor);
    if (!p || !p->value.connected ||
        (v->value.rules.source >= QA_MODE_Q3 &&
         (p->value.bot || p->value.connecting || p->value.spectator)))
        return mode_fail(e, "player cannot vote");
    qa_team_id own;
    if (team && (!qa_modes_team(m, actor, &own, e) || own != team))
        return mode_fail(e, "player is outside voting team");
    int8_t before = member->ballots[slot];
    if (v->value.rules.source != QA_MODE_LMCTF && before)
        return mode_fail(e, "player already voted");
    if (v->value.rules.source == QA_MODE_Q2_CTF && qa_actor_id_equal(actor, vote->initiator))
        return mode_fail(e, "CTF election initiator cannot vote");
    if (before > 0)
        --vote->yes;
    else if (before < 0)
        --vote->no;
    member->ballots[slot] = yes ? 1 : -1;
    if (yes)
        ++vote->yes;
    else
        ++vote->no;
    return true;
}
bool qa_modes_vote_read(qa_modes *m, qa_mode_id id, qa_team_id team, qa_mode_vote *out,
                        qa_error *e) {
    mode_instance *v = mode_get(m, id);
    int slot = v ? vote_slot(v, team) : -1;
    if (slot < 0 || !out)
        return mode_fail(e, "invalid vote read");
    *out = v->votes[slot];
    return true;
}
static bool execute_vote(qa_modes *m, mode_instance *v, qa_mode_vote *vote, qa_error *e) {
    qa_match_intent intent = vote->intent;
    vote->passed = false;
    if (intent.kind == QA_MATCH_START)
        return qa_modes_start(m, v->id, e);
    if (intent.kind == QA_MATCH_CANCEL)
        return qa_modes_cancel(m, v->id, e);
    if (intent.kind == QA_MATCH_ADMIN) {
        mode_member *member = mode_member_get(m, v, intent.actor);
        if (member)
            member->admin = true;
        return true;
    }
    if (intent.kind == QA_MATCH_KICK_PLAYER) {
        if (!mode_live(m, intent.actor))
            return true;
        return m->options.hooks.disconnect
                   ? MODE_CALLBACK(
                         m, m->options.hooks.disconnect(m->options.hooks.context, intent.actor, e))
                   : mode_fail(e, "kick vote has no connection coordinator");
    }
    if (intent.kind == QA_MATCH_TEAM_LEADER) {
        mode_player *target = mode_player_get(m, intent.actor);
        qa_team_id team;
        if (!target || !qa_modes_team(m, intent.actor, &team, e) || team != intent.team)
            return true;
        for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
            uint32_t i = m->players_order.ids[ordinal].slot;
            mode_member *member = &v->members[i];
            mode_player *p = member->joined ? mode_player_get(m, member->actor) : NULL;
            qa_team_id own;
            if (p && qa_modes_team(m, member->actor, &own, NULL) && own == team)
                p->value.leader = false;
        }
        target->value.leader = true;
        return mode_event(m, v, QA_MODE_ROSTER, intent.actor, (qa_actor_id){0}, (qa_actor_id){0},
                          team, 1, 0, e);
    }
    if (!m->options.hooks.intent)
        return mode_fail(e, "vote has no match coordinator");
    return MODE_CALLBACK(m, m->options.hooks.intent(m->options.hooks.context, &intent, e));
}
bool mode_vote_frame(qa_modes *m, mode_instance *v, qa_error *e) {
    for (int slot = 0; slot < 4; ++slot) {
        qa_mode_vote *vote = &v->votes[slot];
        if (vote->passed && v->value.time_ns >= vote->execute_ns) {
            if (!execute_vote(m, v, vote, e))
                return false;
        }
        if (!vote->active)
            continue;
        bool finished = false, pass = false;
        qa_mode_source source = v->value.rules.source;
        if (source == QA_MODE_LMCTF) {
            if (v->value.time_ns <= vote->deadline_ns)
                continue;
            vote->yes = vote->no = 0;
            for (size_t i = 0; i < m->players_order.count; ++i) {
                qa_actor_id actor = m->players_order.ids[i];
                mode_member *member = mode_member_get(m, v, actor);
                mode_player *player = mode_player_get(m, actor);
                if (!member || !player || !player->value.connected)
                    continue;
                if (member->ballots[slot] > 0)
                    ++vote->yes;
                else if (member->ballots[slot] < 0)
                    ++vote->no;
            }
            int32_t total = vote->yes + vote->no;
            int32_t percent = total ? 100 * vote->yes / total : 0;
            if (total && 100 * vote->yes % total > (total >> 1))
                ++percent;
            finished = true;
            pass = total >= 2 && percent >= 75;
        } else if (source == QA_MODE_Q2_CTF) {
            if (vote->yes >= vote->needed) {
                finished = true;
                pass = true;
            } else
                finished = v->value.time_ns >= vote->deadline_ns;
        } else {
            size_t count = voters(m, v, vote->team);
            if (vote->yes > (int32_t)(count / 2)) {
                finished = true;
                pass = true;
            } else if (vote->no >= (int32_t)(count / 2) || v->value.time_ns >= vote->deadline_ns)
                finished = true;
        }
        if (!finished)
            continue;
        vote->active = false;
        vote->passed = pass;
        vote->execute_ns =
            v->value.time_ns + (source >= QA_MODE_Q3 && slot == 0 ? 3 * MODE_SECOND : 0);
        if (!mode_event(m, v, pass ? QA_MODE_VOTE_PASSED : QA_MODE_VOTE_FAILED, vote->initiator,
                        (qa_actor_id){0}, (qa_actor_id){0}, vote->team, vote->yes, vote->no, e))
            return false;
        if (pass && vote->execute_ns == v->value.time_ns && !execute_vote(m, v, vote, e))
            return false;
    }
    return true;
}
