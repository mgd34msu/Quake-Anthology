#include "internal.h"
#include "qa/game_q2_wire.h"

const char *qa_modes_q2_statusbar_text(void) {
    return "yb -24 xv 0 hnum xv 50 pic 0 "
           "if 2 xv 100 anum xv 150 pic 2 endif "
           "if 4 xv 200 rnum xv 250 pic 4 endif "
           "if 6 xv 296 pic 6 endif yb -50 "
           "if 7 xv 0 pic 7 xv 26 yb -42 stat_string 8 yb -50 endif "
           "if 9 xv 246 num 2 10 xv 296 pic 9 endif "
           "if 11 xv 148 pic 11 endif xr -50 yt 2 num 3 14 "
           "yb -129 if 26 xr -26 pic 26 endif "
           "yb -102 if 17 xr -26 pic 17 endif xr -62 num 2 18 "
           "if 22 yb -104 xr -28 pic 22 endif "
           "yb -75 if 19 xr -26 pic 19 endif xr -62 num 2 20 "
           "if 23 yb -77 xr -28 pic 23 endif "
           "if 21 yt 26 xr -24 pic 21 endif "
           "if 27 xv 0 yb -58 string \"Viewing\" xv 64 stat_string 27 endif "
           "if 28 xl 0 yb -78 stat_string 28 endif ";
}

static bool ctf_hud(const mode_instance *v) {
    return v && v->value.rules.enabled && v->value.rules.source == QA_MODE_Q2_CTF;
}

bool qa_modes_q2_hud_resources(qa_modes *m, qa_mode_id id,
                              qa_string_id images[QA_MODE_Q2_HUD_IMAGES],
                              qa_string_id *statusbar) {
    if (!ctf_hud(mode_get(m, id)))
        return false;
    if (images)
        for (unsigned i = 0; i < QA_MODE_Q2_HUD_IMAGES; ++i)
            images[i] = m->runtime_names[MODE_NAME_PIC_I_CTF1 + i];
    if (statusbar)
        *statusbar = m->runtime_names[MODE_NAME_CTF_STATUSBAR];
    return true;
}

static qa_string_id flag_picture(qa_modes *m, const mode_instance *v, unsigned team) {
    mode_object *flag = mode_object_get(m, v->bases[team]);
    unsigned state = flag && flag->value.phase == QA_OBJECTIVE_CARRIED ? 4
                     : flag && flag->value.phase != QA_OBJECTIVE_HOME ? 2 : 0;
    return m->runtime_names[MODE_NAME_PIC_I_CTF1 + state + team];
}

static bool ctf_id_view(qa_modes *m, qa_actor_id actor,
                        const qa_q2_wire_stat_resources *resources,
                        int16_t *stat, qa_error *error) {
    qa_body_state viewer;
    qa_vec3 angles, forward;
    *stat = 0;
    if (!resources->player_skin || !m->options.hooks.player_view ||
        !m->options.hooks.player_view(m->options.hooks.context, actor, &angles) ||
        !qa_world_body_read(m->options.services.world, actor, &viewer, NULL))
        return true;
    qa_angle_vectors(angles, &forward, NULL, NULL, QA_ANGLE_NATIVE_MATH);
    qa_trace_query query = {
        .start = viewer.origin, .end = qa_vec_ma(viewer.origin, 1024, forward),
        .pass_actor = actor,
        .policy = {.behavior = &qa_trace_behaviors[QA_RULESET_Q2_CLASSIC],
            .contents_mask = qa_collision_contents_mask(3, QA_GAME_Q2), .q1_hull = -1}};
    qa_trace_result trace;
    if (!qa_world_trace(m->options.services.world, &query, &trace, error))
        return false;
    qa_builtin_player_info info;
    uint32_t index = 0;
    if (trace.fraction < 1 && trace.actor.registry && m->options.services.player_info &&
        m->options.services.player_info(m->options.services.context, trace.actor, &info)) {
        if (!resources->player_skin(resources->player_context, actor, &index, error))
            return false;
        qa_q2_wire_stat_word((int32_t)(index & UINT16_MAX), stat);
        return true;
    }

    float best_dot = 0;
    uint32_t best_index = 0;
    qa_physics *physics = m->options.services.physics;
    for (size_t i = 0; i < m->players_order.count; ++i) {
        qa_actor_id candidate = m->players_order.actors[i];
        mode_player *player = mode_player_get(m, candidate);
        qa_physics_properties properties;
        qa_body_state body;
        if (!player || !player->connected || !physics || !physics->services.read ||
            !physics->services.read(physics->services.context, candidate, &properties) ||
            properties.solid == QA_PHYSICS_NOT_SOLID ||
            !qa_world_body_read(m->options.services.world, candidate, &body, NULL))
            continue;
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(body.origin, viewer.origin));
        float dot = qa_vec_dot(forward, direction);
        if (dot < best_dot || dot <= .90f || !mode_visible(m, candidate, actor, false))
            continue;
        if (!resources->player_skin(resources->player_context, candidate, &index, error))
            return false;
        if (!index || (dot == best_dot && index >= best_index))
            continue;
        best_dot = dot;
        best_index = index;
    }
    qa_q2_wire_stat_word((int32_t)(best_index & UINT16_MAX), stat);
    return true;
}

bool qa_modes_q2_hud_stats(qa_modes *m, qa_mode_id id, qa_actor_id actor,
                          uint64_t source_frame,
                          const qa_q2_wire_stat_resources *resources,
                          int16_t stats[64], qa_error *error) {
    mode_instance *v = mode_get(m, id);
    mode_member *member = mode_member_get(m, v, actor);
    if (!ctf_hud(v) || !member)
        return true;
    qa_team_id team;
    if (!qa_modes_team(m, id, actor, &team, error))
        return false;
    v = mode_get(m, id);
    member = mode_member_get(m, v, actor);
    if (!ctf_hud(v) || !member)
        return true;

    qa_mode_view value = v->value;
    qa_string_id pictures[12] = {0};
    int16_t projected[12] = {0};
    pictures[0] = flag_picture(m, v, 0);
    pictures[2] = flag_picture(m, v, 1);
    pictures[7] = m->runtime_names[MODE_NAME_PIC_CTFSB1];
    pictures[8] = m->runtime_names[MODE_NAME_PIC_CTFSB2];
    qa_q2_wire_stat_word(value.team_captures[0], &projected[1]);
    qa_q2_wire_stat_word(value.team_captures[1], &projected[3]);

    bool blink = (source_frame & 8u) != 0;
    if ((value.phase == QA_MODE_INTERMISSION || value.phase == QA_MODE_FINISHED) && blink) {
        int winner = value.team_captures[0] > value.team_captures[1] ? 0
                     : value.team_captures[1] > value.team_captures[0] ? 1
                     : value.team_scores[0] > value.team_scores[1] ? 0
                     : value.team_scores[1] > value.team_scores[0] ? 1 : -1;
        if (winner < 0)
            pictures[7] = pictures[8] = QA_STRING_NONE;
        else
            pictures[7 + winner] = QA_STRING_NONE;
    }
    if (value.last_capture_ns && value.time_ns >= value.last_capture_ns &&
        value.time_ns - value.last_capture_ns < 5 * MODE_SECOND && !blink) {
        int captured = value.last_capture_team == value.rules.teams[0] ? 0
                       : value.last_capture_team == value.rules.teams[1] ? 1 : -1;
        if (captured >= 0)
            pictures[captured * 2] = QA_STRING_NONE;
    }

    int joined = mode_team_index(v, team);
    if (joined >= 0 && joined < 2) {
        pictures[5 + joined] = m->runtime_names[MODE_NAME_PIC_I_CTFJ];
        mode_object *flag = mode_object_get(m, member->flag);
        if (blink && flag && flag->spec.team == value.rules.teams[joined ^ 1])
            pictures[4] = m->runtime_names[MODE_NAME_PIC_I_CTF1 + (joined ^ 1)];
    }
    mode_object *relic = mode_object_get(m, member->relic);
    if (relic && relic->spec.kind == QA_MODE_OBJECT_RELIC &&
        relic->spec.relic <= QA_RELIC_REGENERATION)
        pictures[9] = m->runtime_names[MODE_NAME_PIC_TECH1 + relic->spec.relic];

    for (unsigned i = 0; i < sizeof(pictures) / sizeof(pictures[0]); ++i) {
        if (!pictures[i])
            continue;
        uint32_t image;
        if (!resources->image(resources->context, pictures[i], &image, error))
            return false;
        qa_q2_wire_stat_word((int32_t)(image & UINT16_MAX), &projected[i]);
    }
    member = mode_member_get(m, mode_get(m, id), actor);
    if (member && member->player.ctf_id_view &&
        !ctf_id_view(m, actor, resources, &projected[10], error))
        return false;
    const char *match_text; bool match_visible; uint64_t match_revision;
    if (!qa_modes_q2_match_status(m, id, &match_text, &match_visible, &match_revision, error))
        return false;
    qa_q2_wire_stat_word(match_visible ? (int32_t)resources->match_index : 0, &projected[11]);
    memcpy(stats + 17, projected, sizeof(projected));
    return true;
}
