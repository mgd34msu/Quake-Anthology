#include "internal.h"

bool mode_ball_reset(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    body.angles = (qa_vec3){0};
    body.velocity = (qa_vec3){0};
    o->physics.angular_velocity = (qa_vec3){0};
    o->value.phase = QA_OBJECTIVE_DISABLED;
    o->next_ns = v->value.time_ns + 2 * MODE_SECOND;
    return qa_world_body_write(m->options.services.world, o->actor, &body, e) &&
           mode_object_sync(m, o, e) &&
           mode_event(m, v, QA_MODE_BALL_GOAL, v->last_ball_touch, (qa_actor_id){0}, o->actor, 0, 0,
                      0, e);
}
bool mode_ball_touch(qa_modes *m, mode_instance *v, mode_object *o, qa_actor_id actor,
                     bool *accepted, qa_error *e) {
    if (v->value.rules.kind != QA_MODE_DEATHBALL)
        return true;
    if (o->spec.kind == QA_MODE_OBJECT_BALL) {
        mode_member *member = mode_member_get(m, v, actor);
        if (!member || member->player.spectator)
            return true;
        qa_body_state ball, player;
        qa_combat_state state;
        if (!qa_world_body_read(m->options.services.world, o->actor, &ball, e) ||
            !qa_world_body_read(m->options.services.world, actor, &player, e) ||
            !qa_combat_read(m->options.services.combat, actor, &state, e))
            return false;
        float speed = qa_vec_length(ball.velocity);
        if (!state.can_take_damage || speed == 0 ||
            qa_vec_dot(qa_vec_sub(ball.origin, player.origin), ball.velocity) <= .7f)
            return true;
        qa_damage_request request = {
            .target = actor,
            .amount = truncf(speed / 10),
            .knockback = truncf(speed / 10),
            .point = ball.origin,
            .attack = {
                .attacker = o->actor,
                .inflictor = o->actor,
                .combat_provider = m->options.owner,
                .weapon_provider = m->options.owner,
                .time_ns = v->value.time_ns,
                .cause = {.kind = QA_CAUSE_Q2,
                          .source.q2 = {.means_of_death = 52, .native = QA_Q2_CAUSE_CLASSIC}}}};
        return mode_damage(m, QA_GAME_Q2, &request, e);
    }
    if (!qa_actor_id_equal(actor, v->ball))
        return true;
    mode_object *ball = mode_object_get(m, v->ball);
    if (!ball || ball->value.phase == QA_OBJECTIVE_DISABLED)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, actor, &body, e))
        return false;
    if (o->spec.kind == QA_MODE_OBJECT_SPEED) {
        if (o->next_ns >= v->value.time_ns || qa_vec_length(body.velocity) < 1 ||
            ((o->spec.flags & 1u) &&
             qa_vec_dot(qa_vec_normalize(body.velocity), o->spec.direction) < .8f))
            return true;
        o->next_ns = v->value.time_ns + MODE_SECOND / 5;
        body.velocity = qa_vec_scale(body.velocity, o->spec.value != 0 ? o->spec.value : 2);
        *accepted = true;
        return qa_world_body_write(m->options.services.world, actor, &body, e);
    }
    if (o->spec.kind != QA_MODE_OBJECT_GOAL)
        return true;
    qa_team_id team =
        o->spec.team ? o->spec.team : v->value.rules.teams[(o->spec.flags & 1u) ? 0 : 1];
    float configured = o->spec.value;
    int32_t amount = configured == 0                  ? 10
                     : configured >= (float)INT32_MAX ? INT32_MAX
                     : configured <= (float)INT32_MIN ? INT32_MIN
                                                      : (int32_t)configured;
    if (!qa_modes_team_score(m, v->id, team, amount, e))
        return false;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        uint32_t i = m->players_order.ids[ordinal].slot;
        mode_member *p = &v->members[i];
        qa_team_id t;
        if (!p->joined || !mode_player_get(m, p->actor) || !qa_modes_team(m, v->id, p->actor, &t, NULL))
            continue;
        bool scorer = qa_actor_id_equal(p->actor, v->last_ball_touch);
        int32_t score = mode_add_i32(amount, scorer ? 5 : 0);
        if (t == team) {
            if (!qa_modes_add_score(m, v->id, p->actor, score, e))
                return false;
        } else if (scorer && t &&
                   !qa_modes_add_score(m, v->id, p->actor, mode_add_i32(~score, 1), e))
            return false;
    }
    *accepted = true;
    if (!mode_ball_reset(m, v, ball, e))
        return false;
    if (o->spec.target && m->options.services.use_targets &&
        !m->options.services.use_targets(m->options.services.context, o->actor, actor,
                                         o->spec.target, 0, 0, e))
        return false;
    if (v->value.rules.capture_limit > 0)
        for (int i = 0; i < 2; ++i)
            if (v->value.team_scores[i] >= v->value.rules.capture_limit)
                return qa_modes_end(m, v->id, 0, e);
    return true;
}
bool mode_ball_frame(qa_modes *m, mode_instance *v, mode_object *o, qa_error *e) {
    if (o->value.phase != QA_OBJECTIVE_DISABLED || !o->next_ns || v->value.time_ns < o->next_ns)
        return true;
    o->next_ns = 0;
    qa_body_state body;
    if (!qa_world_body_read(m->options.services.world, o->actor, &body, e))
        return false;
    qa_string_id kind;
    if (!qa_builtin_resource(&m->options.services, "dm_dball_ball_start", &kind, e))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (v->spawns[i].classname == kind)
            ++count;
    size_t chosen = count ? (size_t)(mode_random(m) % count) : 0;
    for (size_t i = 0; i < v->spawn_count; ++i)
        if (v->spawns[i].classname == kind && !chosen--) {
            body.origin = v->spawns[i].origin;
            break;
        }
    body.angles = (qa_vec3){0};
    body.velocity = (qa_vec3){0};
    body.ground = (qa_actor_id){0};
    o->physics.angular_velocity = (qa_vec3){0};
    o->value.phase = QA_OBJECTIVE_HOME;
    o->value.visible = true;
    if (!qa_world_body_write(m->options.services.world, o->actor, &body, e) ||
        !qa_combat_set_health(m->options.services.combat, o->actor, 50000, e))
        return false;
    /* Query is retained on this stack while damage callbacks mutate the world. */
    qa_actor_id candidates[1024];
    size_t n;
    bool overflow;
    if (!qa_world_query(m->options.services.world, qa_bounds_translate(body.bounds, body.origin),
                        QA_COLLISION_SOLID, candidates, 1024, &n, &overflow, e))
        return false;
    if (overflow)
        return mode_fail(e, "DeathBall KillBox candidate capacity exceeded");
    for (size_t i = 0; i < n; ++i) {
        if (qa_actor_id_equal(candidates[i], o->actor) || !mode_live(m, candidates[i]))
            continue;
        qa_combat_state target;
        if (!qa_combat_read(m->options.services.combat, candidates[i], &target, NULL) ||
            !target.can_take_damage)
            continue;
        qa_damage_request request = {
            .target = candidates[i],
            .amount = 100000,
            .point = body.origin,
            .attack = {.attacker = o->actor,
                       .inflictor = o->actor,
                       .combat_provider = m->options.owner,
                       .weapon_provider = m->options.owner,
                       .time_ns = v->value.time_ns,
                       .cause = {.kind = QA_CAUSE_Q2,
                                 .source.q2 = {.means_of_death = 21,
                                               .flags = 32,
                                               .native = QA_Q2_CAUSE_CLASSIC}}}};
        if (!mode_damage(m, QA_GAME_Q2, &request, e))
            return false;
    }
    return mode_object_sync(m, o, e) && mode_event(m, v, QA_MODE_BALL_GOAL, (qa_actor_id){0},
                                                   (qa_actor_id){0}, o->actor, 0, 0, 6, e);
}
