#include "internal.h"

typedef enum estimate_kind { ESTIMATE_GRAPH, ESTIMATE_AREA, ESTIMATE_PORTAL } estimate_kind;
struct nav_estimate_cache {
    nav_estimate_cache *next;
    estimate_kind kind;
    uint32_t cluster, goal, flags;
    size_t count, bytes;
    float *seconds;
    uint32_t *first;
    uint16_t *times;
    uint8_t *reaches;
};
typedef struct estimate_update {
    uint32_t area, cluster, row;
    uint16_t time;
    bool queued;
} estimate_update;
typedef struct estimate_queue {
    uint32_t *values;
    size_t capacity, first, count;
} estimate_queue;

bool qa_navigation_aas_area_time(const qa_aas_setting *s, qa_vec3 start, qa_vec3 end, uint16_t *out,
                                 qa_error *e) {
    if (s == NULL || out == NULL || !qa_vec_finite(start) || !qa_vec_finite(end)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid AAS area travel-time query");
        return false;
    }
    float value = nav_distance(start, end) * ((s->presence & 2) == 0 ? 1.3f
                                              : (s->flags & 4) != 0  ? 1
                                                                     : 0.33f);
    if (!isfinite(value) || value >= 2147483648.0f) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "AAS travel distance exceeds source integer range");
        return false;
    }
    *out = (uint16_t)(value < 1 ? 1 : (int32_t)value);
    return true;
}
static bool incoming_source(const qa_aas_view *aas, const qa_nav_edge *edge) {
    const qa_aas_setting *s = aas->settings + edge->from;
    return edge->id >= (uint32_t)s->first_reach && edge->id - (uint32_t)s->first_reach < 128;
}
bool nav_aas_estimate_topology(qa_nav_graph *g, qa_error *e) {
    const qa_aas_view *aas = qa_nav_asset_aas(g->view.asset);
    if (aas == NULL)
        return true;
    size_t count = aas->count[QA_AAS_PORTALS];
    g->portal_maxima = calloc(count == 0 ? 1 : count, sizeof(*g->portal_maxima));
    if (g->portal_maxima == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared AAS portal travel times");
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t area = (uint32_t)aas->portals[i].area, index = nav_node_index(g, area);
        if (index == QA_NAV_NO_INDEX)
            continue;
        const qa_aas_setting *s = aas->settings + area;
        for (int32_t j = 0; j < s->reach_count; ++j) {
            const qa_aas_reach *reach = aas->reachability + s->first_reach + j;
            for (uint32_t k = g->first_in[index]; k < g->first_in[index + 1]; ++k) {
                const qa_nav_edge *incoming = g->edges + g->incoming[k];
                uint16_t time;
                if (!incoming_source(aas, incoming))
                    continue;
                if (!qa_navigation_aas_area_time(s, incoming->end, reach->start, &time, e))
                    return false;
                if (time > g->portal_maxima[i])
                    g->portal_maxima[i] = time;
            }
        }
    }
    return true;
}
static void cache_free(nav_estimate_cache *c) {
    free(c->seconds);
    free(c->first);
    free(c->times);
    free(c->reaches);
    free(c);
}
void nav_estimates_free(qa_navigation *n) {
    while (n->estimates != NULL) {
        nav_estimate_cache *next = n->estimates->next;
        cache_free(n->estimates);
        n->estimates = next;
    }
}
static void prune(qa_navigation *n) {
    size_t bytes = 0, count = 0;
    nav_estimate_cache **at = &n->estimates;
    while (*at != NULL) {
        nav_estimate_cache *c = *at;
        bytes += c->bytes;
        ++count;
        if (count > 1 && (bytes > 16u * 1024u * 1024u || count > 128)) {
            *at = c->next;
            cache_free(c);
        } else
            at = &c->next;
    }
}
static nav_estimate_cache *cached(qa_navigation *n, estimate_kind kind, uint32_t cluster,
                                  uint32_t goal, uint32_t flags) {
    nav_estimate_cache **at = &n->estimates;
    while (*at != NULL) {
        nav_estimate_cache *c = *at;
        if (c->kind == kind && c->cluster == cluster && c->goal == goal && c->flags == flags) {
            *at = c->next;
            c->next = n->estimates;
            n->estimates = c;
            return c;
        }
        at = &c->next;
    }
    return NULL;
}
static nav_estimate_cache *cache_new(estimate_kind kind, uint32_t cluster, uint32_t goal,
                                     uint32_t flags, size_t count, qa_error *e) {
    size_t stride = kind == ESTIMATE_GRAPH ? sizeof(float) + sizeof(uint32_t)
                                           : sizeof(uint16_t) + sizeof(uint8_t);
    if (count > (SIZE_MAX - sizeof(nav_estimate_cache)) / stride)
        goto memory;
    nav_estimate_cache *c = calloc(1, sizeof(*c));
    if (c == NULL)
        goto memory;
    c->kind = kind;
    c->cluster = cluster;
    c->goal = goal;
    c->flags = flags;
    c->count = count;
    c->bytes = sizeof(*c) + count * stride;
    if (kind == ESTIMATE_GRAPH) {
        c->seconds = malloc((count == 0 ? 1 : count) * sizeof(*c->seconds));
        c->first = malloc((count == 0 ? 1 : count) * sizeof(*c->first));
        if (c->seconds == NULL || c->first == NULL) {
            cache_free(c);
            goto memory;
        }
        for (size_t i = 0; i < count; ++i) {
            c->seconds[i] = INFINITY;
            c->first[i] = QA_NAV_NO_INDEX;
        }
    } else {
        c->times = calloc(count == 0 ? 1 : count, sizeof(*c->times));
        c->reaches = calloc(count == 0 ? 1 : count, sizeof(*c->reaches));
        if (c->times == NULL || c->reaches == NULL) {
            cache_free(c);
            goto memory;
        }
    }
    return c;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating retained navigation estimate cache");
    return NULL;
}
static nav_estimate_cache *remember(qa_navigation *n, nav_estimate_cache *c) {
    c->next = n->estimates;
    n->estimates = c;
    return c;
}
static bool graph_cache(qa_navigation *n, qa_nav_workspace *w, uint32_t goal, uint32_t flags,
                        nav_estimate_cache **out, qa_error *e) {
    nav_estimate_cache *c = cached(n, ESTIMATE_GRAPH, 0, goal, flags);
    if (c != NULL) {
        *out = c;
        return true;
    }
    const qa_nav_graph *g = n->graph;
    c = cache_new(ESTIMATE_GRAPH, 0, goal, flags, g->view.node_count, e);
    if (c == NULL)
        return false;
    uint32_t index = nav_node_index(g, goal);
    c->seconds[index] = 0;
    w->queue_count = 0;
    if (!nav_queue_push(w, (nav_queue_entry){index, 0}, e)) {
        cache_free(c);
        return false;
    }
    qa_nav_route_query q = {.has_travel_flags = true, .travel_flags = flags};
    nav_queue_entry current;
    while (nav_queue_pop(w, &current)) {
        if (c->seconds[current.node] != current.cost)
            continue;
        for (uint32_t i = g->first_in[current.node]; i < g->first_in[current.node + 1]; ++i) {
            uint32_t edge_index = g->incoming[i];
            const qa_nav_edge *edge = g->edges + edge_index;
            if (!nav_static_edge(n, edge_index, &q))
                continue;
            uint32_t from = nav_node_index(g, edge->from);
            float seconds = current.cost + edge->travel_seconds;
            if (seconds >= c->seconds[from])
                continue;
            c->seconds[from] = seconds;
            c->first[from] = edge->id;
            if (!nav_queue_push(w, (nav_queue_entry){from, seconds}, e)) {
                cache_free(c);
                return false;
            }
        }
    }
    *out = remember(n, c);
    return true;
}
static uint32_t cluster_area(const qa_aas_view *aas, uint32_t cluster, uint32_t area) {
    if (area >= aas->count[QA_AAS_SETTINGS] || cluster >= aas->count[QA_AAS_CLUSTERS])
        return QA_NAV_NO_INDEX;
    const qa_aas_setting *s = aas->settings + area;
    if (s->cluster > 0)
        return s->cluster_area < 0 ? QA_NAV_NO_INDEX : (uint32_t)s->cluster_area;
    uint32_t portal = (uint32_t)(-(int64_t)s->cluster);
    if (portal >= aas->count[QA_AAS_PORTALS])
        return QA_NAV_NO_INDEX;
    int32_t index =
        aas->portals[portal]
            .cluster_areas[aas->portals[portal].front_cluster == (int32_t)cluster ? 0 : 1];
    return index < 0 ? QA_NAV_NO_INDEX : (uint32_t)index;
}
static bool update_storage(size_t count, estimate_update **updates, estimate_queue *queue,
                           qa_error *e) {
    if (count == 0)
        count = 1;
    if (count > SIZE_MAX / sizeof(estimate_update) || count > SIZE_MAX / sizeof(uint32_t))
        goto memory;
    *updates = calloc(count, sizeof(**updates));
    queue->values = malloc(count * sizeof(*queue->values));
    queue->capacity = count;
    if (*updates == NULL || queue->values == NULL) {
        free(*updates);
        free(queue->values);
        goto memory;
    }
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating AAS cache relaxation workspace");
    return false;
}
static void update_enqueue(estimate_queue *q, estimate_update *updates, uint32_t index) {
    if (updates[index].queued)
        return;
    updates[index].queued = true;
    q->values[(q->first + q->count) % q->capacity] = index;
    ++q->count;
}
static bool update_pop(estimate_queue *q, estimate_update *updates, estimate_update **out) {
    if (q->count == 0)
        return false;
    *out = updates + q->values[q->first];
    q->first = (q->first + 1) % q->capacity;
    --q->count;
    (*out)->queued = false;
    return true;
}
static bool area_cache(qa_navigation *n, uint32_t cluster, uint32_t goal, uint32_t flags,
                       nav_estimate_cache **out, qa_error *e) {
    nav_estimate_cache *c = cached(n, ESTIMATE_AREA, cluster, goal, flags);
    if (c != NULL) {
        *out = c;
        return true;
    }
    const qa_nav_graph *g = n->graph;
    const qa_aas_view *aas = qa_nav_asset_aas(g->view.asset);
    if (cluster >= aas->count[QA_AAS_CLUSTERS] || aas->clusters[cluster].reachable_area_count < 0) {
        qa_error_set(e, QA_ERROR_FORMAT, cluster, "Invalid AAS estimate cluster");
        return false;
    }
    size_t count = (size_t)aas->clusters[cluster].reachable_area_count;
    c = cache_new(ESTIMATE_AREA, cluster, goal, flags, count, e);
    if (c == NULL)
        return false;
    uint32_t first = cluster_area(aas, cluster, goal);
    if (first >= count) {
        *out = remember(n, c);
        return true;
    }
    estimate_update *updates;
    estimate_queue queue = {0};
    if (!update_storage(count, &updates, &queue, e)) {
        cache_free(c);
        return false;
    }
    updates[first] = (estimate_update){.area = goal, .time = 1, .row = QA_NAV_NO_INDEX};
    c->times[first] = 1;
    update_enqueue(&queue, updates, first);
    qa_nav_route_query q = {.has_travel_flags = true, .travel_flags = flags};
    estimate_update *current;
    bool ok = true;
    while (ok && update_pop(&queue, updates, &current)) {
        uint32_t area_index = nav_node_index(g, current->area);
        if (area_index == QA_NAV_NO_INDEX)
            continue;
        for (uint32_t k = g->first_in[area_index + 1]; ok && k > g->first_in[area_index];) {
            uint32_t edge_index = g->incoming[--k];
            const qa_nav_edge *edge = g->edges + edge_index;
            if (!incoming_source(aas, edge) ||
                (qa_nav_aas_travel_flag(edge->source_travel_type) & ~flags) != 0 ||
                !nav_static_edge(n, edge_index, &q) ||
                (qa_nav_area_travel_flags(aas->settings + edge->to) & ~flags) != 0)
                continue;
            const qa_aas_setting *s = aas->settings + edge->from;
            if (s->cluster > 0 && s->cluster != (int32_t)cluster)
                continue;
            uint32_t index = cluster_area(aas, cluster, edge->from);
            if (index >= count)
                continue;
            uint16_t crossing = 0;
            if (current->row != QA_NAV_NO_INDEX &&
                !qa_navigation_aas_area_time(aas->settings + current->area, edge->end,
                                             aas->reachability[current->row].start, &crossing, e)) {
                ok = false;
                break;
            }
            uint16_t time =
                (uint16_t)(current->time + crossing + aas->reachability[edge->id].travel_time);
            if (c->times[index] != 0 && c->times[index] <= time)
                continue;
            c->times[index] = time;
            c->reaches[index] = (uint8_t)(edge->id - (uint32_t)s->first_reach);
            updates[index].area = edge->from;
            updates[index].time = time;
            updates[index].row = edge->id;
            update_enqueue(&queue, updates, index);
        }
    }
    free(updates);
    free(queue.values);
    if (!ok) {
        cache_free(c);
        return false;
    }
    *out = remember(n, c);
    return true;
}
static bool portal_cache(qa_navigation *n, uint32_t cluster, uint32_t goal, uint32_t flags,
                         nav_estimate_cache **out, qa_error *e) {
    nav_estimate_cache *c = cached(n, ESTIMATE_PORTAL, 0, goal, flags);
    if (c != NULL) {
        *out = c;
        return true;
    }
    const qa_nav_graph *g = n->graph;
    const qa_aas_view *aas = qa_nav_asset_aas(g->view.asset);
    size_t count = aas->count[QA_AAS_PORTALS];
    c = cache_new(ESTIMATE_PORTAL, 0, goal, flags, count, e);
    if (c == NULL)
        return false;
    estimate_update *updates;
    estimate_queue queue = {0};
    if (!update_storage(count + 1, &updates, &queue, e)) {
        cache_free(c);
        return false;
    }
    updates[count] = (estimate_update){.area = goal, .cluster = cluster, .time = 1};
    int32_t goal_cluster = aas->settings[goal].cluster;
    if (goal_cluster < 0 && (uint64_t)(-(int64_t)goal_cluster) < count)
        c->times[-goal_cluster] = 1;
    update_enqueue(&queue, updates, (uint32_t)count);
    estimate_update *current;
    bool ok = true;
    while (ok && update_pop(&queue, updates, &current)) {
        if (current->cluster >= aas->count[QA_AAS_CLUSTERS]) {
            qa_error_set(e, QA_ERROR_FORMAT, current->cluster, "Invalid AAS portal cluster");
            ok = false;
            break;
        }
        const qa_aas_cluster *record = aas->clusters + current->cluster;
        nav_estimate_cache *local;
        if (!area_cache(n, current->cluster, current->area, flags, &local, e)) {
            ok = false;
            break;
        }
        for (int32_t i = 0; i < record->portal_count; ++i) {
            uint32_t number = (uint32_t)aas->portal_index[record->first_portal + i];
            const qa_aas_portal *portal = aas->portals + number;
            if ((uint32_t)portal->area == current->area)
                continue;
            uint32_t index = cluster_area(aas, current->cluster, (uint32_t)portal->area);
            if (index >= local->count || local->times[index] == 0)
                continue;
            uint16_t time = (uint16_t)(local->times[index] + current->time);
            if (c->times[number] != 0 && c->times[number] <= time)
                continue;
            c->times[number] = time;
            updates[number].cluster = (uint32_t)(portal->front_cluster == (int32_t)current->cluster
                                                     ? portal->back_cluster
                                                     : portal->front_cluster);
            updates[number].area = (uint32_t)portal->area;
            updates[number].time = (uint16_t)(time + g->portal_maxima[number]);
            update_enqueue(&queue, updates, number);
        }
    }
    free(updates);
    free(queue.values);
    if (!ok) {
        cache_free(c);
        return false;
    }
    *out = remember(n, c);
    return true;
}
static bool aas_estimate(qa_navigation *n, uint32_t area, uint32_t goal, const qa_vec3 *origin,
                         uint32_t flags, qa_nav_estimate *out, qa_error *e) {
    const qa_nav_graph *g = n->graph;
    const qa_aas_view *aas = qa_nav_asset_aas(g->view.asset);
    const qa_aas_setting *start = aas->settings + area, *end = aas->settings + goal;
    if (((start->contents | end->contents) & 256) != 0)
        flags |= UINT32_C(0x00800000);
    int32_t cluster = start->cluster, goal_cluster = end->cluster;
    if (cluster < 0 && goal_cluster > 0) {
        const qa_aas_portal *p = aas->portals - cluster;
        if (p->front_cluster == goal_cluster || p->back_cluster == goal_cluster)
            cluster = goal_cluster;
    } else if (cluster > 0 && goal_cluster < 0) {
        const qa_aas_portal *p = aas->portals - goal_cluster;
        if (p->front_cluster == cluster || p->back_cluster == cluster)
            goal_cluster = cluster;
    }
    if (cluster > 0 && cluster == goal_cluster) {
        nav_estimate_cache *c;
        if (!area_cache(n, (uint32_t)cluster, goal, flags, &c, e))
            return false;
        uint32_t index = cluster_area(aas, (uint32_t)cluster, area);
        if (index >= c->count)
            return true;
        if (c->times[index] != 0) {
            out->found = true;
            out->travel_time = c->times[index];
            if (origin != NULL) {
                uint32_t reach = (uint32_t)start->first_reach + c->reaches[index];
                uint16_t crossing;
                if (reach >= aas->count[QA_AAS_REACHABILITY]) {
                    qa_error_set(e, QA_ERROR_FORMAT, reach, "AAS cached reach is out of bounds");
                    return false;
                }
                if (!qa_navigation_aas_area_time(start, *origin, aas->reachability[reach].start,
                                                 &crossing, e))
                    return false;
                out->travel_time += crossing;
                if (nav_edge_index(g, reach) != QA_NAV_NO_INDEX)
                    out->first_edge = reach;
            }
            return true;
        }
    }
    cluster = start->cluster;
    goal_cluster = end->cluster;
    if (goal_cluster < 0)
        goal_cluster = aas->portals[-goal_cluster].front_cluster;
    if (cluster == 0 || goal_cluster <= 0)
        return true;
    nav_estimate_cache *portals;
    if (!portal_cache(n, (uint32_t)goal_cluster, goal, flags, &portals, e))
        return false;
    if (cluster < 0) {
        uint32_t index = (uint32_t)-cluster;
        out->found = true;
        out->travel_time = portals->times[index];
        uint32_t reach = (uint32_t)start->first_reach + portals->reaches[index];
        if (origin != NULL && nav_edge_index(g, reach) != QA_NAV_NO_INDEX)
            out->first_edge = reach;
        return true;
    }
    if ((size_t)cluster >= aas->count[QA_AAS_CLUSTERS])
        return true;
    const qa_aas_cluster *record = aas->clusters + cluster;
    uint16_t best = 0;
    for (int32_t i = 0; i < record->portal_count; ++i) {
        uint32_t number = (uint32_t)aas->portal_index[record->first_portal + i];
        if (portals->times[number] == 0)
            continue;
        const qa_aas_portal *portal = aas->portals + number;
        nav_estimate_cache *local;
        if (!area_cache(n, (uint32_t)cluster, (uint32_t)portal->area, flags, &local, e))
            return false;
        uint32_t index = cluster_area(aas, (uint32_t)cluster, area);
        if (index >= local->count || local->times[index] == 0)
            continue;
        uint32_t reach =
            origin == NULL ? QA_NAV_NO_INDEX : (uint32_t)start->first_reach + local->reaches[index];
        uint16_t time =
            (uint16_t)(portals->times[number] + local->times[index] + g->portal_maxima[number]);
        if (origin != NULL) {
            uint16_t crossing;
            if (reach >= aas->count[QA_AAS_REACHABILITY]) {
                qa_error_set(e, QA_ERROR_FORMAT, reach, "AAS cached reach is out of bounds");
                return false;
            }
            if (!qa_navigation_aas_area_time(start, *origin, aas->reachability[reach].start,
                                             &crossing, e))
                return false;
            time = (uint16_t)(time + crossing);
        }
        if (best == 0 || time < best) {
            best = time;
            out->found = true;
            out->travel_time = time;
            out->first_edge = nav_edge_index(g, reach) == QA_NAV_NO_INDEX ? QA_NAV_NO_INDEX : reach;
        }
    }
    return true;
}
bool qa_navigation_estimate(qa_navigation *n, qa_nav_workspace *w, uint32_t start, uint32_t goal,
                            const qa_vec3 *origin, uint32_t flags, qa_nav_estimate *out,
                            qa_error *e) {
    if (n == NULL || w == NULL || out == NULL || (origin != NULL && !qa_vec_finite(*origin))) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid navigation estimate query");
        return false;
    }
    *out = (qa_nav_estimate){.first_edge = QA_NAV_NO_INDEX};
    uint32_t index = nav_node_index(n->graph, start);
    if (index == QA_NAV_NO_INDEX || nav_node_index(n->graph, goal) == QA_NAV_NO_INDEX)
        return true;
    if (start == goal) {
        out->found = true;
        out->travel_time = 1;
        return true;
    }
    if (qa_nav_asset_aas(n->graph->view.asset) != NULL) {
        bool ok = aas_estimate(n, start, goal, origin, flags, out, e);
        prune(n);
        return ok;
    }
    nav_estimate_cache *c;
    if (!graph_cache(n, w, goal, flags, &c, e))
        return false;
    float seconds = c->seconds[index];
    if (isfinite(seconds)) {
        if (origin != NULL && c->first[index] != QA_NAV_NO_INDEX) {
            const qa_nav_source_view *asset = qa_nav_asset_kex(n->graph->view.asset);
            seconds += nav_distance(*origin, qa_navigation_edge(n, c->first[index])->start) *
                       (asset == NULL ? 1 : asset->heuristic) / 320;
        }
        float time = seconds * 100;
        if (!isfinite(time) || time >= 4294967296.0f) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Navigation estimate exceeds its time range");
            prune(n);
            return false;
        }
        out->found = true;
        out->travel_time = time < 1 ? 1 : (uint32_t)time;
        out->first_edge = origin == NULL ? QA_NAV_NO_INDEX : c->first[index];
    }
    prune(n);
    return true;
}
