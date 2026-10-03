#include "internal.h"
#include "qa/bot_navigation_source.h"

static bool endpoint(const qa_bot_vector_source *origin, float offset, qa_vec3 *out,
                      qa_error *e) {
    if (!qa_bot_vector_read(origin, out, e) ||
        !qa_bot_vector_component(origin, 2, &out->z, e)) return false;
    out->z += offset;
    return true;
}
static bool trace_from(qa_bot_navigation *n, const qa_bot_vector_source *origin, qa_vec3 end,
                        const qa_bounds *bounds, qa_actor_id pass, qa_trace_result *out,
                        qa_error *e) {
    qa_vec3 start;
    return qa_bot_vector_read(origin, &start, e) &&
        qa_bot_navigation_trace(n, start, end, bounds, pass, 0x10001, out, e);
}
static bool crossed_from(qa_bot_navigation *n, const qa_bot_vector_source *origin, qa_vec3 end,
                          qa_aas_crossing out[10], size_t *count, qa_error *e) {
    qa_vec3 start;
    return qa_bot_vector_read(origin, &start, e) &&
        qa_bot_navigation_trace_areas(n, start, end, out, 10, count, e);
}
bool qa_bot_navigation_fuzzy(qa_bot_navigation *n, qa_vec3 origin, uint32_t *out, qa_error *e) {
    qa_bot_vector_source source = {.value = &origin};
    return qa_bot_navigation_fuzzy_from(n, &source, out, e);
}
bool qa_bot_navigation_fuzzy_from(qa_bot_navigation *n, const qa_bot_vector_source *origin,
                                  uint32_t *out, qa_error *e) {
    if (!n || !origin || (!origin->value && !origin->read) || !out)
        return bot_nav_fail(e, "invalid bot fuzzy-area request");
    const qa_nav_graph_view *graph = qa_navigation_graph(n->runtime);
    if (graph->node_count && graph->nodes[0].source.kind == QA_NAV_ORIGIN_CONSTRUCTED) {
        qa_vec3 point;
        uint32_t node;
        bool found;
        if (!qa_bot_vector_read(origin, &point, e) ||
            !qa_navigation_nearest(n->runtime, n->actor, point, 512, &node, &found, e))
            return false;
        *out = found ? qa_bot_navigation_source_area(n, node) : 0;
        return true;
    }
    uint32_t first;
    qa_vec3 point;
    if (!qa_bot_vector_read(origin, &point, e) ||
        !qa_bot_navigation_point(n, point, &first, e)) return false;
    if (first && qa_bot_navigation_area(n, first).reach_count) { *out = first; return true; }
    qa_aas_crossing crossed[10];
    size_t count;
    qa_vec3 end;
    if (!endpoint(origin, 4, &end, e) ||
        !crossed_from(n, origin, end, crossed, &count, e)) return false;
    for (size_t i = 0; i < count; ++i)
        if (qa_bot_navigation_area(n, crossed[i].area).reach_count) {
            *out = crossed[i].area;
            return true;
        }
    uint32_t best = 0;
    float distance = 999999;
    for (int z = 1; z >= -1; --z) {
        for (int x = 1; x >= -1; --x)
            for (int y = 1; y >= -1; --y) {
                if (!qa_bot_vector_read(origin, &end, e)) return false;
                end = qa_vec_add(end, qa_v3((float)x * 8, (float)y * 8, (float)z * 12));
                if (!crossed_from(n, origin, end, crossed, &count, e)) return false;
                for (size_t i = 0; i < count; ++i) {
                    if (qa_bot_navigation_area(n, crossed[i].area).reach_count) {
                        if (!qa_bot_vector_read(origin, &point, e)) return false;
                        float candidate = qa_vec_length(qa_vec_sub(crossed[i].point, point));
                        if (candidate < distance) { distance = candidate; best = crossed[i].area; }
                    }
                    if (!first) first = crossed[i].area;
                }
            }
        if (best) { *out = best; return true; }
    }
    *out = first;
    return true;
}
bool qa_bot_navigation_reachable(qa_bot_navigation *n, qa_vec3 origin, uint32_t *out, qa_error *e) {
    qa_bot_vector_source source = {.value = &origin};
    return qa_bot_navigation_reachable_from(n, &source, n ? n->actor : (qa_actor_id){0}, out, e);
}
bool qa_bot_navigation_reachable_from(qa_bot_navigation *n, const qa_bot_vector_source *origin,
                                      qa_actor_id pass, uint32_t *out, qa_error *e) {
    if (!n || !origin || (!origin->value && !origin->read) || !out)
        return bot_nav_fail(e, "invalid bot reachability query");
    qa_bounds bounds = qa_bot_navigation_presence(n, 4);
    qa_vec3 end;
    qa_trace_result trace;
    if (!endpoint(origin, -3, &end, e) ||
        !trace_from(n, origin, end, &bounds, pass, &trace, e)) return false;
    if (!trace.start_solid && !trace.all_solid && trace.fraction < 1 &&
        trace.hit != QA_TRACE_HIT_NONE) {
        if (trace.hit == QA_TRACE_HIT_WORLD)
            return qa_bot_navigation_fuzzy_from(n, origin, out, e);
        qa_actor_collision collision;
        int32_t model;
        if (n->observations.model)
            model = n->observations.model(n->observations.context, trace.actor);
        else {
            qa_error collision_error = {0};
            bool has_collision = qa_world_get_collision(n->world, trace.actor, &collision,
                                                         &collision_error);
            if (!has_collision && collision_error.code) {
                if (e) *e = collision_error;
                return false;
            }
            model = has_collision && collision.inline_model ? (int32_t)collision.model : 0;
        }
        const qa_nav_graph_view *graph = qa_navigation_graph(n->runtime);
        for (size_t i = 0; i < graph->edge_count; ++i) {
            const qa_nav_edge *edge = &graph->edges[i];
            if (edge->mode == QA_NAV_MOVER && edge->has_entity && edge->entity.has_model &&
                edge->entity.model == model) {
                *out = qa_bot_navigation_source_area(n, edge->to);
                return true;
            }
        }
        bool swimming;
        qa_vec3 point;
        if (!qa_bot_vector_read(origin, &point, e) ||
            !qa_bot_navigation_swimming(n, point, &swimming, e)) return false;
        if (swimming) return qa_bot_navigation_fuzzy_from(n, origin, out, e);
        if (!qa_bot_navigation_fuzzy_from(n, origin, out, e)) return false;
        if (*out && qa_bot_navigation_area(n, *out).reach_count) return true;
        if (!endpoint(origin, -800, &end, e) ||
            !trace_from(n, origin, end, &bounds, (qa_actor_id){0}, &trace, e)) return false;
        qa_bot_vector_source floor = {.value = &trace.end};
        return qa_bot_navigation_fuzzy_from(n, trace.start_solid || trace.all_solid ? origin :
                                             &floor, out, e);
    }
    return qa_bot_navigation_fuzzy_from(n, origin, out, e);
}
