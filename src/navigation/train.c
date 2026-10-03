#include "internal.h"

static bool supports(const qa_nav_entity_state *state, const qa_nav_train_stop *stop, qa_vec3 point,
                     const qa_nav_profile *p) {
    qa_bounds bounds = qa_bounds_translate(state->bounds,
                                           qa_vec_sub(stop->origin, state->data.train.origin)),
              shape = p->shape.bounds;
    return point.x + shape.maxs.x > bounds.mins.x && point.x + shape.mins.x < bounds.maxs.x &&
           point.y + shape.maxs.y > bounds.mins.y && point.y + shape.mins.y < bounds.maxs.y &&
           fabsf(point.z + shape.mins.z - bounds.maxs.z) <= p->maximum_step;
}
bool qa_navigation_train_ride(const qa_nav_entity_state *state, const qa_nav_edge *edge,
                              const qa_nav_profile *p, qa_nav_train_ride *out) {
    if (state == NULL || edge == NULL || p == NULL || out == NULL ||
        state->kind != QA_NAV_ENTITY_TRAIN || !state->enabled || state->locked ||
        (p->capabilities & QA_NAV_CAPABILITY(QA_NAV_MOVER)) == 0 ||
        (state->data.train.count != 0 && state->data.train.stops == NULL))
        return false;
    const qa_nav_train_stop *boarding = NULL, *arrival = NULL, *stops = state->data.train.stops;
    size_t count = state->data.train.count;
    for (size_t i = 0; i < count && (boarding == NULL || arrival == NULL); ++i) {
        if (stops[i].teleport)
            continue;
        if (boarding == NULL && supports(state, stops + i, edge->start, p))
            boarding = stops + i;
        if (arrival == NULL && supports(state, stops + i, edge->end, p))
            arrival = stops + i;
    }
    if (boarding == NULL || arrival == NULL || boarding->id == arrival->id)
        return false;
    const qa_nav_train_stop *current = boarding;
    for (size_t steps = 0; current != NULL && steps < count; ++steps) {
        if (current->id == arrival->id) {
            *out = (qa_nav_train_ride){*boarding, *arrival};
            return true;
        }
        if (current->wait < 0 || current->teleport || current->next == QA_NAV_NO_INDEX)
            return false;
        uint32_t next = current->next;
        current = NULL;
        for (size_t i = 0; i < count; ++i)
            if (stops[i].id == next) {
                current = stops + i;
                break;
            }
    }
    return false;
}
bool nav_boarding(qa_navigation *n, uint32_t id, bool train, const qa_nav_edge **edge,
                  qa_nav_entity_state *state, qa_nav_train_ride *ride, bool *found, qa_error *e) {
    *found = false;
    uint32_t index = nav_node_index(n->graph, id);
    if (index == QA_NAV_NO_INDEX)
        return true;
    for (uint32_t i = n->graph->first_mover_out[index];
         i < n->graph->first_mover_out[index + 1]; ++i) {
        const qa_nav_edge *candidate = n->graph->edges + n->graph->mover_outgoing[i];
        if (!train && ((candidate->source.kind != QA_NAV_ORIGIN_NAV2 &&
                        candidate->source.kind != QA_NAV_ORIGIN_NAV3) ||
                       candidate->source_travel_type != 6))
            continue;
        bool exists;
        if (!nav_entity(n, candidate, state, &exists, e))
            return false;
        if (!exists || !state->enabled || state->locked)
            continue;
        if (train ? state->kind == QA_NAV_ENTITY_TRAIN && state->data.train.running &&
                        qa_navigation_train_ride(state, candidate, &n->graph->view.profile, ride)
                  : state->kind == QA_NAV_ENTITY_ELEVATOR) {
            *edge = candidate;
            *found = true;
            return true;
        }
    }
    return true;
}
bool qa_navigation_train_step(qa_navigation *n, qa_nav_workspace *w, qa_actor_id actor,
                              uint32_t edge_id, qa_vec3 origin, qa_actor_id ground,
                              qa_nav_train_step *out, qa_error *e) {
    const qa_nav_edge *edge = qa_navigation_edge(n, edge_id);
    if (n == NULL || w == NULL || out == NULL || edge == NULL || !qa_vec_finite(origin)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, edge_id, "Invalid train traversal query");
        return false;
    }
    *out = (qa_nav_train_step){.stage = QA_NAV_TRAIN_ABSENT};
    if (edge->mode != QA_NAV_MOVER || !edge->has_entity)
        return true;
    qa_nav_entity_state state;
    bool found;
    if (!nav_entity(n, edge, &state, &found, e))
        return false;
    if (!found || state.kind != QA_NAV_ENTITY_TRAIN)
        return true;
    out->stage = QA_NAV_TRAIN_UNAVAILABLE;
    const qa_nav_profile *p = &n->graph->view.profile;
    qa_nav_train_ride ride;
    bool aboard = qa_actor_id_equal(ground, state.actor);
    if (!qa_navigation_train_ride(&state, edge, p, &ride) ||
        !nav_static_edge(n, nav_edge_index(n->graph, edge_id), NULL) ||
        (!aboard && !state.data.train.running))
        return true;
    if (n->services.hazard != NULL &&
        n->services.hazard(n->services.context, qa_bounds_translate(p->shape.bounds, origin)))
        return true;
    bool arrived = nav_distance(state.data.train.origin, ride.arrival.origin) <= p->maximum_step;
    if (aboard && !arrived) {
        if (state.data.train.running)
            out->stage = QA_NAV_TRAIN_RIDE;
        return true;
    }
    qa_vec3 staging = edge->has_hint ? edge->hint.funnel : edge->start;
    bool at_boarding =
        nav_distance(state.data.train.origin, ride.boarding.origin) <= p->maximum_step;
    qa_vec3 target = aboard ? edge->end : at_boarding ? edge->start : staging;
    if (!aboard && !at_boarding && nav_distance(origin, staging) <= 8) {
        out->stage = QA_NAV_TRAIN_WAIT;
        return true;
    }
    if (n->services.hazard != NULL &&
        n->services.hazard(n->services.context, qa_bounds_translate(p->shape.bounds, target)))
        return true;
    nav_prediction prediction = {.navigation = n};
    qa_nav_route route = {0};
    bool admitted;
    bool ok = nav_predict(&prediction, actor, origin, target, QA_NAV_WALK, &route, &admitted, e);
    nav_prediction_close(&prediction);
    qa_nav_route_free(&route);
    if (ok && admitted) {
        out->stage = aboard ? QA_NAV_TRAIN_EXIT : QA_NAV_TRAIN_APPROACH;
        out->target = target;
    }
    return ok;
}
