#include "internal.h"

enum { NAV3_CONDITIONAL = 32 | 64 | 128 | 2048 | 4096 };

bool nav_static_node(const qa_navigation *n, uint32_t index, const qa_nav_route_query *q) {
    const qa_nav_node *node = n->graph->nodes + index;
    qa_nav_profile p;
    if (n->enabled[index] == 0 || (q && q->node_filter && !q->node_filter(q->context, node)))
        return false;
    if (q != NULL)
        for (size_t i = 0; i < q->disabled_count; ++i)
            if (q->disabled_areas[i] == node->id)
                return false;
    if (node->source.kind == QA_NAV_ORIGIN_AAS && (node->flags & 8) != 0 && n->enabled[index] != 1)
        return false;
    if (node->source.kind == QA_NAV_ORIGIN_NAV3 && (node->flags & 8192) != 0 &&
        (node->flags & NAV3_CONDITIONAL) == 0)
        return false;
    if (q && q->advisory) return true;
    if (!nav_node_profile(&n->graph->view.profile, node, &p)) return false;
    if (node->source.kind == QA_NAV_ORIGIN_NAV3 &&
        ((p.monster && (node->flags & 256) != 0) ||
         ((node->flags & 512) != 0 && (p.capabilities & QA_NAV_CAPABILITY(QA_NAV_CROUCH)) == 0)))
        return false;
    return true;
}
bool nav_static_edge(const qa_navigation *n, uint32_t index, const qa_nav_route_query *q) {
    const qa_nav_edge *edge = n->graph->edges + index;
    const qa_nav_profile *p = &n->graph->view.profile;
    if ((!(q && q->advisory) && (edge->mode == QA_NAV_UNKNOWN ||
            (p->capabilities & QA_NAV_CAPABILITY(edge->mode)) == 0)) ||
        n->blocked[index])
        return false;
    if (q != NULL &&
        ((q->edge_filter != NULL && !q->edge_filter(q->context, edge)) ||
         (q->has_travel_flags && (qa_nav_edge_travel_flag(edge) & q->travel_flags) == 0)))
        return false;
    uint32_t from = nav_node_index(n->graph, edge->from), to = nav_node_index(n->graph, edge->to);
    if (!nav_static_node(n, from, q) || !nav_static_node(n, to, q))
        return false;
    if (q && q->advisory) return true;
    if (edge->mode == QA_NAV_DROP && edge->start.z - edge->end.z > p->maximum_drop) return false;
    if (edge->source.kind == QA_NAV_ORIGIN_AAS) {
        if ((q != NULL && q->has_travel_flags &&
             (qa_nav_aas_travel_flag(edge->source_travel_type) & q->travel_flags) == 0) ||
            (p->team == 1 && (edge->source_travel_type & UINT32_C(0x01000000)) != 0) ||
            (p->team == 2 && (edge->source_travel_type & UINT32_C(0x02000000)) != 0))
            return false;
        const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
        if (aas != NULL) {
            const qa_aas_setting *setting = aas->settings + edge->to;
            if ((q != NULL && q->has_travel_flags &&
                 (qa_nav_area_travel_flags(setting) & ~q->travel_flags) != 0) ||
                (p->team == 1 && (setting->contents & 2048) != 0) ||
                (p->team == 2 && (setting->contents & 4096) != 0))
                return false;
        }
    } else if (edge->source.kind == QA_NAV_ORIGIN_NAV3) {
        if ((edge->source_flags & 64) != 0 ||
            (p->team != 0 && (edge->source_flags & (p->team == 1 ? 1u : 2u)) == 0))
            return false;
    }
    return true;
}
bool nav_node_allowed(qa_navigation *n, qa_actor_id actor, uint32_t index,
                      const qa_nav_route_query *q, bool await_mover, bool *allowed, qa_error *e) {
    *allowed = false;
    if (!nav_static_node(n, index, q))
        return true;
    const qa_nav_node *node = n->graph->nodes + index;
    if (node->source.kind == QA_NAV_ORIGIN_NAV3 && (node->flags & NAV3_CONDITIONAL)) {
        qa_nav_profile condition = n->graph->view.profile;
        qa_vec3 origin = node->origin;
        /* Graph origins include hull rise; conditionals use authored origin plus 24. */
        origin.z += 24 + condition.shape.bounds.mins.z;
        condition.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX,
            .bounds = {qa_v3(-16, -16, -24), qa_v3(16, 16, (node->flags & 512) ? 4 : 32)}};
        const uint32_t masks[] = {3, 56, 24};
        const uint32_t flags[] = {128, 2048, 32};
        for (size_t i = 0; i < 3; ++i) {
            if (!(node->flags & flags[i])) continue;
            condition.policy.contents_mask = qa_collision_contents_mask(masks[i], QA_GAME_Q2);
            qa_trace_result trace;
            if (!nav_trace(&n->services, &condition, actor, origin, origin, false, &trace, e))
                return false;
            bool inside = trace.start_solid || trace.all_solid;
            if (i == 1 ? !inside : inside) return true;
            if (i == 2 && n->services.hazard) {
                bool hazardous;
                if (!n->services.hazard(n->services.context,
                    qa_bounds_translate(condition.shape.bounds, origin), &hazardous, e)) return false;
                if (hazardous) return true;
            }
        }
        if (node->flags & 64) {
            condition.policy.contents_mask = qa_collision_contents_mask(3, QA_GAME_Q2);
            condition.shape.bounds.mins.z = condition.shape.bounds.maxs.z = 0;
            qa_vec3 end = origin;
            end.z -= 96;
            qa_trace_result trace;
            if (!nav_trace(&n->services, &condition, actor, origin, end, false, &trace, e))
                return false;
            if (trace.fraction == 1) return true;
        }
        if (node->flags & 4096) {
            for (uint32_t i = n->graph->first_out[index]; i < n->graph->first_out[index + 1]; ++i) {
                const qa_nav_edge *edge = n->graph->edges + n->graph->outgoing[i];
                qa_nav_entity_state source;
                bool found;
                if (!nav_entity(n, edge, &source, &found, e)) return false;
                if (found && source.locked) return true;
            }
        }
    }
    if (q && q->advisory) { *allowed = true; return true; }
    qa_nav_profile p;
    if (!nav_node_profile(&n->graph->view.profile, node, &p))
        return true;
    uint32_t medium;
    if (!nav_contents(&n->services, &p, actor, node->origin, false, &medium, e))
        return false;
    if ((medium & (QA_NAV_SLIME | QA_NAV_LAVA)) != 0 ||
        ((medium & QA_NAV_WATER) != 0 && (p.capabilities & QA_NAV_CAPABILITY(QA_NAV_SWIM)) == 0))
        return true;
    if (n->services.hazard != NULL) {
        bool hazardous;
        if (!n->services.hazard(n->services.context, qa_bounds_translate(p.shape.bounds, node->origin),
            &hazardous, e)) return false;
        if (hazardous) return true;
    }
    if (!await_mover) {
        bool clear;
        if (!nav_clear(&n->services, &p, actor, node->origin, node->origin, false, &clear, e))
            return false;
        if (!clear)
            return true;
    }
    *allowed = true;
    return true;
}
bool qa_navigation_node_allowed(qa_navigation *n, qa_actor_id actor, uint32_t node,
    const qa_nav_route_query *q, bool *allowed, qa_error *e) {
    uint32_t index = n ? nav_node_index(n->graph, node) : QA_NAV_NO_INDEX;
    if (index == QA_NAV_NO_INDEX || !allowed || (q && q->disabled_count && !q->disabled_areas)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, node, "Unknown navigation node/output");
        return false;
    }
    nav_refresh(n);
    return nav_node_allowed(n, actor, index, q, false, allowed, e);
}
bool nav_entity(qa_navigation *n, const qa_nav_edge *edge, qa_nav_entity_state *out, bool *found,
                qa_error *e) {
    *found = false;
    if (!edge->has_entity || n->services.entity == NULL)
        return true;
    if (!n->services.entity(n->services.context, &edge->entity, out, found, e))
        return false;
    if (*found &&
        (!qa_bounds_valid(out->bounds) || !qa_vec_finite(out->velocity) ||
         (out->has_destination && !qa_vec_finite(out->destination)) ||
         (unsigned)out->kind > QA_NAV_ENTITY_TRAIN ||
         (out->kind == QA_NAV_ENTITY_TRAIN &&
          (!qa_vec_finite(out->data.train.origin) ||
           (out->data.train.count != 0 && out->data.train.stops == NULL))) ||
         (out->kind == QA_NAV_ENTITY_ELEVATOR &&
          (!qa_vec_finite(out->data.elevator.origin) || !qa_vec_finite(out->data.elevator.bottom) ||
           !qa_vec_finite(out->data.elevator.top) ||
           (unsigned)out->data.elevator.phase > QA_NAV_MOVER_DOWN)))) {
        qa_error_set(e, QA_ERROR_ARGUMENT, edge->id, "Invalid live navigation entity observation");
        return false;
    }
    return true;
}
bool nav_edge_allowed(qa_navigation *n, qa_nav_workspace *w, qa_actor_id actor, uint32_t index,
                      const qa_nav_route_query *q, bool *allowed, qa_error *e) {
    *allowed = false;
    if (!nav_static_edge(n, index, q))
        return true;
    const qa_nav_edge *edge = n->graph->edges + index;
    uint32_t to = nav_node_index(n->graph, edge->to);
    if (q && q->advisory) {
        if (w && qa_stamp_set_test(&w->grounded_valid, to)) *allowed = w->grounded[to];
        else {
            if (!nav_node_allowed(n, actor, to, q, false, allowed, e)) return false;
            if (w) {
                w->grounded[to] = *allowed;
                qa_stamp_set_mark(&w->grounded_valid, to);
            }
        }
        return true;
    }
    qa_nav_entity_state elevator, train, source;
    const qa_nav_edge *boarding;
    qa_nav_train_ride ride;
    bool has_elevator, has_train, has_source;
    if (!nav_boarding(n, edge->to, false, &boarding, &elevator, &ride, &has_elevator, e))
        return false;
    if (!nav_boarding(n, edge->to, true, &boarding, &train, &ride, &has_train, e))
        return false;
    bool awaiting_train = has_train &&
        !qa_navigation_train_at_stop(&train,&ride.boarding,&n->graph->view.profile);
    if (!nav_entity(n, edge, &source, &has_source, e))
        return false;
    if (edge->mode == QA_NAV_MOVER && has_source && source.kind == QA_NAV_ENTITY_TRAIN &&
        source.data.train.running &&
        qa_navigation_train_ride(&source, edge, &n->graph->view.profile, &ride) &&
        !qa_navigation_train_at_stop(&source,&ride.arrival,&n->graph->view.profile))
        awaiting_train = true;
    if (awaiting_train) {
        bool clear;
        qa_vec3 origin = n->graph->nodes[to].origin;
        if (!nav_clear(&n->services, &n->graph->view.profile, actor, origin, origin, false, &clear,
                       e))
            return false;
        if (!clear)
            return true;
    }
    bool waiting = awaiting_train ||
                   (has_elevator && elevator.data.elevator.phase != QA_NAV_MOVER_BOTTOM),
         node_allowed;
    bool *cache = w == NULL ? NULL : waiting ? w->waiting : w->grounded;
    qa_stamp_set *valid = w == NULL ? NULL : waiting ? &w->waiting_valid : &w->grounded_valid;
    if (valid != NULL && qa_stamp_set_test(valid, to))
        node_allowed = cache[to];
    else {
        if (!nav_node_allowed(n, actor, to, q, waiting, &node_allowed, e))
            return false;
        if (cache != NULL) {
            cache[to] = node_allowed;
            qa_stamp_set_mark(valid, to);
        }
    }
    if (!node_allowed || (edge->has_entity && (!has_source || !source.enabled || source.locked)))
        return true;
    *allowed = true;
    return true;
}
bool qa_navigation_edge_allowed(qa_navigation *n, qa_actor_id actor, uint32_t edge, bool *allowed,
                                qa_error *e) {
    uint32_t index = n == NULL ? QA_NAV_NO_INDEX : nav_edge_index(n->graph, edge);
    if (index == QA_NAV_NO_INDEX || allowed == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, edge, "Unknown navigation edge/output");
        return false;
    }
    nav_refresh(n);
    return nav_edge_allowed(n, NULL, actor, index, NULL, allowed, e);
}
