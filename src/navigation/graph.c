#include "internal.h"

bool nav_graph_new(const qa_nav_map *map, const qa_nav_profile *profile, qa_nav_graph **out,
                   qa_error *e) {
    if (map == NULL || out == NULL || !nav_profile_valid(profile, e))
        return false;
    qa_nav_graph *g = calloc(1, sizeof(*g));
    if (g == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation graph");
        return false;
    }
    atomic_init(&g->references, 1);
    g->view.map = *map;
    g->view.profile = *profile;
    *out = g;
    return true;
}
bool nav_graph_node(qa_nav_graph *g, const qa_nav_node *node, qa_error *e) {
    size_t n = g->view.node_count;
    if (n == UINT32_MAX)
        goto memory;
    if (n == g->node_capacity) {
        size_t capacity = g->node_capacity == 0 ? 128 : g->node_capacity * 2;
        if (capacity < g->node_capacity || capacity > UINT32_MAX ||
            capacity > SIZE_MAX / sizeof(*g->nodes))
            goto memory;
        qa_nav_node *nodes = realloc(g->nodes, capacity * sizeof(*nodes));
        if (nodes == NULL)
            goto memory;
        g->nodes = nodes;
        g->node_capacity = capacity;
    }
    g->nodes[n] = *node;
    ++g->view.node_count;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, n, "Growing navigation nodes");
    return false;
}
bool nav_graph_edge(qa_nav_graph *g, const qa_nav_edge *edge, qa_error *e) {
    size_t n = g->view.edge_count;
    if (n == UINT32_MAX)
        goto memory;
    if (n == g->edge_capacity) {
        size_t capacity = g->edge_capacity == 0 ? 512 : g->edge_capacity * 2;
        if (capacity < g->edge_capacity || capacity > UINT32_MAX ||
            capacity > SIZE_MAX / sizeof(*g->edges))
            goto memory;
        qa_nav_edge *edges = realloc(g->edges, capacity * sizeof(*edges));
        if (edges == NULL)
            goto memory;
        g->edges = edges;
        g->edge_capacity = capacity;
    }
    g->edges[n] = *edge;
    ++g->view.edge_count;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, n, "Growing navigation edges");
    return false;
}
static uint32_t *indices(size_t count, bool clear) {
    if (count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    return clear ? calloc(count == 0 ? 1 : count, sizeof(uint32_t))
                 : malloc((count == 0 ? 1 : count) * sizeof(uint32_t));
}
static bool mover_edge(const qa_nav_edge *edge) {
    return edge->mode == QA_NAV_MOVER && edge->has_entity;
}
bool nav_graph_finish(qa_nav_graph *g, qa_error *e) {
    const size_t n = g->view.node_count, m = g->view.edge_count;
    size_t max_node = 0, max_edge = 0, mover_count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (g->nodes[i].id == QA_NAV_NO_INDEX || !qa_vec_finite(g->nodes[i].origin) ||
            !qa_bounds_valid(g->nodes[i].bounds) || !isfinite(g->nodes[i].radius) ||
            g->nodes[i].radius < 0)
            goto invalid;
        if ((size_t)g->nodes[i].id + 1 > max_node)
            max_node = (size_t)g->nodes[i].id + 1;
    }
    for (size_t i = 0; i < m; ++i) {
        if (g->edges[i].id == QA_NAV_NO_INDEX ||
            (unsigned)g->edges[i].mode >= QA_NAV_TRAVEL_COUNT ||
            !qa_vec_finite(g->edges[i].start) || !qa_vec_finite(g->edges[i].end))
            goto invalid;
        if ((size_t)g->edges[i].id + 1 > max_edge)
            max_edge = (size_t)g->edges[i].id + 1;
        if (mover_edge(g->edges + i))
            ++mover_count;
    }
    g->node_lookup = indices(max_node, false);
    g->edge_lookup = indices(max_edge, false);
    g->first_out = indices(n + 1, true);
    g->outgoing = indices(m, false);
    g->first_mover_out = indices(n + 1, false);
    g->mover_outgoing = indices(mover_count, false);
    g->clusters = indices(n, false);
    if (g->node_lookup == NULL || g->edge_lookup == NULL || g->first_out == NULL ||
        g->outgoing == NULL || g->first_mover_out == NULL || g->mover_outgoing == NULL ||
        g->clusters == NULL)
        goto memory;
    g->node_lookup_count = max_node;
    g->edge_lookup_count = max_edge;
    for (size_t i = 0; i < max_node; ++i)
        g->node_lookup[i] = QA_NAV_NO_INDEX;
    for (size_t i = 0; i < max_edge; ++i)
        g->edge_lookup[i] = QA_NAV_NO_INDEX;
    for (size_t i = 0; i < n; ++i) {
        if (g->node_lookup[g->nodes[i].id] != QA_NAV_NO_INDEX)
            goto invalid;
        g->node_lookup[g->nodes[i].id] = (uint32_t)i;
    }
    for (size_t i = 0; i < m; ++i) {
        const qa_nav_edge *edge = g->edges + i;
        uint32_t from = nav_node_index(g, edge->from), to = nav_node_index(g, edge->to);
        if (from == QA_NAV_NO_INDEX || to == QA_NAV_NO_INDEX ||
            g->edge_lookup[edge->id] != QA_NAV_NO_INDEX || !isfinite(edge->travel_seconds) ||
            edge->travel_seconds < 0)
            goto invalid;
        g->edge_lookup[edge->id] = (uint32_t)i;
        ++g->first_out[from + 1];
    }
    uint32_t *first_in = indices(n + 1, true), *incoming = indices(m, false),
             *cursor = indices(n, false);
    uint32_t *stack = indices(n, false), *next = indices(n, false), *order = indices(n, false);
    uint8_t *seen = calloc(n == 0 ? 1 : n, 1);
    if (first_in == NULL || incoming == NULL || cursor == NULL || stack == NULL || next == NULL ||
        order == NULL || seen == NULL) {
        free(first_in);
        free(incoming);
        free(cursor);
        free(stack);
        free(next);
        free(order);
        free(seen);
        goto memory;
    }
    for (size_t i = 0; i < m; ++i)
        ++first_in[nav_node_index(g, g->edges[i].to) + 1];
    for (size_t i = 1; i <= n; ++i) {
        g->first_out[i] += g->first_out[i - 1];
        first_in[i] += first_in[i - 1];
    }
    memcpy(cursor, g->first_out, n * sizeof(*cursor));
    for (size_t i = 0; i < m; ++i)
        g->outgoing[cursor[nav_node_index(g, g->edges[i].from)]++] = (uint32_t)i;
    uint32_t mover = 0;
    for (size_t node = 0; node < n; ++node) {
        g->first_mover_out[node] = mover;
        for (uint32_t i = g->first_out[node]; i < g->first_out[node + 1]; ++i) {
            uint32_t edge = g->outgoing[i];
            if (mover_edge(g->edges + edge))
                g->mover_outgoing[mover++] = edge;
        }
    }
    g->first_mover_out[n] = mover;
    memcpy(cursor, first_in, n * sizeof(*cursor));
    for (size_t i = 0; i < m; ++i)
        incoming[cursor[nav_node_index(g, g->edges[i].to)]++] = (uint32_t)i;
    size_t ordered = 0;
    for (uint32_t start = 0; start < n; ++start) {
        if (seen[start])
            continue;
        size_t depth = 1;
        stack[0] = start;
        next[0] = g->first_out[start];
        seen[start] = 1;
        while (depth != 0) {
            uint32_t node = stack[depth - 1];
            if (next[depth - 1] == g->first_out[node + 1]) {
                order[ordered++] = node;
                --depth;
                continue;
            }
            uint32_t child = nav_node_index(g, g->edges[g->outgoing[next[depth - 1]++]].to);
            if (!seen[child]) {
                seen[child] = 1;
                stack[depth] = child;
                next[depth] = g->first_out[child];
                ++depth;
            }
        }
    }
    memset(seen, 0, n);
    uint32_t cluster = 0;
    while (ordered != 0) {
        uint32_t start = order[--ordered];
        if (seen[start])
            continue;
        size_t used = 1;
        stack[0] = start;
        seen[start] = 1;
        while (used != 0) {
            uint32_t node = stack[--used];
            g->clusters[node] = cluster;
            for (uint32_t i = first_in[node]; i < first_in[node + 1]; ++i) {
                uint32_t child = nav_node_index(g, g->edges[incoming[i]].from);
                if (!seen[child]) {
                    seen[child] = 1;
                    stack[used++] = child;
                }
            }
        }
        ++cluster;
    }
    g->first_in = first_in;
    g->incoming = incoming;
    free(cursor);
    free(stack);
    free(next);
    free(order);
    free(seen);
    g->view.nodes = g->nodes;
    g->view.edges = g->edges;
    g->view.clusters = g->clusters;
    g->view.cluster_count = cluster;
    return nav_aas_estimate_topology(g, e);
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid or duplicate navigation graph identity");
    return false;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation adjacency");
    return false;
}
void qa_nav_graph_retain(qa_nav_graph *g) {
    if (g != NULL)
        atomic_fetch_add_explicit(&g->references, 1, memory_order_relaxed);
}
void qa_nav_graph_release(qa_nav_graph *g) {
    if (g != NULL && atomic_fetch_sub_explicit(&g->references, 1, memory_order_acq_rel) == 1) {
        qa_nav_asset_release((qa_nav_asset *)g->view.asset);
        free(g->nodes);
        free(g->edges);
        free(g->node_lookup);
        free(g->edge_lookup);
        free(g->outgoing);
        free(g->first_out);
        free(g->mover_outgoing);
        free(g->first_mover_out);
        free(g->incoming);
        free(g->first_in);
        free(g->clusters);
        free(g->rejected);
        free(g->portal_maxima);
        free(g);
    }
}
const qa_nav_graph_view *qa_nav_graph_read(const qa_nav_graph *g) {
    return g == NULL ? NULL : &g->view;
}
