#include "internal.h"

static bool source_mode(const mode_instance *v) {
    return v && v->value.rules.source >= QA_MODE_Q3 &&
        v->value.rules.kind >= QA_MODE_FFA && v->value.rules.kind <= QA_MODE_HARVESTER;
}
static int32_t signed_bits(uint32_t bits) {
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
static bool source_clock(qa_modes *m, mode_instance *v, int32_t *out, qa_error *e) {
    return m->options.hooks.q3_clock
        ? MODE_CALLBACK(m, m->options.hooks.q3_clock(m->options.hooks.context, v->id, out, e))
        : mode_fail(e, "Q3 settings have no actual source clock");
}
bool qa_modes_q3_settings_read(const qa_modes *m, qa_mode_id id, qa_mode_q3_settings *out,
                               bool *present, qa_error *e) {
    const mode_instance *v = m && id.slot < m->mode_capacity ? &m->instances[id.slot] : NULL;
    if (!v || !v->active || v->id.generation != id.generation || !out || !present || !source_mode(v))
        return mode_fail(e, "Q3 settings query requires an actual source mode");
    *out = v->q3_settings;
    *present = v->q3_settings_present;
    return true;
}
bool qa_modes_q3_settings_admit(qa_modes *m, qa_mode_id id, const qa_mode_q3_settings *settings,
                                int32_t source_time_ms, int32_t restarted, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !settings || m->callback_depth || m->source_restored ||
        v->q3_settings_present || !m->options.hooks.q3_clock || !m->options.hooks.q3_warmup_restart)
        return mode_fail(e, "Q3 settings admission requires idle actual source owners");
    v->q3_settings = *settings;
    v->q3_settings_present = true;
    v->q3_started_ms = source_time_ms;
    v->q3_warmup_ms = restarted || !settings->do_warmup ? 0 : -1;
    v->q3_warmup_seen = settings->warmup_modification_count;
    v->value.phase = v->q3_warmup_ms ? QA_MODE_WAITING : QA_MODE_PLAYING;
    v->value.deadline_ns = 0;
    return true;
}
bool qa_modes_q3_settings_update(qa_modes *m, qa_mode_id id, const qa_mode_q3_settings *settings,
                                 qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !settings || !v->q3_settings_present || m->callback_depth || m->source_restored)
        return mode_fail(e, "Q3 settings update requires its idle admitted source owner");
    v->q3_settings = *settings;
    return true;
}
bool qa_modes_q3_settings_rebind(qa_modes *m, qa_mode_id id, const qa_mode_q3_settings *settings,
                                qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!source_mode(v) || !settings || !v->q3_settings_present || m->callback_depth ||
        v->q3_settings.do_warmup != settings->do_warmup ||
        v->q3_settings.warmup_seconds != settings->warmup_seconds ||
        v->q3_settings.time_limit_minutes != settings->time_limit_minutes ||
        v->q3_settings.frag_limit != settings->frag_limit ||
        v->q3_settings.capture_limit != settings->capture_limit)
        return mode_fail(e, "Q3 settings rebind requires the same actual copied gameplay values");
    bool observed = v->q3_warmup_seen == v->q3_settings.warmup_modification_count;
    v->q3_settings.warmup_modification_count = settings->warmup_modification_count;
    v->q3_warmup_seen = observed ? settings->warmup_modification_count :
        settings->warmup_modification_count - 1;
    return true;
}
static bool wait(qa_modes *m, mode_instance *v, qa_error *e) {
    if (v->q3_warmup_ms == -1 && v->value.phase == QA_MODE_WAITING) return true;
    v->q3_warmup_ms = -1;
    v->restart_sent = false;
    return mode_set_phase(m, v, QA_MODE_WAITING, 0, e);
}
static bool enough_team(qa_modes *m, mode_instance *v, qa_team_id team, bool *found, qa_error *e) {
    *found = false;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id actor = m->players_order.ids[i];
        mode_member *member = mode_member_get(m, v, actor);
        mode_player *player = member ? mode_player_get(m, actor) : NULL;
        if (!player || !player->connected || player->connecting || member->player.spectator) continue;
        qa_team_id own;
        if (!qa_modes_team(m, v->id, actor, &own, e)) return false;
        if (mode_member_get(m, v, actor) && own == team) { *found = true; return true; }
    }
    return true;
}
bool mode_q3_warmup_frame(qa_modes *m, mode_instance *v, qa_error *e) {
    if (!v->value.playing) return true;
    if (v->value.rules.kind == QA_MODE_DUEL) {
        if (v->value.playing != 2) return wait(m, v, e);
    } else if (v->value.rules.kind == QA_MODE_SINGLE_PLAYER) return true;
    if (!v->q3_warmup_ms)
        return v->value.phase == QA_MODE_PLAYING || mode_set_phase(m, v, QA_MODE_PLAYING, 0, e);
    if (v->value.rules.kind > QA_MODE_TEAM_DEATHMATCH) {
        bool red, blue;
        if (!enough_team(m, v, v->value.rules.teams[0], &red, e) ||
            !enough_team(m, v, v->value.rules.teams[1], &blue, e)) return false;
        if (!red || !blue) return wait(m, v, e);
    } else if (v->value.playing < 2) return wait(m, v, e);
    if (v->q3_warmup_seen != v->q3_settings.warmup_modification_count) {
        v->q3_warmup_seen = v->q3_settings.warmup_modification_count;
        v->q3_warmup_ms = -1;
    }
    int32_t now;
    if (!source_clock(m, v, &now, e)) return false;
    if (v->q3_warmup_ms < 0) {
        uint32_t delay = ((uint32_t)v->q3_settings.warmup_seconds - 1u) * 1000u;
        v->q3_warmup_ms = signed_bits((uint32_t)now + delay);
        v->restart_sent = false;
        return mode_set_phase(m, v, QA_MODE_COUNTDOWN,
                              (uint64_t)(uint32_t)v->q3_warmup_ms * MODE_MILLISECOND, e);
    }
    if (v->restart_sent || now <= v->q3_warmup_ms) return true;
    if (!m->options.hooks.q3_warmup_restart) return mode_fail(e, "Q3 warmup has no actual restart flag writer");
    v->q3_warmup_ms = signed_bits((uint32_t)v->q3_warmup_ms + 10000u);
    if (!MODE_CALLBACK(m, m->options.hooks.q3_warmup_restart(m->options.hooks.context, v->id, e)) ||
        !mode_intent(m, v, QA_MATCH_RESTART_MAP, (qa_actor_id){0}, 0, 0, e)) return false;
    v->restart_sent = true;
    return true;
}
bool mode_q3_limits(qa_modes *m, mode_instance *v, qa_error *e) {
    const qa_mode_q3_settings *settings = &v->q3_settings;
    if (settings->time_limit_minutes && !v->q3_warmup_ms) {
        int32_t now;
        if (!source_clock(m, v, &now, e)) return false;
        int32_t elapsed = signed_bits((uint32_t)now - (uint32_t)v->q3_started_ms);
        int32_t limit = signed_bits((uint32_t)settings->time_limit_minutes * 60000u);
        if (elapsed >= limit) return qa_modes_end(m, v->id, 0, e);
    }
    if (v->value.playing < 2) return true;
    if (v->value.rules.kind < QA_MODE_CTF && settings->frag_limit) {
        if (v->value.team_scores[0] >= settings->frag_limit ||
            v->value.team_scores[1] >= settings->frag_limit) return qa_modes_end(m, v->id, 0, e);
        for (size_t i = 0; i < m->players_order.count; ++i) {
            qa_actor_id actor = m->players_order.ids[i];
            mode_member *member = mode_member_get(m, v, actor);
            mode_player *player = member ? mode_player_get(m, actor) : NULL;
            if (!player || !player->connected || player->connecting || member->player.spectator) continue;
            qa_team_id team;
            int32_t score;
            if (!qa_modes_team(m, v->id, actor, &team, e)) return false;
            if (!mode_member_get(m, v, actor) || team) continue;
            if (!qa_modes_score(m, v->id, actor, &score, e)) return false;
            if (mode_member_get(m, v, actor) && score >= settings->frag_limit)
                return qa_modes_end(m, v->id, 0, e);
        }
    }
    if (v->value.rules.kind >= QA_MODE_CTF && settings->capture_limit &&
        (v->value.team_scores[0] >= settings->capture_limit || v->value.team_scores[1] >= settings->capture_limit))
        return qa_modes_end(m, v->id, 0, e);
    return true;
}
