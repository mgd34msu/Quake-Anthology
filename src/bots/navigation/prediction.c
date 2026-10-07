#include "internal.h"
#include <math.h>

static bool crossings(qa_bot_navigation *n, qa_error *e) {
    size_t nodes = qa_navigation_graph(n->runtime)->node_count;
    if (nodes >= SIZE_MAX / sizeof(*n->crossings))
        return bot_nav_fail(e, "bot prediction crossing workspace exceeds address range");
    size_t count = nodes + 1;
    if (count <= n->crossing_capacity) return true;
    qa_aas_crossing *p = realloc(n->crossings, count * sizeof(*p));
    if (!p) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating bot movement trace crossings");
        return false;
    }
    n->crossings = p;
    n->crossing_capacity = count;
    return true;
}
bool qa_bot_navigation_predict_movement(qa_bot_navigation *n,
                                        const qa_bot_movement_prediction_query *query,
                                        qa_bot_movement_prediction *out, qa_error *e) {
    if (!n || !query || !out)
        return bot_nav_fail(e, "missing source bot movement prediction query/output");
    float frame_time = query->frame_time <= 0 ? .1f : query->frame_time;
    double milliseconds = floor((double)frame_time * 1000 + .5);
    if (!isfinite(milliseconds) || milliseconds < 1 || milliseconds > UINT32_MAX)
        return bot_nav_fail(e, "bot movement prediction must advance representable source time");
    qa_nav_prediction_query q = {.actor = n->actor, .pass_actor = query->pass_actor,
        .origin = query->origin,
        .velocity = query->velocity, .command_move = query->command_move,
        .presence = query->presence, .on_ground = query->on_ground, .world_only = query->world_only,
        .command_frames = query->command_frames > 0 ? (uint32_t)query->command_frames : 0,
        .maximum_frames = query->maximum_frames > 0 ? (uint32_t)query->maximum_frames : 0,
        .frame_ms = (uint32_t)milliseconds, .stop_events = query->stop_events,
        .stop_area = qa_bot_navigation_node(n, query->stop_area)};
    if (!qa_navigation_predict(n->runtime, n->workspace, &q, &n->prediction, e)) return false;
    const qa_nav_prediction_result *result = &n->prediction;
    qa_bot_movement_prediction value = {.succeeded = result->stop_event != 0 ||
        result->frames >= q.maximum_frames, .end = result->end, .velocity = result->velocity,
        .presence = query->presence, .stop_event = result->stop_event,
        .frames = result->frames, .time = result->seconds,
        .end_area = qa_bot_navigation_source_area(n, result->end_area)};
    if (result->has_trace && result->stop_event && !(result->stop_event & (4 | 8 | 16))) {
        if (!crossings(n, e)) return false;
        const qa_trace_result *trace = &result->last_trace;
        size_t count;
        if (!qa_bot_navigation_trace_areas(n, result->last_query.start, trace->end,
                                           n->crossings, n->crossing_capacity, &count, e))
            return false;
        value.trace.start_solid = trace->start_solid;
        value.trace.fraction = trace->fraction;
        value.trace.end = trace->end;
        if (trace->hit == QA_TRACE_HIT_ACTOR) value.trace.actor = trace->actor;
        if (count) value.trace.last_area = n->crossings[count - 1].area;
        else if (!qa_bot_navigation_point(n, result->last_query.start, &value.trace.last_area, e))
            return false;
        if (trace->fraction != 1 && !qa_bot_navigation_point(n, trace->end, &value.trace.area, e))
            return false;
        const qa_aas_view *aas = qa_nav_asset_aas(qa_navigation_graph(n->runtime)->asset);
        if (aas) for (size_t i = 0; i < aas->count[QA_AAS_PLANES]; ++i) {
            const qa_aas_plane *p = &aas->planes[i];
            if (p->distance == trace->plane.distance &&
                p->normal.x == trace->plane.normal.x && p->normal.y == trace->plane.normal.y &&
                p->normal.z == trace->plane.normal.z) {
                value.trace.plane = (uint32_t)i;
                break;
            }
        }
    }
    if (result->has_bounds) {
        qa_bounds crouched = qa_bot_navigation_presence(n, 4);
        value.presence = result->bounds.maxs.z - result->bounds.mins.z <=
            crouched.maxs.z - crouched.mins.z ? 4 : 2;
    }
    if (result->end_area == QA_NAV_NO_INDEX &&
        !qa_bot_navigation_point(n, result->end, &value.end_area, e)) return false;
    if ((result->stop_event & (4 | 8 | 16)) &&
        !qa_bot_navigation_contents(n, result->end, &value.end_contents, e)) return false;
    if (result->stop_event) {
        value.time = fmaxf(0, result->seconds - frame_time);
        if (value.frames) --value.frames;
    }
    *out = value;
    return true;
}
