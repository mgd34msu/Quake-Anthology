#include "internal.h"

bool nav_reserve(void **data, size_t *capacity, size_t need, size_t stride, qa_error *e) {
    if (need <= *capacity)
        return true;
    size_t count = *capacity < 64 ? 64 : *capacity;
    while (count < need && count <= SIZE_MAX / 2)
        count *= 2;
    if (count < need || count > SIZE_MAX / stride)
        goto memory;
    void *next = realloc(*data, count * stride);
    if (next == NULL)
        goto memory;
    *data = next;
    *capacity = count;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, need, "Growing retained navigation workspace");
    return false;
}
bool qa_navigation_create(qa_nav_graph *g, const qa_navigation_services *s, qa_navigation **out,
                          qa_error *e) {
    if (g == NULL || out == NULL || !nav_services_valid(s, false, e)) {
        if (g == NULL || out == NULL)
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation graph/output");
        return false;
    }
    if ((s->prediction_begin == NULL) != (s->prediction_end == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Navigation prediction lease requires paired begin/end hooks");
        return false;
    }
    qa_navigation *n = calloc(1, sizeof(*n));
    if (n == NULL)
        goto memory;
    n->graph = g;
    qa_nav_graph_retain(g);
    n->services = *s;
    size_t nodes = g->view.node_count, edges = g->view.edge_count;
    n->enabled = malloc(nodes == 0 ? 1 : nodes);
    n->blocked = calloc(edges == 0 ? 1 : edges, 1);
    n->admission_seconds = malloc((edges == 0 ? 1 : edges) * sizeof(*n->admission_seconds));
    if (n->enabled == NULL || n->blocked == NULL || n->admission_seconds == NULL) {
        qa_navigation_destroy(n);
        goto memory;
    }
    memset(n->enabled, -1, nodes);
    for (size_t i = 0; i < edges; ++i)
        n->admission_seconds[i] = NAN;
    n->world_revision = s->revision == NULL ? 0 : s->revision(s->context);
    *out = n;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation runtime");
    return false;
}
void qa_navigation_destroy(qa_navigation *n) {
    if (n == NULL)
        return;
    nav_estimates_free(n);
    qa_nav_graph_release(n->graph);
    free(n->enabled);
    free(n->blocked);
    free(n->admission_seconds);
    free(n);
}
static void invalidate(qa_navigation *n, bool topology) {
    ++n->generation;
    for (size_t i = 0; i < n->graph->view.edge_count; ++i)
        n->admission_seconds[i] = NAN;
    if (topology) {
        ++n->topology_revision;
        nav_estimates_free(n);
    }
}
void nav_refresh(qa_navigation *n) {
    uint64_t revision =
        n->services.revision == NULL ? 0 : n->services.revision(n->services.context);
    if (n->world_revision != revision) {
        n->world_revision = revision;
        invalidate(n, false);
    }
}
uint64_t qa_navigation_generation(qa_navigation *n) {
    if (n == NULL)
        return 0;
    nav_refresh(n);
    return n->generation;
}
const qa_nav_node *qa_navigation_node(const qa_navigation *n, uint32_t id) {
    if (n == NULL)
        return NULL;
    uint32_t index = nav_node_index(n->graph, id);
    return index == QA_NAV_NO_INDEX ? NULL : n->graph->nodes + index;
}
const qa_nav_edge *qa_navigation_edge(const qa_navigation *n, uint32_t id) {
    if (n == NULL)
        return NULL;
    uint32_t index = nav_edge_index(n->graph, id);
    return index == QA_NAV_NO_INDEX ? NULL : n->graph->edges + index;
}
const qa_nav_graph_view *qa_navigation_graph(const qa_navigation *n) {
    return n ? &n->graph->view : NULL;
}
size_t qa_navigation_outgoing_count(const qa_navigation *n, uint32_t id) {
    uint32_t node = n ? nav_node_index(n->graph, id) : QA_NAV_NO_INDEX;
    return node == QA_NAV_NO_INDEX ? 0 : n->graph->first_out[node + 1] - n->graph->first_out[node];
}
const qa_nav_edge *qa_navigation_outgoing(const qa_navigation *n, uint32_t id, size_t index) {
    uint32_t node = n ? nav_node_index(n->graph, id) : QA_NAV_NO_INDEX;
    if (node == QA_NAV_NO_INDEX || index >= qa_navigation_outgoing_count(n, id))
        return NULL;
    return n->graph->edges + n->graph->outgoing[n->graph->first_out[node] + index];
}
size_t qa_navigation_adjacent_count(const qa_navigation *n, uint32_t id) {
    uint32_t node = n ? nav_node_index(n->graph, id) : QA_NAV_NO_INDEX;
    if (node == QA_NAV_NO_INDEX) return 0;
    return (size_t)(n->graph->first_out[node + 1] - n->graph->first_out[node]) +
           (size_t)(n->graph->first_in[node + 1] - n->graph->first_in[node]);
}
const qa_nav_edge *qa_navigation_adjacent_next(const qa_navigation *n, uint32_t id,
                                               qa_nav_adjacency_cursor *cursor) {
    uint32_t node = n ? nav_node_index(n->graph, id) : QA_NAV_NO_INDEX;
    if (node == QA_NAV_NO_INDEX || !cursor) return NULL;
    const qa_nav_graph *g = n->graph;
    if (cursor->outgoing > g->first_out[node + 1] - g->first_out[node] ||
        cursor->incoming > g->first_in[node + 1] - g->first_in[node]) return NULL;
    uint32_t out = g->first_out[node] + cursor->outgoing;
    uint32_t in = g->first_in[node] + cursor->incoming;
    uint32_t out_end = g->first_out[node + 1], in_end = g->first_in[node + 1];
    if (out == out_end && in == in_end) return NULL;
    if (in == in_end || (out < out_end && g->outgoing[out] <= g->incoming[in])) {
        ++cursor->outgoing;
        return g->edges + g->outgoing[out];
    }
    ++cursor->incoming;
    return g->edges + g->incoming[in];
}
bool qa_navigation_enabled(const qa_navigation *n, uint32_t area, bool *enabled, bool *overridden) {
    uint32_t index = n ? nav_node_index(n->graph, area) : QA_NAV_NO_INDEX;
    if (index == QA_NAV_NO_INDEX || !enabled || !overridden) return false;
    const qa_nav_node *node = &n->graph->nodes[index];
    *overridden = n->enabled[index] >= 0;
    *enabled = *overridden ? n->enabled[index] != 0 :
        !(node->source.kind == QA_NAV_ORIGIN_AAS && (node->flags & 8));
    return true;
}
bool qa_navigation_boarding_elevator(qa_navigation *n, uint32_t area, const qa_nav_edge **edge,
                                       qa_nav_entity_state *state, bool *found, qa_error *e) {
    if (!n || !edge || !state || !found) {
        qa_error_set(e, QA_ERROR_ARGUMENT, area, "Invalid navigation boarding elevator query");
        return false;
    }
    qa_nav_train_ride unused;
    return nav_boarding(n, area, false, edge, state, &unused, found, e);
}
bool qa_navigation_enable(qa_navigation *n, uint32_t area, bool enabled, bool *previous,
                          qa_error *e) {
    uint32_t index = n == NULL ? QA_NAV_NO_INDEX : nav_node_index(n->graph, area);
    if (index == QA_NAV_NO_INDEX || previous == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, area, "Unknown navigation area/output");
        return false;
    }
    const qa_nav_node *node = n->graph->nodes + index;
    *previous = n->enabled[index] < 0
                    ? !(node->source.kind == QA_NAV_ORIGIN_AAS && (node->flags & 8) != 0)
                    : n->enabled[index] != 0;
    if (*previous != enabled) {
        n->enabled[index] = enabled ? 1 : 0;
        invalidate(n, true);
    }
    return true;
}
bool qa_navigation_block(qa_navigation *n, uint32_t edge, bool blocked, qa_error *e) {
    uint32_t index = n == NULL ? QA_NAV_NO_INDEX : nav_edge_index(n->graph, edge);
    if (index == QA_NAV_NO_INDEX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, edge, "Unknown navigation edge");
        return false;
    }
    n->blocked[index] = blocked;
    invalidate(n, true);
    return true;
}
bool qa_nav_workspace_create(qa_nav_workspace **out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation workspace output");
        return false;
    }
    qa_nav_workspace *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation workspace");
        return false;
    }
    if (!qa_aas_query_create(&w->aas, e)) {
        free(w);
        return false;
    }
    *out = w;
    return true;
}
void qa_nav_workspace_destroy(qa_nav_workspace *w) {
    if (w == NULL)
        return;
    qa_aas_query_destroy(w->aas);
    free(w->costs);
    free(w->parents);
    free(w->path);
    free(w->repair);
    free(w->grounded);
    free(w->waiting);
    free(w->rejected);
    free(w->queue);
    free(w->crossings);
    free(w);
}
bool nav_workspace_prepare(qa_nav_workspace *w, const qa_nav_graph *g, qa_error *e) {
    size_t n = g->view.node_count, m = g->view.edge_count;
    if (w->node_capacity < n) {
        if (n > SIZE_MAX / sizeof(float) || n > SIZE_MAX / sizeof(uint32_t))
            goto memory;
        float *costs = malloc(n * sizeof(*costs));
        uint32_t *parents = malloc(n * sizeof(*parents)), *path = malloc(n * sizeof(*path)),
                 *repair = malloc(n * sizeof(*repair));
        int8_t *grounded = malloc(n), *waiting = malloc(n);
        if (costs == NULL || parents == NULL || path == NULL || repair == NULL || grounded == NULL ||
            waiting == NULL) {
            free(costs);
            free(parents);
            free(path);
            free(repair);
            free(grounded);
            free(waiting);
            goto memory;
        }
        free(w->costs);
        free(w->parents);
        free(w->path);
        free(w->repair);
        free(w->grounded);
        free(w->waiting);
        w->costs = costs;
        w->parents = parents;
        w->path = path;
        w->repair = repair;
        w->grounded = grounded;
        w->waiting = waiting;
        w->node_capacity = n;
    }
    if (w->edge_capacity < m) {
        uint8_t *rejected = realloc(w->rejected, m);
        if (rejected == NULL)
            goto memory;
        w->rejected = rejected;
        w->edge_capacity = m;
    }
    if (n != 0) {
        memset(w->grounded, -1, n);
        memset(w->waiting, -1, n);
    }
    if (m != 0)
        memset(w->rejected, 0, m);
    w->queue_count = 0;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating retained navigation search state");
    return false;
}
bool nav_queue_push(qa_nav_workspace *w, nav_queue_entry entry, qa_error *e) {
    if (!nav_reserve((void **)&w->queue, &w->queue_capacity, w->queue_count + 1, sizeof(*w->queue),
                     e))
        return false;
    size_t index = w->queue_count++;
    while (index != 0) {
        size_t parent = (index - 1) / 2;
        if (w->queue[parent].cost <= entry.cost)
            break;
        w->queue[index] = w->queue[parent];
        index = parent;
    }
    w->queue[index] = entry;
    return true;
}
bool nav_queue_pop(qa_nav_workspace *w, nav_queue_entry *out) {
    if (w->queue_count == 0)
        return false;
    *out = w->queue[0];
    nav_queue_entry tail = w->queue[--w->queue_count];
    if (w->queue_count == 0)
        return true;
    size_t index = 0;
    while (index < w->queue_count / 2) {
        size_t child = index * 2 + 1;
        if (child + 1 < w->queue_count && w->queue[child + 1].cost < w->queue[child].cost)
            ++child;
        if (tail.cost <= w->queue[child].cost)
            break;
        w->queue[index] = w->queue[child];
        index = child;
    }
    w->queue[index] = tail;
    return true;
}
bool nav_route_point(qa_nav_route *route, qa_vec3 point, qa_error *e) {
    if (!nav_reserve((void **)&route->points, &route->point_capacity, route->point_count + 1,
                     sizeof(*route->points), e))
        return false;
    route->points[route->point_count++] = point;
    return true;
}
void qa_nav_route_free(qa_nav_route *r) {
    if (r == NULL)
        return;
    qa_nav_graph_release(r->graph);
    free(r->nodes);
    free(r->edges);
    free(r->points);
    *r = (qa_nav_route){0};
}
