#include "internal.h"

static bool candidate(qa_navigation *n, qa_nav_workspace *w, const qa_nav_route_query *q,
                      uint32_t start, uint32_t goal, size_t *count, bool *found, qa_error *e) {
    const qa_nav_graph *g = n->graph;
    for (size_t i = 0; i < g->view.node_count; ++i) {
        w->costs[i] = INFINITY;
        w->parents[i] = QA_NAV_NO_INDEX;
    }
    w->queue_count = 0;
    w->costs[start] = 0;
    *found = false;
    *count = 0;
    if (!nav_queue_push(w, (nav_queue_entry){start, 0}, e))
        return false;
    nav_queue_entry current;
    while (nav_queue_pop(w, &current)) {
        if (current.cost != w->costs[current.node])
            continue;
        if (current.node == goal) {
            uint32_t node = goal;
            while (node != start) {
                uint32_t edge = w->parents[node];
                if (edge == QA_NAV_NO_INDEX || *count >= g->view.node_count) {
                    qa_error_set(e, QA_ERROR_FORMAT, node,
                                 "Navigation predecessor chain is incomplete or cyclic");
                    return false;
                }
                w->path[(*count)++] = edge;
                node = nav_node_index(g, g->edges[edge].from);
            }
            for (size_t i = 0; i < *count / 2; ++i) {
                uint32_t swap = w->path[i];
                w->path[i] = w->path[*count - 1 - i];
                w->path[*count - 1 - i] = swap;
            }
            *found = true;
            return true;
        }
        for (uint32_t i = g->first_out[current.node]; i < g->first_out[current.node + 1]; ++i) {
            uint32_t index = g->outgoing[i];
            if (w->rejected[index])
                continue;
            bool allowed;
            if (!nav_edge_allowed(n, w, q->actor, index, q, &allowed, e))
                return false;
            if (!allowed)
                continue;
            const qa_nav_edge *edge = g->edges + index;
            uint32_t to = nav_node_index(g, edge->to);
            float seconds = isnan(n->admission_seconds[index]) ? edge->travel_seconds
                                                               : n->admission_seconds[index],
                  cost = current.cost + seconds;
            if (cost >= w->costs[to])
                continue;
            w->costs[to] = cost;
            w->parents[to] = index;
            if (!nav_queue_push(w, (nav_queue_entry){to, cost}, e))
                return false;
        }
    }
    return true;
}
static qa_vec3 cursor(const qa_nav_route *route) { return route->points[route->point_count - 1]; }
static bool traverse(qa_navigation *n, nav_prediction *prediction, qa_actor_id actor,
                     uint32_t index, qa_nav_route *route, bool *admitted, qa_error *e) {
    const qa_nav_edge *edge = n->graph->edges + index;
    const qa_nav_profile *p = &n->graph->view.profile;
    qa_nav_entity_state source;
    bool has_source;
    *admitted = false;
    if (!nav_entity(n, edge, &source, &has_source, e))
        return false;
    if (edge->mode == QA_NAV_MOVER && has_source && source.kind == QA_NAV_ENTITY_TRAIN) {
        qa_nav_train_ride ride;
        if (!source.data.train.running || !qa_navigation_train_ride(&source, edge, p, &ride))
            return true;
        qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
        if (nav_distance(cursor(route), staging) > 1) {
            bool approach;
            if (!nav_predict(prediction, actor, cursor(route), staging, QA_NAV_WALK, route,
                             &approach, e))
                return false;
            if (!approach)
                return true;
        }
        if (!nav_route_point(route, edge->start, e) || !nav_route_point(route, edge->end, e))
            return false;
        route->travel_seconds += edge->travel_seconds;
        nav_prediction_close(prediction);
        *admitted = true;
        return true;
    }
    if (edge->mode == QA_NAV_MOVER &&
        (edge->source.kind == QA_NAV_ORIGIN_NAV2 || edge->source.kind == QA_NAV_ORIGIN_NAV3) &&
        edge->source_travel_type == 6 && has_source && source.kind == QA_NAV_ENTITY_ELEVATOR &&
        source.enabled && !source.locked) {
        qa_vec3 top = source.data.elevator.top, bottom = source.data.elevator.bottom;
        if (top.z <= bottom.z || top.x != bottom.x || top.y != bottom.y)
            return true;
        qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
        const qa_nav_node *node = qa_navigation_node(n, edge->from);
        if (nav_distance(cursor(route), edge->start) > fmaxf(p->maximum_step, node->radius) &&
            nav_distance(cursor(route), staging) > 1) {
            bool approach;
            if (!nav_predict(prediction, actor, cursor(route), staging, QA_NAV_WALK, route,
                             &approach, e))
                return false;
            if (!approach)
                return true;
        }
        if (!nav_route_point(route, edge->end, e))
            return false;
        route->travel_seconds += edge->travel_seconds;
        nav_prediction_close(prediction);
        *admitted = true;
        return true;
    }
    if (nav_distance(cursor(route), edge->start) > 1) {
        bool approach;
        if (!nav_predict(prediction, actor, cursor(route), edge->start,
                         edge->mode == QA_NAV_CROUCH ? QA_NAV_CROUCH : QA_NAV_WALK, route,
                         &approach, e))
            return false;
        if (!approach)
            return true;
    }
    qa_vec3 landing = edge->end;
    if (edge->mode == QA_NAV_WALK) {
        const qa_nav_edge *boarding;
        qa_nav_entity_state state;
        qa_nav_train_ride ride;
        bool found;
        if (!nav_boarding(n, edge->to, true, &boarding, &state, &ride, &found, e))
            return false;
        if (found && nav_distance(state.data.train.origin, ride.boarding.origin) > p->maximum_step)
            landing = boarding->has_hint ? boarding->hint.funnel : edge->start;
        else if (edge->source.kind == QA_NAV_ORIGIN_NAV2 ||
                 edge->source.kind == QA_NAV_ORIGIN_NAV3) {
            if (!nav_boarding(n, edge->to, false, &boarding, &state, &ride, &found, e))
                return false;
            if (found && state.data.elevator.phase != QA_NAV_MOVER_BOTTOM)
                landing = edge->start;
            else if (found) {
                qa_vec3 end = edge->end;
                end.z -= 96;
                qa_trace_result floor;
                if (!nav_trace(&n->services, p, actor, edge->end, end, false, &floor, e))
                    return false;
                if (!floor.start_solid && !floor.all_solid && floor.hit == QA_TRACE_HIT_ACTOR &&
                    qa_actor_id_equal(floor.actor, state.actor) && floor.contact &&
                    floor.contact_plane.normal.z >= p->minimum_floor_normal)
                    landing = floor.end;
            }
        }
    }
    float seconds = route->travel_seconds;
    if (!nav_predict(prediction, actor, cursor(route), landing, edge->mode, route, admitted, e))
        return false;
    if (*admitted)
        n->admission_seconds[index] = route->travel_seconds - seconds;
    return true;
}
bool qa_navigation_route(qa_navigation *n, qa_nav_workspace *w, const qa_nav_route_query *q,
                         qa_nav_route *route, qa_error *e) {
    if (n == NULL || w == NULL || q == NULL || route == NULL || !qa_vec_finite(q->start) ||
        !qa_vec_finite(q->goal) || (q->disabled_count != 0 && q->disabled_areas == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation route query");
        return false;
    }
    route->found = false;
    route->node_count = 0;
    route->edge_count = 0;
    route->point_count = 0;
    route->travel_seconds = 0;
    qa_nav_graph_release(route->graph);
    route->graph = NULL;
    nav_refresh(n);
    uint32_t start = q->start_node, goal = q->goal_node;
    bool found;
    if (start == QA_NAV_NO_INDEX) {
        if (!qa_navigation_area(n, q->actor, q->start, &start, &found, e))
            return false;
        if (!found)
            return true;
    }
    if (goal == QA_NAV_NO_INDEX) {
        if (!qa_navigation_area(n, q->actor, q->goal, &goal, &found, e))
            return false;
        if (!found)
            return true;
    }
    uint32_t first = nav_node_index(n->graph, start), last = nav_node_index(n->graph, goal);
    if (first == QA_NAV_NO_INDEX || last == QA_NAV_NO_INDEX)
        return true;
    bool allowed;
    if (!nav_node_allowed(n, q->actor, first, q, false, &allowed, e))
        return false;
    if (!allowed)
        return true;
    if (!nav_workspace_prepare(w, n->graph, e))
        return false;
    for (;;) {
        size_t count;
        if (!candidate(n, w, q, first, last, &count, &found, e))
            return false;
        if (!found)
            return true;
        route->point_count = 0;
        route->travel_seconds = 0;
        if (!nav_route_point(route, q->start, e))
            return false;
        nav_prediction prediction = {.navigation = n};
        bool ok = true, failed = false;
        for (size_t i = 0; ok && i < count; ++i) {
            bool admitted;
            ok = traverse(n, &prediction, q->actor, w->path[i], route, &admitted, e);
            if (!ok || !admitted) {
                if (ok)
                    w->rejected[w->path[i]] = 1;
                failed = true;
                break;
            }
        }
        if (ok && !failed && nav_distance(cursor(route), q->goal) > 1) {
            bool admitted;
            ok = nav_predict(&prediction, q->actor, cursor(route), q->goal, QA_NAV_WALK, route,
                             &admitted, e);
            if (ok && !admitted) {
                nav_prediction_close(&prediction);
                return true;
            }
        }
        nav_prediction_close(&prediction);
        if (!ok)
            return false;
        if (failed)
            continue;
        if (!nav_reserve((void **)&route->nodes, &route->node_capacity, count + 1,
                         sizeof(*route->nodes), e) ||
            !nav_reserve((void **)&route->edges, &route->edge_capacity, count,
                         sizeof(*route->edges), e))
            return false;
        route->nodes[0] = start;
        for (size_t i = 0; i < count; ++i) {
            const qa_nav_edge *edge = n->graph->edges + w->path[i];
            route->edges[i] = edge->id;
            route->nodes[i + 1] = edge->to;
        }
        route->node_count = count + 1;
        route->edge_count = count;
        route->generation = n->generation;
        route->graph = n->graph;
        qa_nav_graph_retain(route->graph);
        route->found = true;
        return true;
    }
}
bool qa_navigation_admit_edge(qa_navigation *n, qa_actor_id actor, uint32_t id, qa_vec3 origin,
                              qa_nav_route *route, qa_error *e) {
    uint32_t index = n ? nav_edge_index(n->graph, id) : QA_NAV_NO_INDEX;
    if (!route || index == QA_NAV_NO_INDEX || !qa_vec_finite(origin)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, id, "Invalid navigation edge admission");
        return false;
    }
    route->found = false;
    route->node_count = route->edge_count = route->point_count = 0;
    route->travel_seconds = 0;
    qa_nav_graph_release(route->graph);
    route->graph = NULL;
    nav_refresh(n);
    bool allowed;
    if (!nav_edge_allowed(n, NULL, actor, index, NULL, &allowed, e)) return false;
    if (!allowed) return true;
    const qa_nav_edge *edge = n->graph->edges + index;
    if (!nav_route_point(route, origin, e)) return false;
    nav_prediction prediction = {.navigation = n};
    bool admitted;
    bool ok = traverse(n, &prediction, actor, index, route, &admitted, e);
    nav_prediction_close(&prediction);
    if (!ok || !admitted) return ok;
    if (!nav_reserve((void **)&route->nodes, &route->node_capacity, 2, sizeof(*route->nodes), e) ||
        !nav_reserve((void **)&route->edges, &route->edge_capacity, 1, sizeof(*route->edges), e))
        return false;
    route->nodes[0] = edge->from;
    route->nodes[1] = edge->to;
    route->edges[0] = edge->id;
    route->node_count = 2;
    route->edge_count = 1;
    route->generation = n->generation;
    route->graph = n->graph;
    qa_nav_graph_retain(route->graph);
    route->found = true;
    return true;
}
bool qa_navigation_admit_movement(qa_navigation *n, qa_actor_id actor, qa_vec3 from, qa_vec3 to,
                                  qa_nav_travel mode, qa_nav_route *route, qa_error *e) {
    if (!n || !route || !qa_vec_finite(from) || !qa_vec_finite(to) || (unsigned)mode >= QA_NAV_TRAVEL_COUNT) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid direct navigation movement admission");
        return false;
    }
    route->found = false;
    route->node_count = route->edge_count = route->point_count = 0;
    route->travel_seconds = 0;
    qa_nav_graph_release(route->graph);
    route->graph = NULL;
    if (!nav_route_point(route, from, e)) return false;
    nav_prediction prediction = {.navigation = n};
    bool admitted;
    bool ok = nav_predict(&prediction, actor, from, to, mode, route, &admitted, e);
    nav_prediction_close(&prediction);
    if (ok) route->found = admitted;
    return ok;
}
bool qa_navigation_route_valid(qa_navigation *n, qa_actor_id actor, const qa_nav_route *route,
                               bool *valid, qa_error *e) {
    if (n == NULL || route == NULL || valid == NULL ||
        (route->edge_count != 0 && route->edges == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation route validation request");
        return false;
    }
    nav_refresh(n);
    *valid = false;
    if (!route->found || route->graph != n->graph || route->generation != n->generation)
        return true;
    for (size_t i = 0; i < route->edge_count; ++i) {
        uint32_t index = nav_edge_index(n->graph, route->edges[i]);
        bool allowed;
        if (index == QA_NAV_NO_INDEX)
            return true;
        if (!nav_edge_allowed(n, NULL, actor, index, NULL, &allowed, e))
            return false;
        if (!allowed)
            return true;
    }
    *valid = true;
    return true;
}
