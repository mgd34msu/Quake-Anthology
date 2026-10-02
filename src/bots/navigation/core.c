#include "internal.h"

bool bot_nav_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool bot_nav_scratch(qa_bot_navigation *n, size_t count, qa_error *e) {
    if (count <= n->capacity)
        return true;
    if (count > SIZE_MAX / sizeof(qa_nav_adjacency_cursor))
        return bot_nav_fail(e, "bot navigation workspace exceeds address range");
    uint32_t *areas = malloc(count * sizeof(*areas)), *stack = malloc(count * sizeof(*stack));
    uint32_t *starts = malloc(count * sizeof(*starts)), *goals = malloc(count * sizeof(*goals));
    uint32_t *next = malloc(count * sizeof(*next));
    uint8_t *visited = malloc(count);
    qa_nav_adjacency_cursor *adjacency = malloc(count * sizeof(*adjacency));
    if (!areas || !stack || !starts || !goals || !visited || !next || !adjacency) {
        free(areas); free(stack); free(starts); free(goals); free(visited); free(next);
        free(adjacency);
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating bot navigation query workspace");
        return false;
    }
    free(n->areas); free(n->stack); free(n->start_times); free(n->goal_times); free(n->visited);
    free(n->next_neighbor);
    free(n->adjacency);
    n->areas = areas;
    n->stack = stack;
    n->start_times = starts;
    n->goal_times = goals;
    n->visited = visited;
    n->next_neighbor = next;
    n->adjacency = adjacency;
    n->capacity = count;
    return true;
}
bool qa_bot_navigation_create(qa_navigation *runtime, qa_world *world, qa_actor_id actor,
                              const qa_bot_navigation_observations *observations,
                              qa_bot_navigation **out, qa_error *e) {
    if (!runtime || !world || !out)
        return bot_nav_fail(e, "missing bot navigation runtime/world/output");
    qa_bot_navigation *n = calloc(1, sizeof(*n));
    if (!n) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot navigation owner");
        return false;
    }
    n->world = world;
    if (observations) n->observations = *observations;
    if (!qa_nav_workspace_create(&n->workspace, e) ||
        !qa_bot_navigation_bind(n, runtime, actor, e)) {
        qa_bot_navigation_destroy(n);
        return false;
    }
    *out = n;
    return true;
}
void qa_bot_navigation_destroy(qa_bot_navigation *n) {
    if (!n) return;
    qa_nav_workspace_destroy(n->workspace);
    qa_nav_route_free(&n->trajectory);
    qa_nav_prediction_result_free(&n->prediction);
    free(n->crossings);
    free(n->areas); free(n->stack); free(n->start_times); free(n->goal_times); free(n->visited);
    free(n->next_neighbor);
    free(n->adjacency);
    free(n);
}
bool qa_bot_navigation_create_restored(qa_navigation *runtime, qa_world *world, qa_actor_id actor,
    const qa_bot_navigation_observations *observations, qa_bot_navigation **out, qa_error *e) {
    qa_saved_actor_id saved;
    if (!world || !out || *out || (actor.registry &&
        !qa_actors_save_reference(qa_world_actors(world),actor,&saved,e))) return false;
    qa_bot_navigation *n=NULL;
    if (!qa_bot_navigation_create(runtime,world,(qa_actor_id){0},observations,&n,e)) return false;
    n->actor=actor; *out=n; return true;
}
bool qa_bot_navigation_bind(qa_bot_navigation *n, qa_navigation *runtime, qa_actor_id actor,
                            qa_error *e) {
    const qa_nav_graph_view *graph = qa_navigation_graph(runtime);
    if (!n || !graph ||
        (actor.registry && !qa_actors_get(qa_world_actors(n->world), actor)))
        return bot_nav_fail(e, "invalid selected bot navigation owner");
    if (n->runtime == runtime) {
        n->actor = actor;
        return true;
    }
    uint32_t offset = qa_nav_asset_aas(graph->asset) ? 0 : 1;
    for (size_t i = 0; i < graph->node_count; ++i)
        if (graph->nodes[i].id > INT32_MAX - offset)
            return bot_nav_fail(e, "navigation area exceeds source signed range");
    for (size_t i = 0; i < graph->edge_count; ++i)
        if (graph->edges[i].id > INT32_MAX - offset)
            return bot_nav_fail(e, "navigation reachability exceeds source signed range");
    if (!bot_nav_scratch(n, graph->node_count, e))
        return false;
    n->runtime = runtime;
    n->actor = actor;
    n->offset = offset;
    return true;
}
qa_navigation *qa_bot_navigation_runtime(const qa_bot_navigation *n) { return n ? n->runtime : NULL; }
qa_actor_id qa_bot_navigation_actor(const qa_bot_navigation *n) { return n ? n->actor : (qa_actor_id){0}; }
uint32_t qa_bot_navigation_source_area(const qa_bot_navigation *n, uint32_t id) {
    return n && id != QA_NAV_NO_INDEX ? id + n->offset : 0;
}
uint32_t qa_bot_navigation_node(const qa_bot_navigation *n, uint32_t area) {
    return n && area ? area - n->offset : QA_NAV_NO_INDEX;
}
const qa_nav_edge *qa_bot_navigation_reachability(const qa_bot_navigation *n, uint32_t reach) {
    return n && reach ? qa_navigation_edge(n->runtime, reach - n->offset) : NULL;
}
qa_bot_nav_area qa_bot_navigation_area(const qa_bot_navigation *n, uint32_t area) {
    const qa_nav_node *node = n && area ?
        qa_navigation_node(n->runtime, qa_bot_navigation_node(n, area)) : NULL;
    if (!node)
        return (qa_bot_nav_area){0};
    const qa_aas_view *aas = qa_nav_asset_aas(qa_navigation_graph(n->runtime)->asset);
    const qa_aas_setting *s = aas && node->source.kind == QA_NAV_ORIGIN_AAS ?
        &aas->settings[node->id] : NULL;
    return (qa_bot_nav_area){
        .contents = s ? (uint32_t)s->contents :
            (node->contents & 1) | ((node->contents & 4) >> 1) | ((node->contents & 2) << 1),
        .flags = s ? (uint32_t)s->flags : node->flags,
        .presence = s ? (uint32_t)s->presence : node->presence,
        .cluster = s ? s->cluster : node->source_cluster,
        .reach_count = qa_navigation_outgoing_count(n->runtime, node->id)};
}
qa_bounds qa_bot_navigation_presence(const qa_bot_navigation *n, uint32_t presence) {
    const qa_nav_graph_view *g = n ? qa_navigation_graph(n->runtime) : NULL;
    if (!g) return (qa_bounds){0};
    return presence == 4 && g->profile.has_crouched_shape ?
        g->profile.crouched_shape.bounds : g->profile.shape.bounds;
}
bool qa_bot_navigation_point(qa_bot_navigation *n, qa_vec3 origin, uint32_t *out, qa_error *e) {
    if (!n || !out) return bot_nav_fail(e, "missing bot point-area output");
    uint32_t node; bool found;
    if (!qa_navigation_area(n->runtime, n->actor, origin, &node, &found, e)) return false;
    *out = found ? qa_bot_navigation_source_area(n, node) : 0;
    return true;
}
bool qa_bot_navigation_trace_areas(qa_bot_navigation *n, qa_vec3 start, qa_vec3 end,
                                  qa_aas_crossing *out, size_t capacity, size_t *count, qa_error *e) {
    if (!n) return bot_nav_fail(e, "missing bot navigation owner");
    if (!qa_navigation_trace_areas(n->runtime, n->workspace, start, end, out, capacity, count, e))
        return false;
    for (size_t i = 0; i < *count; ++i) out[i].area = qa_bot_navigation_source_area(n, out[i].area);
    return true;
}
bool qa_bot_navigation_trace_collect(qa_bot_navigation *n, qa_vec3 start, qa_vec3 end,
                                     size_t maximum, qa_nav_crossings *out, qa_error *e) {
    if (out) out->count = 0;
    if (!n) return bot_nav_fail(e, "missing bot navigation owner");
    if (!qa_navigation_trace_collect(n->runtime, n->workspace, start, end, maximum, out, e))
        return false;
    for (size_t i = 0; i < out->count; ++i)
        out->data[i].area = qa_bot_navigation_source_area(n, out->data[i].area);
    return true;
}
bool qa_bot_navigation_bbox_areas(qa_bot_navigation *n, qa_bounds bounds, uint32_t *out,
                                 size_t capacity, size_t *count, qa_error *e) {
    if (!n) return bot_nav_fail(e, "missing bot navigation owner");
    if (!qa_navigation_bbox_areas(n->runtime, n->workspace, bounds, out, capacity, count, e))
        return false;
    for (size_t i = 0; i < *count; ++i) out[i] = qa_bot_navigation_source_area(n, out[i]);
    return true;
}
bool qa_bot_navigation_enable(qa_bot_navigation *n, uint32_t area, bool enabled,
                              bool *previous, qa_error *e) {
    if (!n || !previous) return bot_nav_fail(e, "invalid bot area admission");
    if (!area) { *previous = false; return true; }
    return qa_navigation_enable(n->runtime, qa_bot_navigation_node(n, area), enabled, previous, e);
}
bool qa_bot_navigation_trace(qa_bot_navigation *n, qa_vec3 start, qa_vec3 end,
                             const qa_bounds *bounds, qa_actor_id pass, uint32_t mask,
                             qa_trace_result *out, qa_error *e) {
    if (!n) return bot_nav_fail(e, "missing bot trace owner");
    qa_trace_query query = {.start = start, .end = end,
        .shape = {.kind = bounds ? QA_SHAPE_BOX : QA_SHAPE_POINT},
        .policy = {.family = QA_COLLISION_Q3, .contents_mask = mask, .q1_hull = -1,
                   .curves = true, .player_curve_clip = true}, .pass_actor = pass};
    if (bounds) query.shape.bounds = *bounds;
    return qa_world_trace(n->world, &query, out, e);
}
bool qa_bot_navigation_contents(qa_bot_navigation *n, qa_vec3 point, int32_t *out, qa_error *e) {
    if (!n || !out) return bot_nav_fail(e, "invalid bot point contents");
    qa_point_query q = {.point = point, .policy = {.family = QA_COLLISION_Q3, .q1_hull = -1}};
    qa_point_contents contents;
    if (!qa_world_point_contents(n->world, &q, &contents, e)) return false;
    *out = contents.contents;
    return true;
}
bool qa_bot_navigation_swimming(qa_bot_navigation *n, qa_vec3 point, bool *out, qa_error *e) {
    if (!out) return bot_nav_fail(e, "missing bot swimming output");
    point.z -= 2;
    int32_t contents;
    if (!qa_bot_navigation_contents(n, point, &contents, e)) return false;
    *out = (contents & (8 | 16 | 32)) != 0;
    return true;
}
bool qa_bot_navigation_selected_contents(qa_bot_navigation *n, qa_vec3 point,
                                         qa_point_contents *out, qa_error *e) {
    if (!n || !out) return bot_nav_fail(e, "invalid selected bot point contents");
    qa_point_query query = {.point = point,
        .policy = qa_navigation_graph(n->runtime)->profile.policy, .pass_actor = n->actor};
    return qa_world_point_contents(n->world, &query, out, e);
}
bool qa_bot_navigation_drop(qa_bot_navigation *n, qa_vec3 origin, qa_bounds bounds,
                            qa_vec3 *out, bool *success, qa_error *e) {
    if (!n || !out || !success) return bot_nav_fail(e, "missing bot floor output");
    qa_vec3 end = origin; end.z -= 100;
    qa_trace_result trace;
    qa_actor_id pass = n->observations.actor ?
        n->observations.actor(n->observations.context, 0) : (qa_actor_id){0};
    if (!qa_bot_navigation_trace(n, origin, end, &bounds, pass, 1, &trace, e))
        return false;
    *success = !trace.start_solid && !trace.all_solid;
    *out = *success ? trace.end : origin;
    return true;
}
bool qa_bot_navigation_best(qa_bot_navigation *n, qa_vec3 origin, qa_bounds bounds,
                            qa_vec3 *out, uint32_t *area, qa_error *e) {
    if (!n || !out || !area) return bot_nav_fail(e, "missing bot reachable-area output");
    qa_vec3 start = origin;
    if (!qa_bot_navigation_point(n, start, area, e)) return false;
    for (int i = 0; i < 5 && !*area; ++i)
        for (int j = 0; j < 5 && !*area; ++j)
            for (int k = -1; k <= 1 && !*area; ++k)
                for (int l = -1; l <= 1 && !*area; ++l) {
                    start = qa_v3(origin.x + (float)(j * 4 * k), origin.y + (float)(j * 4 * l),
                                  origin.z + (float)(i * 4));
                    if (!qa_bot_navigation_point(n, start, area, e)) return false;
                }
    if (*area) {
        qa_vec3 end = start; end.z -= 50; start.z += .25f;
        qa_bounds crouch = qa_bot_navigation_presence(n, 4);
        qa_trace_result trace;
        if (!qa_bot_navigation_trace(n, start, end, &crouch, (qa_actor_id){0}, 0x10001, &trace, e))
            return false;
        if (trace.start_solid || trace.all_solid) { *out = start; return true; }
        if (!qa_bot_navigation_point(n, trace.end, area, e)) return false;
        if (*area) { *out = trace.end; return true; }
    }
    size_t count;
    if (!qa_bot_navigation_bbox_areas(n, qa_bounds_translate(bounds, origin), n->areas,
                                      n->capacity, &count, e)) return false;
    *out = origin;
    *area = count ? n->areas[0] : 0;
    for (size_t i = 0; i < count; ++i)
        if (qa_bot_navigation_area(n, n->areas[i]).flags & 5) {
            *area = n->areas[i];
            break;
        }
    return true;
}
bool qa_bot_navigation_route(qa_bot_navigation *n, const qa_bot_nav_route_query *query,
                             qa_bot_nav_route *out, qa_error *e) {
    if (!n || !query || !out) return bot_nav_fail(e, "invalid bot route query");
    if (!query->area || !query->goal_area) { *out = (qa_bot_nav_route){0}; return true; }
    qa_nav_estimate estimate;
    if (!qa_navigation_estimate(n->runtime, n->workspace,
        qa_bot_navigation_node(n, query->area), qa_bot_navigation_node(n, query->goal_area),
        query->has_origin ? &query->origin : NULL, query->travel_flags, &estimate, e)) return false;
    *out = (qa_bot_nav_route){.found = estimate.found, .travel_time = estimate.travel_time,
        .next_reachability = estimate.found && estimate.first_edge != QA_NAV_NO_INDEX ?
            estimate.first_edge + n->offset : 0};
    return true;
}
