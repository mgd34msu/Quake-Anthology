#include "internal.h"

bool qa_navigation_create(qa_nav_graph *g, const qa_navigation_services *s, qa_arena *source, qa_navigation **out,
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
    qa_arena storage; nav_arena_init(&storage, source);
    qa_navigation *n = qa_arena_alloc(&storage, sizeof(*n), _Alignof(qa_navigation), e);
    if (n == NULL) { qa_arena_destroy(&storage); goto memory; }
    memset(n, 0, sizeof(*n)); n->storage = storage;
    nav_arena_init(&n->estimate_storage, source);
    nav_arena_init(&n->prediction_storage, source);
    nav_arena_init(&n->nearest_storage, source);
    n->graph = g;
    qa_nav_graph_retain(g);
    n->services = *s;
    size_t nodes = g->view.node_count, edges = g->view.edge_count;
    n->enabled = qa_arena_alloc(&n->storage, nodes ? nodes : 1, _Alignof(int8_t), e);
    n->blocked = qa_arena_alloc(&n->storage, edges ? edges : 1, _Alignof(uint8_t), e);
    n->admission_seconds = qa_arena_alloc(&n->storage,
        (edges ? edges : 1) * sizeof(*n->admission_seconds), _Alignof(float), e);
    if (n->enabled == NULL || n->blocked == NULL || n->admission_seconds == NULL) {
        qa_navigation_destroy(n);
        goto memory;
    }
    if (!nav_estimates_prepare(n, e)) {
        qa_navigation_destroy(n);
        return false;
    }
    memset(n->enabled, -1, nodes);
    memset(n->blocked, 0, edges);
    for (size_t i = 0; i < edges; ++i)
        n->admission_seconds[i] = NAN;
    if (!qa_pool_prepare(&n->prediction_results, &n->prediction_storage, 32,
            sizeof(qa_movement_result), _Alignof(qa_movement_result), e)) {
        qa_navigation_destroy(n);
        return false;
    }
    memset(n->prediction_results.values, 0,
        n->prediction_results.capacity * n->prediction_results.stride);
    for (size_t i = 0; !s->traversal_admit && i < n->prediction_results.capacity; ++i)
        if (!qa_movement_result_reserve(qa_pool_at(&n->prediction_results, i), 64, e)) {
            qa_navigation_destroy(n);
            return false;
        }
    qa_arena_seal(&n->prediction_storage);
    if (nodes) {
        if (nodes > SIZE_MAX / sizeof(nav_nearest_candidate)) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation nearest workspace exceeds addressable storage");
            qa_navigation_destroy(n);
            return false;
        }
        if (!qa_pool_reserve_pages(&n->nearest_candidates, &n->nearest_storage,
                n->prediction_results.capacity + 1, nodes * sizeof(nav_nearest_candidate),
                _Alignof(nav_nearest_candidate), e)) {
            qa_navigation_destroy(n);
            return false;
        }
    }
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
    qa_arena_destroy(&n->estimate_storage);
    for (size_t i = 0; i < n->prediction_results.capacity; ++i)
        qa_movement_result_free(qa_pool_at(&n->prediction_results, i));
    qa_arena_destroy(&n->prediction_storage);
    qa_arena_destroy(&n->nearest_storage);
    qa_nav_graph_release(n->graph);
    qa_arena storage = n->storage;
    qa_arena_destroy(&storage);
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
bool qa_nav_workspace_create(qa_arena *source, qa_nav_workspace **out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing navigation workspace output");
        return false;
    }
    qa_arena storage; nav_arena_init(&storage, source);
    qa_nav_workspace *w = qa_arena_alloc(&storage, sizeof(*w), _Alignof(qa_nav_workspace), e);
    if (w == NULL) {
        qa_arena_destroy(&storage);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation workspace");
        return false;
    }
    memset(w, 0, sizeof(*w)); w->owner_storage = storage;
    if (!qa_aas_query_create(source, &w->aas, e)) {
        qa_arena_destroy(&storage);
        return false;
    }
    *out = w;
    return true;
}
void qa_nav_workspace_destroy(qa_nav_workspace *w) {
    if (w == NULL)
        return;
    qa_aas_query_destroy(w->aas);
    qa_arena_destroy(&w->storage);
    qa_arena storage = w->owner_storage;
    qa_arena_destroy(&storage);
}
qa_nav_workspace_limits qa_nav_workspace_limits_read(const qa_nav_graph_view *graph) {
    const qa_aas_view *aas = graph ? qa_nav_asset_aas(graph->asset) : NULL;
    return (qa_nav_workspace_limits){
        .nodes = graph ? graph->node_count : 0,
        .edges = graph ? graph->edge_count : 0,
        .aas_nodes = aas ? aas->count[QA_AAS_NODES] : 0,
        .aas_areas = aas ? aas->count[QA_AAS_AREAS] : 0};
}
bool qa_nav_workspace_prepare(qa_nav_workspace *w, qa_nav_workspace_limits limits, qa_error *e) {
    size_t nodes = limits.nodes > w->node_capacity ? limits.nodes : w->node_capacity;
    size_t edges = limits.edges > w->edge_capacity ? limits.edges : w->edge_capacity;
    if (!qa_aas_query_prepare(w->aas, limits.aas_nodes, limits.aas_areas, e))
        return false;
    if (nodes == w->node_capacity && edges == w->edge_capacity)
        return true;
    size_t node_stride = sizeof(float) + 5 * sizeof(uint32_t) + sizeof(size_t) +
        2 * sizeof(bool) + sizeof(nav_queue_entry) + sizeof(nav_crossing);
    size_t padding = 11 * _Alignof(size_t);
    if (edges > (SIZE_MAX - padding) / sizeof(uint32_t) ||
        nodes > (SIZE_MAX - padding - edges * sizeof(uint32_t)) / node_stride)
        goto memory;
    qa_arena storage; nav_arena_init(&storage, &w->owner_storage);
    if (!qa_arena_reserve(&storage, padding + nodes * node_stride +
            edges * sizeof(uint32_t), e))
        return false;
    qa_nav_workspace prepared = *w;
    prepared.storage = storage;
#define NODE_SPAN(field, type) do { \
    prepared.field = nodes ? qa_arena_alloc(&prepared.storage, \
        nodes * sizeof(*prepared.field), _Alignof(type), e) : NULL; \
    if (nodes && !prepared.field) goto allocation; \
} while (0)
    NODE_SPAN(costs, float);
    NODE_SPAN(parents, uint32_t);
    NODE_SPAN(path, uint32_t);
    NODE_SPAN(repair, uint32_t);
    NODE_SPAN(queue_positions, size_t);
    NODE_SPAN(grounded, bool);
    NODE_SPAN(waiting, bool);
    NODE_SPAN(queue, nav_queue_entry);
    NODE_SPAN(crossings, nav_crossing);
#undef NODE_SPAN
    uint32_t *grounded_marks = nodes ? qa_arena_alloc(&prepared.storage,
        nodes * sizeof(*grounded_marks), _Alignof(uint32_t), e) : NULL;
    uint32_t *waiting_marks = nodes ? qa_arena_alloc(&prepared.storage,
        nodes * sizeof(*waiting_marks), _Alignof(uint32_t), e) : NULL;
    uint32_t *rejected = edges ? qa_arena_alloc(&prepared.storage,
        edges * sizeof(*rejected), _Alignof(uint32_t), e) : NULL;
    if ((nodes && (!grounded_marks || !waiting_marks)) || (edges && !rejected))
        goto allocation;
    for (size_t i = 0; i < nodes; ++i)
        prepared.queue_positions[i] = SIZE_MAX;
    qa_stamp_set_init(&prepared.grounded_valid, grounded_marks, nodes);
    qa_stamp_set_init(&prepared.waiting_valid, waiting_marks, nodes);
    qa_stamp_set_init(&prepared.rejected, rejected, edges);
    prepared.node_capacity = prepared.queue_capacity = prepared.crossing_capacity = nodes;
    prepared.edge_capacity = edges;
    prepared.queue_count = 0;
    qa_arena_seal(&prepared.storage);
    qa_arena_destroy(&w->storage);
    *w = prepared;
    return true;
allocation:
    qa_arena_destroy(&prepared.storage);
    return false;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation workspace exceeds addressable storage");
    return false;
}
bool nav_workspace_reset(qa_nav_workspace *w, const qa_nav_graph *g, qa_error *e) {
    if (g->view.node_count > w->node_capacity || g->view.edge_count > w->edge_capacity) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation workspace was not sized for its graph");
        return false;
    }
    qa_stamp_set_begin(&w->grounded_valid);
    qa_stamp_set_begin(&w->waiting_valid);
    qa_stamp_set_begin(&w->rejected);
    nav_queue_clear(w);
    return true;
}
void nav_queue_clear(qa_nav_workspace *w) {
    for(size_t i=0;i<w->queue_count;++i)
        w->queue_positions[w->queue[i].node]=SIZE_MAX;
    w->queue_count=0;
}
static void queue_assign(qa_nav_workspace *w,size_t index,nav_queue_entry entry) {
    bool upward=index!=0 && entry.cost<w->queue[(index-1)/2].cost;
    while(upward && index!=0) {
        size_t parent = (index - 1) / 2;
        if (w->queue[parent].cost <= entry.cost)
            break;
        w->queue[index] = w->queue[parent];
        w->queue_positions[w->queue[index].node]=index;
        index = parent;
    }
    while(!upward && index<w->queue_count/2) {
        size_t child = index * 2 + 1;
        if (child + 1 < w->queue_count && w->queue[child + 1].cost < w->queue[child].cost)
            ++child;
        if (entry.cost <= w->queue[child].cost)
            break;
        w->queue[index] = w->queue[child];
        w->queue_positions[w->queue[index].node]=index;
        index = child;
    }
    w->queue[index]=entry;w->queue_positions[entry.node]=index;
}
bool nav_queue_push(qa_nav_workspace *w, nav_queue_entry entry, qa_error *e) {
    size_t index=w->queue_positions[entry.node];
    if(index==SIZE_MAX) {
        if (w->queue_count == w->queue_capacity) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation queue storage is full");
            return false;
        }
        index=w->queue_count++;
    }
    queue_assign(w,index,entry);return true;
}
void nav_queue_remove(qa_nav_workspace *w,uint32_t node) {
    size_t index=w->queue_positions[node];
    if(index==SIZE_MAX) return;
    w->queue_positions[node]=SIZE_MAX;
    nav_queue_entry tail=w->queue[--w->queue_count];
    if(index<w->queue_count) queue_assign(w,index,tail);
}
bool nav_queue_pop(qa_nav_workspace *w, nav_queue_entry *out) {
    if(!w->queue_count) return false;
    *out=w->queue[0];nav_queue_remove(w,out->node);
    return true;
}
bool nav_storage_reserve(qa_arena *storage, void **data, size_t *capacity,
    size_t need, size_t stride, size_t alignment, qa_error *e) {
    if (need <= *capacity) return true;
    size_t count = *capacity < 64 ? 64 : *capacity;
    while (count < need && count <= SIZE_MAX / 2) count *= 2;
    if (count < need || count > SIZE_MAX / stride) {
        qa_error_set(e, QA_ERROR_MEMORY, need, "Navigation storage extent overflow");
        return false;
    }
    void *next = qa_arena_grow(storage, *data, *capacity * stride,
        count * stride, alignment, e);
    if (!next) return false;
    *data = next; *capacity = count; return true;
}
bool qa_nav_prediction_result_reserve(qa_nav_prediction_result *result, size_t points,
    qa_error *e) {
    return nav_storage_reserve(&result->storage, (void **)&result->trajectory,
        &result->trajectory_capacity, points, sizeof(*result->trajectory),
        _Alignof(qa_vec3), e);
}
void qa_nav_prediction_result_reset(qa_nav_prediction_result *result) {
    if (!result) return;
    qa_arena storage = result->storage;
    qa_vec3 *trajectory = result->trajectory;
    size_t capacity = result->trajectory_capacity;
    *result = (qa_nav_prediction_result){.storage = storage,
        .trajectory = trajectory, .trajectory_capacity = capacity};
}
bool qa_nav_route_reserve(qa_nav_route *route, size_t nodes, size_t edges,
    size_t points, qa_error *e) {
    return nav_storage_reserve(&route->storage, (void **)&route->nodes, &route->node_capacity,
               nodes, sizeof(*route->nodes), _Alignof(uint32_t), e) &&
        nav_storage_reserve(&route->storage, (void **)&route->edges, &route->edge_capacity,
               edges, sizeof(*route->edges), _Alignof(uint32_t), e) &&
        nav_storage_reserve(&route->storage, (void **)&route->points, &route->point_capacity,
               points, sizeof(*route->points), _Alignof(qa_vec3), e);
}
bool nav_route_point(qa_nav_route *route, qa_vec3 point, qa_error *e) {
    if (!qa_nav_route_reserve(route, 0, 0, route->point_count + 1, e)) return false;
    route->points[route->point_count++] = point;
    return true;
}
void qa_nav_route_reset(qa_nav_route *r) {
    if (r == NULL)
        return;
    qa_nav_graph_release(r->graph);
    r->graph = NULL;
    r->node_count = r->edge_count = r->point_count = 0;
    r->travel_seconds = 0;
    r->generation = 0;
    r->found = false;
}
void qa_nav_route_free(qa_nav_route *r) {
    if (r == NULL)
        return;
    qa_nav_route_reset(r);
    qa_arena_destroy(&r->storage);
    *r = (qa_nav_route){0};
}
