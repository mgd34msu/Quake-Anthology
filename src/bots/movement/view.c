#include "internal.h"

bool qa_bot_moves_view_target(qa_bot_moves *m, uint32_t handle, const qa_bot_goal *goal,
                              uint32_t flags, float ahead, qa_vec3 *out, bool *found, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    qa_bot_move_state *state = bot_move_state(m, handle, e);
    if (!state)
        return false;
    if (!goal || !out || !found || !qa_vec_finite(goal->origin) || !isfinite(ahead))
        return bot_move_fail(e, "invalid bot view target query");
    *found = false;
    m->busy = true;
    bot_travel t;
    uint32_t area;
    bool ok = bot_travel_begin(m, state, &t, e) &&
              qa_bot_navigation_point(t.navigation, state->input.origin, &area, e) &&
              bot_travel_points(&t, state->input.origin, area, goal, flags, found, e);
    if (ok && *found) {
        qa_vec3 start = state->input.origin;
        *out = goal->origin;
        for (size_t i = 0; i <= m->point_count; ++i) {
            qa_vec3 point = i == m->point_count ? goal->origin : m->points[i];
            qa_vec3 delta = qa_vec_sub(point, start);
            float distance = qa_vec_length(delta);
            if (distance >= ahead) {
                *out = bot_ma(start, ahead, qa_vec_normalize(delta));
                break;
            }
            ahead -= distance;
            start = point;
        }
    }
    m->busy = false;
    return ok;
}
bool qa_bot_moves_visible_position(qa_bot_moves *m, int32_t client, qa_vec3 origin, uint32_t area,
                                   const qa_bot_goal *goal, uint32_t flags, qa_vec3 *out,
                                   bool *found, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!goal || !out || !found || !qa_vec_finite(origin) || !qa_vec_finite(goal->origin))
        return bot_move_fail(e, "invalid bot visible position query");
    *found = false;
    m->busy = true;
    qa_bot_move_state state = {.input = {.client = client, .origin = origin}};
    bot_travel t;
    bool route;
    bool ok = bot_travel_begin(m, &state, &t, e) &&
              bot_travel_points(&t, origin, area, goal, flags, &route, e);
    if (ok && route)
        for (size_t i = 0; i < m->point_count; ++i) {
            qa_trace_result trace;
            if (!bot_trace(&t, m->points[i], goal->origin, NULL, -1, 1, &trace, e)) {
                ok = false;
                break;
            }
            if (trace.fraction == 1 && !trace.start_solid && !trace.all_solid) {
                *out = m->points[i];
                *found = true;
                break;
            }
        }
    m->busy = false;
    return ok;
}
