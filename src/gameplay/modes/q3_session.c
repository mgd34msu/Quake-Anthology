#include "internal.h"
#include "qa/modes_q3_session.h"

static bool source_mode(const mode_instance *v) {
    return v && v->value.rules.source >= QA_MODE_Q3;
}
static bool source_client(qa_modes *m, mode_instance *v, qa_actor_id actor,
                           qa_actor_owner owner, uint32_t *slot) {
    const qa_actor_record *record = qa_actors_get(
        qa_session_actors(m->options.services.session), actor);
    if (!owner || !record || record->owner != owner || !record->has_source ||
        record->source_slot >= 64 || !mode_member_get(m, v, actor)) return false;
    if (slot) *slot = record->source_slot;
    return true;
}
static bool current_member(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    return mode_member_get(m, v, actor) || mode_fail(e, "Q3 session client retired during source access");
}
static bool source_time(qa_modes *m, mode_instance *v, qa_actor_id actor,
                         int32_t *out, qa_error *e) {
    if (!m->options.hooks.q3_clock)
        return mode_fail(e, "Q3 session has no actual source clock");
    return MODE_CALLBACK(m, m->options.hooks.q3_clock(m->options.hooks.context, v->id, out, e)) &&
        current_member(m, v, actor, e);
}
bool mode_q3_session_initialize(qa_modes *m, mode_instance *v, qa_actor_id actor, qa_error *e) {
    if (!source_mode(v)) return true;
    int32_t time;
    if (!source_time(m, v, actor, &time, e)) return false;
    mode_member *p = mode_member_get(m, v, actor);
    p->player.q3_spectator_time_ms = time;
    p->player.q3_spectator_state = QA_MODE_Q3_SPECTATOR_FREE;
    p->player.q3_spectator_client = 0;
    return true;
}
bool mode_q3_session_team(qa_modes *m, mode_instance *v, qa_actor_id actor,
                          bool observer, qa_error *e) {
    if (!source_mode(v)) return true;
    int32_t time = 0;
    if (observer && !source_time(m, v, actor, &time, e)) return false;
    mode_member *p = mode_member_get(m, v, actor);
    if (!p) return mode_fail(e, "Q3 team session client retired");
    if (observer) p->player.q3_spectator_time_ms = time;
    p->player.q3_spectator_state = observer ? QA_MODE_Q3_SPECTATOR_FREE : QA_MODE_Q3_SPECTATOR_NOT;
    p->player.q3_spectator_client = 0;
    p->player.follow_target = (qa_actor_id){0};
    p->player.automatic_follow = 0;
    p->player.scoreboard = false;
    return true;
}
bool mode_q3_session_follow(qa_modes *m, mode_instance *v, qa_actor_id actor,
                            qa_actor_id target, int automatic, qa_error *e) {
    if (!source_mode(v)) return true;
    int32_t slot = -automatic;
    if (!automatic && target.registry) {
        qa_builtin_player_info info;
        if (!m->options.services.player_info ||
            !MODE_CALLBACK(m, m->options.services.player_info(m->options.services.context, target, &info)) ||
            !mode_member_get(m, v, target) || info.slot >= 64 ||
            !current_member(m, v, actor, e))
            return mode_fail(e, "Q3 follow has no current source client slot");
        slot = (int32_t)info.slot;
    }
    mode_member *p = mode_member_get(m, v, actor);
    if (!p) return mode_fail(e, "Q3 follow session client retired");
    p->player.q3_spectator_state = target.registry || automatic
        ? QA_MODE_Q3_SPECTATOR_FOLLOW : QA_MODE_Q3_SPECTATOR_FREE;
    if (target.registry || automatic) p->player.q3_spectator_client = slot;
    return true;
}

static bool read_session(qa_modes *m, mode_instance *v, qa_actor_id actor,
                           qa_actor_owner owner, uint32_t *slot,
                           qa_mode_q3_session *out, qa_error *e) {
    uint32_t number;
    if (!source_client(m, v, actor, owner, &number))
        return mode_fail(e, "Q3 session capture has no actual native client slot");
    qa_team_id team;
    if (!qa_modes_team(m, v->id, actor, &team, e)) return false;
    if (!source_client(m, v, actor, owner, NULL))
        return mode_fail(e, "Q3 session client retired during team read");
    const qa_mode_player_state *p = &mode_member_get(m, v, actor)->player;
    int32_t source_team = p->spectator ? 3 : !team ? 0
        : team == v->value.rules.teams[0] ? 1 : team == v->value.rules.teams[1] ? 2 : -1;
    if (source_team < 0) return mode_fail(e, "Q3 session team has no source identity");
    *out = (qa_mode_q3_session){source_team, p->q3_spectator_time_ms,
        p->q3_spectator_state, p->q3_spectator_client, p->wins, p->losses, p->leader ? 1 : 0};
    *slot = number;
    return true;
}
bool qa_modes_q3_session_read(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_actor_owner owner, uint32_t *slot, qa_mode_q3_session *out, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !slot || !out || m->source_restored || m->callback_depth == UINT_MAX)
        return mode_fail(e, "invalid native Q3 session capture boundary");
    return MODE_CALLBACK(m, read_session(m, v, actor, owner, slot, out, e));
}
static bool restore_session(qa_modes *m, mode_instance *v, qa_actor_id actor,
    qa_actor_owner owner, uint32_t slot, const qa_mode_q3_session *saved, qa_error *e) {
    uint32_t current;
    if (!source_client(m, v, actor, owner, &current) || current != slot)
        return mode_fail(e, "Q3 session restore requires the actual readmitted source slot");
    qa_team_id team = saved->team == 1 ? v->value.rules.teams[0]
        : saved->team == 2 ? v->value.rules.teams[1] : 0;
    if ((saved->team == 1 || saved->team == 2) && !team)
        return mode_fail(e, "Q3 session restore has no actual team identity");
    if (!qa_modes_set_team(m, v->id, actor, team, e)) return false;
    if (!source_client(m, v, actor, owner, NULL))
        return mode_fail(e, "Q3 session client retired during team restoration");
    qa_mode_player_state *p = &mode_member_get(m, v, actor)->player;
    p->spectator = saved->team == 3;
    p->spectator_since_ns = (uint64_t)(uint32_t)saved->spectator_time_ms * MODE_MILLISECOND;
    p->q3_spectator_time_ms = saved->spectator_time_ms;
    p->q3_spectator_state = saved->spectator_state;
    p->q3_spectator_client = saved->spectator_client;
    p->wins = saved->wins; p->losses = saved->losses; p->leader = saved->team_leader != 0;
    p->follow_target = (qa_actor_id){0};
    p->automatic_follow = saved->spectator_state == QA_MODE_Q3_SPECTATOR_FOLLOW &&
        saved->spectator_client < 0 ? (int8_t)-saved->spectator_client : 0;
    p->scoreboard = saved->spectator_state == QA_MODE_Q3_SPECTATOR_SCOREBOARD;
    return true;
}
bool qa_modes_q3_session_restore(qa_modes *m, qa_mode_id id, qa_actor_id actor,
    qa_actor_owner owner, uint32_t slot, const qa_mode_q3_session *saved, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !saved || m->source_restored || m->callback_depth == UINT_MAX ||
        saved->team < 0 || saved->team > 3 || saved->spectator_state < 0 ||
        saved->spectator_state > QA_MODE_Q3_SPECTATOR_SCOREBOARD ||
        saved->spectator_client < -2 || saved->spectator_client >= 64 ||
        saved->team_leader < 0 || saved->team_leader > 1)
        return mode_fail(e, "invalid native Q3 session restoration boundary");
    qa_mode_q3_session copy = *saved;
    return MODE_CALLBACK(m, restore_session(m, v, actor, owner, slot, &copy, e));
}
bool qa_modes_q3_session_reconnect(qa_modes *m, qa_mode_id id, qa_actor_owner owner, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !owner || !qa_modes_idle(m))
        return mode_fail(e, "Q3 session reconnect requires its idle actual source roster");
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.ids[i];
        mode_member *p = mode_member_get(m, v, actor);
        if (!p) continue;
        if (!source_client(m, v, actor, owner, NULL))
            return mode_fail(e, "Q3 session reconnect contains a foreign client owner");
        p->player.follow_target = (qa_actor_id){0};
        if (p->player.q3_spectator_state != QA_MODE_Q3_SPECTATOR_FOLLOW ||
            p->player.q3_spectator_client < 0) continue;
        const qa_actor_record *target = qa_actors_at_source(
            qa_session_actors(m->options.services.session), owner,
            (uint32_t)p->player.q3_spectator_client);
        if (target && mode_member_get(m, v, target->id)) p->player.follow_target = target->id;
    }
    return true;
}

bool qa_modes_q3_round_reset(qa_modes *m, qa_mode_id id, int32_t source_time_ms,
                              int32_t restarted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !qa_modes_idle(m) || !v->q3_settings_present || v->horde ||
        !m->options.hooks.q3_clock || !m->options.hooks.q3_warmup_restart ||
        !qa_session_safe(m->options.services.session) ||
        !qa_world_idle(m->options.services.world) || !qa_combat_idle(m->options.services.combat) ||
        qa_actors_count(qa_session_actors(m->options.services.session)))
        return mode_fail(e, "Q3 mode round reset requires genuine complete canonical retirement");
    for (uint32_t i = 0; i < m->actor_capacity; ++i)
        if (m->players[i].active || m->objects[i].active || m->objects[i].admitting ||
            v->members[i].joined || v->bindings[i].serial || v->base_admitting[0] ||
            v->base_admitting[1] || v->base_admitting[2])
            return mode_fail(e, "Q3 mode round reset has retained actor admission state");
    qa_mode_rules rules = v->value.rules;
    qa_mode_q3_settings settings = v->q3_settings;
    mode_member *members = v->members;
    mode_match_owner *bindings = v->bindings;
    mode_ghost *ghosts = v->ghosts;
    qa_actor_id *sorted = v->sorted;
    mode_rank_entry *ranks = v->ranks;
    memset(members, 0, m->actor_capacity * sizeof(*members));
    memset(bindings, 0, m->actor_capacity * sizeof(*bindings));
    memset(ghosts, 0, m->actor_capacity * sizeof(*ghosts));
    memset(sorted, 0, m->actor_capacity * sizeof(*sorted));
    memset(ranks, 0, m->actor_capacity * sizeof(*ranks));
    free(v->spawns);
    free(v->items);
    *v = (mode_instance){.id = id, .active = true, .members = members,
        .bindings = bindings, .ghosts = ghosts, .sorted = sorted, .ranks = ranks,
        .rune_cursor = SIZE_MAX, .rune_forward = true};
    v->value.rules = rules;
    v->value.time_ns = (uint64_t)(uint32_t)source_time_ms * MODE_MILLISECOND;
    v->value.started_ns = v->value.time_ns;
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *o = &m->objectives[i];
        if (o->active && o->binding.mode.slot == id.slot && o->binding.mode.generation == id.generation)
            *o = (mode_objective){0};
    }
    return qa_modes_q3_settings_admit(m, id, &settings, source_time_ms, restarted, e);
}
