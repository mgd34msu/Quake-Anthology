#include "internal.h"
#include "qa/modes_q3_session.h"
#include "qa/modes_q3_clients.h"

static bool source_mode(const mode_instance *v) {
    return v && v->value.rules.source >= QA_MODE_Q3;
}

static bool source_time(qa_modes *m, mode_instance *v, qa_actor_id actor,
    int32_t *out, qa_error *e) {
    if (!m->options.hooks.q3_clock)
        return mode_fail(e, "Q3 rule member has no actual source clock");
    return MODE_CALLBACK(m, m->options.hooks.q3_clock(m->options.hooks.context, v->id, out, e)) &&
        (mode_member_get(m, v, actor) || mode_fail(e, "Q3 rule member retired during source access"));
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
    if (!p) return mode_fail(e, "Q3 team rule member retired");
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
        qa_actor_owner owner;
        uint32_t actual_slot;
        if (!m->options.hooks.q3_client_slot ||
            !MODE_CALLBACK(m, m->options.hooks.q3_client_slot(m->options.hooks.context,
                v->id, target, &owner, &actual_slot, e)) ||
            !mode_member_get(m, v, target) || actual_slot >= 64 || !mode_member_get(m, v, actor))
            return mode_fail(e, "Q3 rule follow has no current source client slot");
        slot = (int32_t)actual_slot;
    }
    mode_member *p = mode_member_get(m, v, actor);
    if (!p) return mode_fail(e, "Q3 follow rule member retired");
    p->player.q3_spectator_state = target.registry || automatic
        ? QA_MODE_Q3_SPECTATOR_FOLLOW : QA_MODE_Q3_SPECTATOR_FREE;
    if (target.registry || automatic) p->player.q3_spectator_client = slot;
    return true;
}

bool qa_modes_q3_client_begin_state(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_error *e) {
    mode_instance *v = m ? mode_get(m, id) : NULL;
    mode_member *p = mode_member_get(m, v, actor);
    if (!source_mode(v) || !p)
        return mode_fail(e, "ClientBegin rule state has no actual Q3 member");
    p->stats.q3_defend_count = p->stats.q3_assist_count = p->stats.q3_capture_count = 0;
    p->stats.rank = 0;
    return qa_modes_set_score(m, id, actor, 0, e);
}

bool qa_modes_q3_round_reset(qa_modes *m, qa_mode_id id, int32_t source_time_ms,
    uint64_t source_start_ns, int32_t restarted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if ((uint32_t)(source_start_ns / MODE_MILLISECOND) != (uint32_t)source_time_ms ||
        !source_mode(v) || !qa_modes_idle(m) || !v->q3_settings_present || v->horde ||
        !m->options.hooks.q3_clock || !m->options.hooks.q3_warmup_restart ||
        !qa_session_safe(m->options.services.session) ||
        !qa_world_idle(m->options.services.world) || !qa_combat_idle(m->options.services.combat) ||
        qa_actors_count(qa_session_actors(m->options.services.session)))
        return mode_fail(e, "Q3 mode round reset requires complete canonical retirement");
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
    v->value.time_ns = source_start_ns;
    v->value.started_ns = v->value.time_ns;
    for (uint32_t i = 0; i < m->objective_capacity; ++i) {
        mode_objective *o = &m->objectives[i];
        if (o->active && o->binding.mode.slot == id.slot && o->binding.mode.generation == id.generation)
            *o = (mode_objective){0};
    }
    return qa_modes_q3_settings_admit(m, id, &settings, source_time_ms, restarted, e);
}
