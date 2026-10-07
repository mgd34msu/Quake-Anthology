#include "internal.h"

static bool view_fields(const qa_bot_move_goal_source *goal,
                         const qa_bot_vector_target *out, bool *found, qa_error *e) {
    if (!goal || (!goal->value && (!goal->area || (!goal->origin.value && !goal->origin.read))) ||
        !out || (!out->value && !out->write) || !found)
        return bot_move_fail(e, "missing bot view goal/output fields");
    return true;
}
static bool view_target(qa_bot_moves *, uint32_t, const qa_bot_move_goal_source *, uint32_t,
                         float, const qa_bot_vector_target *, bool base, bool *, qa_error *);
static bool qa_bot_moves_view_target_operation(qa_bot_moves *m, uint32_t handle, const qa_bot_goal *goal,
                              uint32_t flags, float ahead, qa_vec3 *out, bool *found, qa_error *e) {
    if (!goal || !out || !qa_vec_finite(goal->origin) || !isfinite(ahead))
        return bot_move_fail(e, "invalid bot view target query");
    qa_bot_move_goal_source source = {.value = goal};
    qa_bot_vector_target target = {.value = out};
    return view_target(m, handle, &source, flags, ahead, &target, false, found, e);
}
static bool qa_bot_moves_view_target_from_operation(qa_bot_moves *m, uint32_t handle,
                                   const qa_bot_move_goal_source *goal, uint32_t flags,
                                   float ahead, const qa_bot_vector_target *out, bool *found,
                                   qa_error *e) {
    return view_target(m, handle, goal, flags, ahead, out, true, found, e);
}
static bool view_target(qa_bot_moves *m, uint32_t handle, const qa_bot_move_goal_source *goal,
                         uint32_t flags, float ahead, const qa_bot_vector_target *out,
                         bool base, bool *found, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if(!found) return bot_move_fail(e,"missing bot view result");
    bot_move_record *state = bot_move_source_state(m, handle);
    if (!state) {*found=false;return true;}
    if (!view_fields(goal, out, found, e)) return false;
    *found = false;
    m->busy = true;
    bot_travel t;
    uint32_t area, goal_area;
    qa_bot_vector_source origin = bot_move_origin_source(state);
    bool ok = (base ? bot_travel_begin_client(m,-1,&t,e) : bot_travel_begin(m,state,&t,e)) &&
              qa_bot_navigation_point(t.navigation, bot_move_vector(state,BM_ORIGIN), &area, e) &&
              bot_goal_area(goal, &goal_area, e) &&
              bot_travel_points(&t, &origin, area, goal_area, flags, found, e);
    if (ok && *found) {
        qa_vec3 start = bot_move_vector(state,BM_ORIGIN);
        qa_bot_vector_source end = bot_goal_origin(goal);
        for (size_t i = 0; i <= m->point_count; ++i) {
            qa_bot_vector_source next = i == m->point_count ? end :
                i == 0 ? origin : (qa_bot_vector_source){.value = &m->points[i]};
            qa_vec3 point;
            if (!(ok = qa_bot_vector_read(&next, &point, e))) break;
            qa_vec3 delta = qa_vec_sub(point, start);
            float distance = qa_vec_length(delta);
            if (distance >= ahead) {
                qa_vec3 value = bot_ma(start, ahead, qa_vec_normalize(delta));
                qa_bot_vector_source result = {.value = &value};
                ok = qa_bot_vector_write(out, &result, e);
                goto done;
            }
            ahead -= distance;
            start = point;
        }
        if (ok) ok = qa_bot_vector_write(out, &end, e);
    }
done:
    m->busy = false;
    return ok;
}
bool qa_bot_moves_visible_position(qa_bot_moves *m, qa_vec3 origin, uint32_t area,
                                   const qa_bot_goal *goal, uint32_t flags, qa_vec3 *out,
                                   bool *found, qa_error *e) {
    if (!goal || !out || !qa_vec_finite(origin) || !qa_vec_finite(goal->origin))
        return bot_move_fail(e, "invalid bot visible position query");
    qa_bot_vector_source start = {.value = &origin};
    qa_bot_move_goal_source source = {.value = goal};
    qa_bot_vector_target target = {.value = out};
    return qa_bot_moves_visible_position_from(m, &start, area, &source, flags,
                                               &target, found, e);
}
bool qa_bot_moves_visible_position_from(qa_bot_moves *m,
                                        const qa_bot_vector_source *origin, uint32_t area,
                                        const qa_bot_move_goal_source *goal, uint32_t flags,
                                        const qa_bot_vector_target *out, bool *found, qa_error *e) {
    if (!bot_move_mutable(m, e) || !view_fields(goal, out, found, e)) return false;
    if (!origin || (!origin->value && !origin->read))
        return bot_move_fail(e, "missing bot visible-position origin fields");
    *found = false;
    m->busy = true;
    bot_travel t;
    uint32_t goal_area;
    bool route;
    bool ok = bot_goal_area(goal, &goal_area, e) && bot_travel_begin_client(m,-1,&t,e) &&
              bot_travel_points(&t, origin, area, goal_area, flags, &route, e);
    if (ok && route) {
        qa_bot_vector_source end = bot_goal_origin(goal);
        for (size_t i = 0; i < m->point_count; ++i) {
            qa_bot_vector_source point = i == 0 ? *origin :
                (qa_bot_vector_source){.value = &m->points[i]};
            qa_vec3 start, target;
            qa_trace_result trace;
            if (!qa_bot_vector_read(&point, &start, e) || !qa_bot_vector_read(&end, &target, e) ||
                !bot_trace(&t, start, target, NULL, -1, 1, &trace, e)) {
                ok = false;
                break;
            }
            if (trace.fraction == 1 && !trace.start_solid && !trace.all_solid) {
                ok = qa_bot_vector_write(out, &point, e);
                *found = ok;
                break;
            }
        }
    }
    m->busy = false;
    return ok;
}

bool qa_bot_moves_view_target(qa_bot_moves *m, uint32_t handle, const qa_bot_goal *goal, uint32_t flags, float ahead, qa_vec3 *out, bool *found, qa_error *e) {
    BOT_MOVE_OPERATION(m,e,qa_bot_moves_view_target_operation(m,handle,goal,flags,ahead,out,found,e));
}

bool qa_bot_moves_view_target_from(qa_bot_moves *m, uint32_t handle, const qa_bot_move_goal_source *goal, uint32_t flags, float ahead, const qa_bot_vector_target *out, bool *found, qa_error *e) {
    BOT_MOVE_OPERATION(m,e,qa_bot_moves_view_target_from_operation(m,handle,goal,flags,ahead,out,found,e));
}
