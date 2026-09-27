#include "internal.h"

static bool route_time(qa_bot_route_prediction *result, const qa_aas_setting *initial,
                       qa_vec3 origin, qa_vec3 start, uint32_t travel, qa_error *e) {
    uint16_t area;
    if (!qa_navigation_aas_area_time(initial, origin, start, &area, e)) return false;
    uint64_t total = (uint64_t)(uint32_t)result->time + area + travel;
    if (total > INT32_MAX) return bot_nav_fail(e, "predicted bot route time exceeds source range");
    result->time = (int32_t)total;
    return true;
}
bool qa_bot_navigation_predict_route(qa_bot_navigation *n, const qa_bot_route_prediction_query *q,
                                     qa_bot_route_prediction *out, qa_error *e) {
    if (!n || !q || !out || !q->route.has_origin)
        return bot_nav_fail(e, "invalid bot route prediction");
    qa_bot_route_prediction result = {.end_area = q->route.goal_area,
                                      .end_position = q->route.origin};
    qa_bot_nav_route_query current = q->route;
    qa_bot_nav_area first = qa_bot_navigation_area(n, current.area);
    qa_aas_setting initial = {.presence = (int32_t)first.presence, .flags = (int32_t)first.flags};
    const qa_nav_graph_view *g = qa_navigation_graph(n->runtime);
    const qa_aas_view *aas = qa_nav_asset_aas(g->asset);
    for (size_t count = 0; current.area != current.goal_area && count < g->node_count &&
         (!q->maximum_areas || (q->maximum_areas > 0 && count < (uint32_t)q->maximum_areas)); ++count) {
        qa_bot_nav_route route;
        if (!qa_bot_navigation_route(n, &current, &route, e)) return false;
        const qa_nav_edge *edge = route.found ? qa_bot_navigation_reachability(n, route.next_reachability) : NULL;
        if (!edge) {
            result.stop_event = QA_BOT_ROUTE_NO_ROUTE;
            *out = result;
            return true;
        }
        uint32_t flags = qa_nav_edge_travel_flag(edge);
        uint32_t destination = qa_bot_navigation_source_area(n, edge->to);
        const qa_aas_reach *source = aas && edge->source.kind == QA_NAV_ORIGIN_AAS ?
            &aas->reachability[edge->id] : NULL;
        float raw_time = edge->travel_seconds * 100;
        if (!source && (!isfinite(raw_time) || raw_time >= (float)INT32_MAX))
            return bot_nav_fail(e, "predicted edge time exceeds source range");
        uint32_t travel = source ? source->travel_time : raw_time < 1 ? 1 : (uint32_t)raw_time;
        if (q->stop_events & QA_BOT_ROUTE_TRAVEL) {
            if (flags & q->stop_travel_flags) {
                result.end_area = current.area;
                result.end_contents = qa_bot_navigation_area(n, current.area).contents;
                result.end_travel_flags = flags;
                result.end_position = edge->start;
                result.succeeded = true;
                result.stop_event = QA_BOT_ROUTE_TRAVEL;
                *out = result;
                return true;
            }
            qa_bot_nav_area dest = qa_bot_navigation_area(n, destination);
            qa_aas_setting setting = {.contents = (int32_t)dest.contents, .flags = (int32_t)dest.flags};
            uint32_t contents_flags = qa_nav_area_travel_flags(&setting);
            if (contents_flags & q->stop_travel_flags) {
                result.end_area = destination;
                result.end_contents = dest.contents;
                result.end_travel_flags = contents_flags;
                result.end_position = edge->end;
                if (!route_time(&result, &initial, q->route.origin, edge->start, travel, e)) return false;
                result.succeeded = true;
                result.stop_event = QA_BOT_ROUTE_TRAVEL;
                *out = result;
                return true;
            }
        }
        qa_aas_crossing crossed[32]; size_t crossed_count = 0;
        qa_vec3 from = edge->start, to = edge->end;
        bool trace = !source;
        if (source) {
            switch ((uint32_t)source->travel_type & 0xffffff) {
            case 4: case 9: to = from; to.z = edge->end.z; trace = true; break;
            case 7: from = to; from.z = edge->start.z; trace = true; break;
            case 14: trace = true; break;
            default: break;
            }
        }
        if (trace && !qa_bot_navigation_trace_areas(n, from, to, crossed, 32, &crossed_count, e))
            return false;
        for (size_t i = 0; i <= crossed_count; ++i) {
            uint32_t area = i == crossed_count ? destination : crossed[i].area;
            uint32_t contents = qa_bot_navigation_area(n, area).contents;
            if ((q->stop_events & QA_BOT_ROUTE_CONTENTS) && (contents & q->stop_contents)) {
                result.end_area = area;
                result.end_contents = contents;
                result.end_position = edge->end;
                if (!route_time(&result, &initial, q->route.origin, edge->start, travel, e)) return false;
                result.succeeded = true;
                result.stop_event = QA_BOT_ROUTE_CONTENTS;
                *out = result;
                return true;
            }
            if ((q->stop_events & QA_BOT_ROUTE_AREA) && area == q->stop_area) {
                result.end_area = area;
                result.end_contents = contents;
                result.end_position = edge->start;
                result.succeeded = true;
                result.stop_event = QA_BOT_ROUTE_AREA;
                *out = result;
                return true;
            }
        }
        if (!route_time(&result, &initial, q->route.origin, edge->start, travel, e)) return false;
        result.end_area = destination;
        result.end_contents = qa_bot_navigation_area(n, destination).contents;
        result.end_position = edge->end;
        result.end_travel_flags = flags;
        current.area = destination;
        current.origin = edge->end;
        if (q->maximum_time && result.time > q->maximum_time) break;
    }
    result.succeeded = current.area == current.goal_area;
    *out = result;
    return true;
}

static size_t node_ordinal(qa_bot_navigation *n, uint32_t id) {
    const qa_nav_node *node = qa_navigation_node(n->runtime, id);
    return node ? (size_t)(node - qa_navigation_graph(n->runtime)->nodes) : SIZE_MAX;
}
static size_t neighbor_at(qa_bot_navigation *n, size_t ordinal, size_t depth, bool *present) {
    const qa_nav_graph_view *g = qa_navigation_graph(n->runtime);
    const qa_nav_node *node = &g->nodes[ordinal];
    const qa_aas_view *aas = qa_nav_asset_aas(g->asset);
    uint32_t other;
    if (aas) {
        uint32_t index = n->next_neighbor[depth]++;
        const qa_aas_area *area = &aas->areas[node->id];
        if (index >= (uint32_t)area->face_count) { *present = false; return SIZE_MAX; }
        int32_t face_id = aas->face_index[(uint32_t)area->first_face + index];
        const qa_aas_face *face = &aas->faces[face_id < 0 ? -face_id : face_id];
        other = (uint32_t)(face->front_area == (int32_t)node->id ? face->back_area : face->front_area);
        *present = true;
        return other ? node_ordinal(n, other) : SIZE_MAX;
    }
    const qa_nav_edge *edge = qa_navigation_adjacent_next(n->runtime, node->id, &n->adjacency[depth]);
    *present = edge != NULL;
    if (!edge) return SIZE_MAX;
    other = edge->from == node->id ? edge->to : edge->from;
    return node_ordinal(n, other);
}
bool qa_bot_navigation_alternatives(qa_bot_navigation *n, const qa_bot_nav_route_query *query,
                                    uint32_t types, int32_t maximum,
                                    qa_bot_alternative_goal *out, size_t capacity,
                                    size_t *count, qa_error *e) {
    if (!n || !query || !count || (capacity && !out) || !query->has_origin)
        return bot_nav_fail(e, "invalid alternative bot route query");
    *count = 0;
    if (!query->area || !query->goal_area) return true;
    qa_bot_nav_route direct;
    if (!qa_bot_navigation_route(n, query, &direct, e)) return false;
    const qa_nav_graph_view *g = qa_navigation_graph(n->runtime);
    if (!g->node_count) return true;
    memset(n->start_times, 0, g->node_count * sizeof(*n->start_times));
    memset(n->visited, 0, g->node_count);
    for (size_t i = 0; i < g->node_count; ++i) {
        uint32_t area = qa_bot_navigation_source_area(n, g->nodes[i].id);
        qa_bot_nav_area info = qa_bot_navigation_area(n, area);
        if (!(types & QA_BOT_ALTERNATIVE_ALL) &&
            !((types & QA_BOT_ALTERNATIVE_CLUSTER) && (info.contents & 8)) &&
            !((types & QA_BOT_ALTERNATIVE_VIEW) && (info.contents & 512))) continue;
        if (!info.reach_count) continue;
        qa_bot_nav_route_query q = *query;
        q.goal_area = area;
        qa_bot_nav_route start, goal;
        if (!qa_bot_navigation_route(n, &q, &start, e)) return false;
        if (!start.found || !start.travel_time ||
            (float)start.travel_time > 1.1f * (float)direct.travel_time) continue;
        q.area = area;
        q.goal_area = query->goal_area;
        q.has_origin = false;
        if (!qa_bot_navigation_route(n, &q, &goal, e)) return false;
        if (!goal.found || !goal.travel_time ||
            (float)goal.travel_time > .8f * (float)direct.travel_time) continue;
        n->start_times[i] = start.travel_time;
        n->goal_times[i] = goal.travel_time;
    }
    for (size_t first = 0; first < g->node_count; ++first) {
        if (!n->start_times[first] || n->visited[first]) continue;
        size_t depth = 1, cluster_count = 1;
        n->stack[0] = (uint32_t)first;
        n->next_neighbor[0] = 0;
        n->adjacency[0] = (qa_nav_adjacency_cursor){0};
        n->areas[0] = (uint32_t)first;
        n->visited[first] = 1;
        while (depth) {
            bool present;
            size_t other = neighbor_at(n, n->stack[depth - 1], depth - 1, &present);
            if (!present) { --depth; continue; }
            if (other == SIZE_MAX || !n->start_times[other] || n->visited[other]) continue;
            n->visited[other] = 1;
            n->areas[cluster_count++] = (uint32_t)other;
            n->stack[depth] = (uint32_t)other;
            n->adjacency[depth] = (qa_nav_adjacency_cursor){0};
            n->next_neighbor[depth++] = 0;
        }
        qa_vec3 center = {0};
        for (size_t i = 0; i < cluster_count; ++i)
            center = qa_vec_add(center, g->nodes[n->areas[i]].origin);
        center = qa_vec_scale(center, 1.0f / (float)cluster_count);
        size_t best = first;
        float distance = 999999;
        for (size_t i = 0; i < cluster_count; ++i) {
            size_t candidate = n->areas[i];
            float length = qa_vec_length(qa_vec_sub(g->nodes[candidate].origin, center));
            if (length < distance) { distance = length; best = candidate; }
        }
        if (*count >= capacity) return bot_nav_fail(e, "alternative bot goal output is too small");
        out[(*count)++] = (qa_bot_alternative_goal){.origin = g->nodes[best].origin,
            .area = qa_bot_navigation_source_area(n, g->nodes[best].id),
            .start_time = n->start_times[best], .goal_time = n->goal_times[best],
            .extra_time = (uint16_t)(n->start_times[best] + n->goal_times[best] - direct.travel_time)};
        if (maximum <= 0 || *count >= (uint32_t)maximum) break;
    }
    return true;
}
