#include "internal.h"

static bool nearest(qa_navigation *n, qa_actor_id actor, qa_vec3 point, float radius,
                    bool geographic, uint32_t *out, bool *found, qa_error *e) {
    if (n == NULL || out == NULL || found == NULL || !qa_vec_finite(point) || !isfinite(radius) ||
        radius < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation nearest-area query");
        return false;
    }
    *found = false;
    *out = QA_NAV_NO_INDEX;
    float best = radius;
    for (uint32_t i = 0; i < n->graph->view.node_count; ++i) {
        const qa_nav_node *node = n->graph->nodes + i;
        float distance = nav_distance(point, node->origin);
        qa_nav_profile p;
        if (distance > best || !nav_node_profile(&n->graph->view.profile, node, &p))
            continue;
        bool allowed, clear;
        if (geographic)
            allowed = nav_static_node(n, i, NULL);
        else if (!nav_node_allowed(n, actor, i, NULL, false, &allowed, e))
            return false;
        if (!allowed)
            continue;
        if (geographic) {
            p.shape = (qa_trace_shape){.kind = QA_SHAPE_POINT};
            p.policy.q1_hull = 0;
        }
        if (!nav_clear(&n->services, &p, actor, point, node->origin, geographic, &clear, e))
            return false;
        if (clear) {
            best = distance;
            *out = node->id;
            *found = true;
        }
    }
    return true;
}
bool qa_navigation_nearest(qa_navigation *n, qa_actor_id actor, qa_vec3 point, float radius,
                           uint32_t *out, bool *found, qa_error *e) {
    return nearest(n, actor, point, radius, false, out, found, e);
}
bool qa_navigation_area(qa_navigation *n, qa_actor_id actor, qa_vec3 point, uint32_t *out,
                        bool *found, qa_error *e) {
    if (n == NULL || out == NULL || found == NULL || !qa_vec_finite(point)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation point-area query");
        return false;
    }
    const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
    if (aas == NULL)
        return nearest(n, actor, point, 512, true, out, found, e);
    if (!qa_aas_point_area(aas, point, out, e))
        return false;
    *found = nav_node_index(n->graph, *out) != QA_NAV_NO_INDEX;
    if (!*found)
        *out = QA_NAV_NO_INDEX;
    return true;
}
bool qa_navigation_bbox_areas(qa_navigation *n, qa_nav_workspace *w, qa_bounds bounds,
                              uint32_t *out, size_t capacity, size_t *count, qa_error *e) {
    if (n == NULL || w == NULL || count == NULL || (capacity != 0 && out == NULL) ||
        !nav_bounds_valid(bounds)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation bounds-area query");
        return false;
    }
    const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
    if (aas != NULL)
        return qa_aas_bbox_areas(aas, w->aas, bounds, out, capacity, count, e);
    *count = 0;
    for (size_t i = 0; i < n->graph->view.node_count && *count < capacity; ++i)
        if (qa_bounds_overlap(bounds, n->graph->nodes[i].bounds))
            out[(*count)++] = n->graph->nodes[i].id;
    return true;
}
static int crossing_compare(const void *left, const void *right) {
    const nav_crossing *a = left, *b = right;
    if (a->fraction != b->fraction)
        return a->fraction < b->fraction ? -1 : 1;
    return a->ordinal < b->ordinal ? -1 : a->ordinal > b->ordinal;
}
static bool trace_foreign(qa_navigation *n, qa_nav_workspace *w, qa_vec3 start, qa_vec3 end,
                           qa_aas_crossing *out, size_t capacity, size_t *count,
                           qa_nav_crossings *collected, qa_error *e) {
    size_t crossed = 0;
    const float from[3] = {start.x, start.y, start.z}, to[3] = {end.x, end.y, end.z};
    for (size_t i = 0; i < n->graph->view.node_count; ++i) {
        const qa_nav_node *node = n->graph->nodes + i;
        const float min[3] = {node->bounds.mins.x, node->bounds.mins.y, node->bounds.mins.z},
                    max[3] = {node->bounds.maxs.x, node->bounds.maxs.y, node->bounds.maxs.z};
        float enter = 0, leave = 1;
        for (unsigned axis = 0; axis < 3; ++axis) {
            float delta = to[axis] - from[axis];
            if (delta == 0) {
                if (from[axis] < min[axis] || from[axis] > max[axis])
                    leave = -1;
                continue;
            }
            float first = (min[axis] - from[axis]) / delta,
                  second = (max[axis] - from[axis]) / delta;
            enter = fmaxf(enter, fminf(first, second));
            leave = fminf(leave, fmaxf(first, second));
        }
        if (enter > leave)
            continue;
        if (!nav_reserve((void **)&w->crossings, &w->crossing_capacity, crossed + 1,
                         sizeof(*w->crossings), e))
            return false;
        w->crossings[crossed++] =
            (nav_crossing){.crossing = {node->id, qa_vec_lerp(start, end, enter)},
                           .fraction = enter,
                           .ordinal = (uint32_t)i};
    }
    if (crossed > 1)
        qsort(w->crossings, crossed, sizeof(*w->crossings), crossing_compare);
    size_t written = crossed < capacity ? crossed : capacity;
    if (collected != NULL) {
        if (!nav_reserve((void **)&collected->data, &collected->capacity, written,
                         sizeof(*collected->data), e))
            return false;
        out = collected->data;
    }
    for (size_t i = 0; i < written; ++i)
        out[i] = w->crossings[i].crossing;
    *count = written;
    return true;
}
bool qa_navigation_trace_areas(qa_navigation *n, qa_nav_workspace *w, qa_vec3 start, qa_vec3 end,
                               qa_aas_crossing *out, size_t capacity, size_t *count, qa_error *e) {
    if (n == NULL || w == NULL || count == NULL || (capacity != 0 && out == NULL) ||
        !qa_vec_finite(start) || !qa_vec_finite(end)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation trace-area query");
        return false;
    }
    const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
    if (aas != NULL)
        return qa_aas_trace_areas(aas, w->aas, start, end, out, capacity, count, e);
    return trace_foreign(n, w, start, end, out, capacity, count, NULL, e);
}
bool qa_navigation_trace_collect(qa_navigation *n, qa_nav_workspace *w, qa_vec3 start,
                                 qa_vec3 end, size_t maximum, qa_nav_crossings *out, qa_error *e) {
    if (out != NULL)
        out->count = 0;
    if (n == NULL || w == NULL || out == NULL || !qa_vec_finite(start) || !qa_vec_finite(end)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid retained navigation trace-area query");
        return false;
    }
    const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
    if (aas != NULL)
        return qa_aas_trace_collect(aas, w->aas, start, end, maximum, out, e);
    return trace_foreign(n, w, start, end, NULL, maximum, &out->count, out, e);
}
