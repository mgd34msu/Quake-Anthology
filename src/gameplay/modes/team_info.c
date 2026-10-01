#include "internal.h"

bool qa_modes_location(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_mode_location *out,
                       qa_error *e) {
    mode_instance *v = mode_get(m, id);
    qa_body_state body;
    if (!v || !out || !qa_world_body_read(m->options.services.world, actor, &body, e))
        return false;
    *out = (qa_mode_location){0};
    float best = 3 * 8192.0f * 8192.0f;
    for (size_t i = m->observations.count; i > 0; --i) {
        mode_object *o = mode_object_get(m, m->observations.ids[i - 1]);
        if (!o || o->mode.slot != id.slot || o->mode.generation != id.generation ||
            o->spec.kind != QA_MODE_OBJECT_LOCATION)
            continue;
        qa_vec3 delta = qa_vec_sub(body.origin, o->home);
        float distance = qa_vec_dot(delta, delta);
        if (distance > best || !mode_visible(m, actor, o->actor, true))
            continue;
        best = distance;
        int32_t color = o->spec.value < 0 ? 0 : o->spec.value > 7 ? 7 : (int32_t)o->spec.value;
        *out = (qa_mode_location){
            .actor = o->actor, .message = o->spec.message, .id = o->spec.location, .color = color};
    }
    return true;
}
bool qa_modes_team_info(qa_modes *m, qa_mode_id id, qa_actor_id recipient, qa_mode_team_row *rows,
                        size_t capacity, size_t *count, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    qa_team_id team;
    if (!v || !count || (capacity && !rows) || !qa_modes_team(m, v->id, recipient, &team, e))
        return mode_fail(e, "invalid team information query");
    *count = 0;
    for (size_t ordinal = 0; ordinal < m->players_order.count && *count < 32; ++ordinal) {
        qa_actor_id actor = m->players_order.ids[ordinal];
        qa_team_id current;
        mode_member *member = mode_member_get(m, v, actor);
        if (!member || !qa_modes_team(m, v->id, actor, &current, NULL) || current != team)
            continue;
        if (*count >= capacity)
            return mode_fail(e, "team information output too small");
        qa_combat_state state;
        if (!qa_combat_read(m->options.services.combat, actor, &state, e))
            return false;
        qa_mode_team_row row = {.actor = actor,
                                .location = member->location,
                                .health = fmaxf(0, state.health),
                                .armor = fmaxf(0, state.armor.regular.points)};
        if (m->options.hooks.team_equipment &&
            !m->options.hooks.team_equipment(m->options.hooks.context, actor, &row.weapon,
                                             &row.powerups, e))
            return false;
        rows[(*count)++] = row;
    }
    return true;
}
bool mode_team_info_frame(qa_modes *m, mode_instance *v, qa_error *e) {
    if (m->options.hooks.q3_team_status_bound &&
        MODE_CALLBACK(m, m->options.hooks.q3_team_status_bound(
            m->options.hooks.context, v->id)))
        return true;
    if (v->value.rules.source < QA_MODE_Q3 || v->value.rules.kind < QA_MODE_TEAM_DEATHMATCH ||
        v->value.time_ns - v->team_location_ns <= MODE_SECOND)
        return true;
    v->team_location_ns = v->value.time_ns;
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.ids[ordinal];
        mode_member *member = mode_member_get(m, v, actor);
        mode_player *player = mode_player_get(m, actor);
        qa_team_id team;
        if (!member || !player || !player->value.connected || player->value.connecting ||
            !qa_modes_team(m, v->id, actor, &team, NULL) ||
            (team != v->value.rules.teams[0] && team != v->value.rules.teams[1]))
            continue;
        qa_mode_location location;
        if (!qa_modes_location(m, v->id, actor, &location, e))
            return false;
        member->location = location.id;
    }
    for (size_t ordinal = 0; ordinal < m->players_order.count; ++ordinal) {
        qa_actor_id actor = m->players_order.ids[ordinal];
        mode_player *player = mode_player_get(m, actor);
        qa_team_id team;
        if (!mode_member_get(m, v, actor) || !player || !player->value.connected ||
            player->value.connecting || !qa_modes_team(m, v->id, actor, &team, NULL) ||
            (team != v->value.rules.teams[0] && team != v->value.rules.teams[1]))
            continue;
        if (!mode_event(m, v, QA_MODE_TEAM_INFO, actor, (qa_actor_id){0}, (qa_actor_id){0}, team, 0,
                        0, e))
            return false;
    }
    return true;
}
