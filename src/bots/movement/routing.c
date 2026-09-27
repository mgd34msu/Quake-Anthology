#include "internal.h"

static bool reserve(void **data, size_t *capacity, size_t count, size_t width, qa_error *e) {
    if (count <= *capacity)
        return true;
    if (count > SIZE_MAX / width)
        return bot_move_fail(e, "bot route storage exceeds address range");
    void *p = realloc(*data, count * width);
    if (!p) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "retaining bot route workspace");
        return false;
    }
    *data = p;
    *capacity = count;
    return true;
}
bool bot_travel_begin(qa_bot_moves *moves, qa_bot_move_state *state, bot_travel *out, qa_error *e) {
    for (size_t i = 0; i < BOT_MOVE_VARIABLE_COUNT; ++i)
        if (!moves->variables[i])
            return bot_move_fail(e, "source bot movement requires setup");
    qa_bot_navigation *navigation =
        moves->services.navigation(moves->services.context, state->input.client);
    if (!navigation)
        return bot_move_fail(e, "bot client has no selected navigation");
    qa_navigation *runtime = qa_bot_navigation_runtime(navigation);
    *out = (bot_travel){.moves = moves,
                        .state = state,
                        .navigation = navigation,
                        .runtime = runtime,
                        .graph = qa_navigation_graph(runtime),
                        .actor = qa_bot_navigation_actor(navigation)};
    return true;
}
static uint32_t source_type(const qa_nav_edge *edge) {
    if (edge->source.kind == QA_NAV_ORIGIN_AAS)
        return edge->source_travel_type;
    static const uint8_t types[QA_NAV_TRAVEL_COUNT] = {
        BOT_WALK,        BOT_CROUCH,   BOT_JUMP,         BOT_DROP,        BOT_SWIM,
        BOT_WATER_JUMP,  BOT_LADDER,   BOT_TELEPORT,     BOT_ELEVATOR,    BOT_JUMP_PAD,
        BOT_ROCKET_JUMP, BOT_BFG_JUMP, BOT_GRAPPLE_HOOK, BOT_DOUBLE_JUMP, BOT_RAMP_JUMP,
        BOT_STRAFE_JUMP, BOT_INVALID};
    if (edge->mode == QA_NAV_JUMP &&
        (edge->source_travel_type == 5 || edge->source_travel_type == 11))
        return BOT_BARRIER_JUMP;
    return types[edge->mode];
}
bool bot_reach_describe(const bot_travel *t, const qa_nav_edge *edge, bot_reach *out, qa_error *e) {
    const qa_aas_view *aas = qa_nav_asset_aas(t->graph->asset);
    const qa_aas_reach *source =
        aas && edge->id < aas->count[QA_AAS_REACHABILITY] ? &aas->reachability[edge->id] : NULL;
    double edge_height = trunc((double)edge->end.z - edge->start.z);
    double time = trunc((double)edge->travel_seconds * 100);
    if (!source && (!isfinite(edge_height) || edge_height < INT32_MIN || edge_height > INT32_MAX ||
                    !isfinite(time) || time > UINT32_MAX))
        return bot_move_fail(e, "foreign reachability exceeds source integer fields");
    *out = (bot_reach){.graph_edge = edge,
                       .area = qa_bot_navigation_source_area(t->navigation, edge->to),
                       .number = qa_bot_navigation_source_area(t->navigation, edge->id),
                       .face = source                                       ? source->face
                               : edge->has_entity && edge->entity.has_model ? edge->entity.model
                                                                            : 0,
                       .edge = source ? source->edge : (int32_t)edge_height,
                       .start = edge->start,
                       .end = edge->end,
                       .type = source_type(edge),
                       .time = source     ? source->travel_time
                               : time < 1 ? 1
                                          : (uint32_t)time};
    return true;
}
bool bot_reach_read(const bot_travel *t, uint32_t number, bot_reach *out, bool *found,
                    qa_error *e) {
    const qa_nav_edge *edge = qa_bot_navigation_reachability(t->navigation, number);
    *found = edge != NULL;
    return !edge || bot_reach_describe(t, edge, out, e);
}
float bot_reach_time(const bot_reach *r) {
    switch (r->type & BOT_TRAVEL_MASK) {
    case BOT_WALK:
    case BOT_CROUCH:
    case BOT_BARRIER_JUMP:
    case BOT_DROP:
    case BOT_JUMP:
    case BOT_SWIM:
    case BOT_WATER_JUMP:
    case BOT_TELEPORT:
        return 5;
    case BOT_LADDER:
    case BOT_ROCKET_JUMP:
    case BOT_BFG_JUMP:
        return 6;
    case BOT_ELEVATOR:
    case BOT_JUMP_PAD:
    case BOT_BOBBING:
        return 10;
    default:
        return 8;
    }
}
static float distance_squared(qa_vec3 first, qa_vec3 second) {
    qa_vec3 v = qa_vec_sub(second, first);
    return qa_vec_dot(v, v);
}
static float line_distance_squared(qa_vec3 point, qa_vec3 start, qa_vec3 end) {
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(end, start));
    qa_vec3 projection = bot_ma(start, qa_vec_dot(qa_vec_sub(point, start), direction), direction);
    float p[3] = {projection.x, projection.y, projection.z};
    float a[3] = {start.x, start.y, start.z}, b[3] = {end.x, end.y, end.z};
    for (size_t i = 0; i < 3; ++i)
        if ((p[i] > a[i] && p[i] > b[i]) || (p[i] < a[i] && p[i] < b[i]))
            return distance_squared(point, fabsf(p[i] - a[i]) < fabsf(p[i] - b[i]) ? start : end);
    return distance_squared(point, projection);
}
static bool avoid_spots(const qa_bot_move_state *state, const bot_reach *reach) {
    bool continuous;
    switch (reach->type & BOT_TRAVEL_MASK) {
    case BOT_DROP:
    case BOT_JUMP:
    case BOT_TELEPORT:
    case BOT_ELEVATOR:
    case BOT_GRAPPLE_HOOK:
    case BOT_ROCKET_JUMP:
    case BOT_BFG_JUMP:
    case BOT_JUMP_PAD:
    case BOT_BOBBING:
        continuous = false;
        break;
    default:
        continuous = true;
        break;
    }
    int32_t result = 0;
    for (size_t i = 0; i < state->avoid_count; ++i) {
        const qa_bot_avoid_spot *spot = &state->avoid_spots[i];
        float radius = spot->radius * spot->radius;
        float distance = line_distance_squared(spot->origin, state->input.origin, reach->start);
        if (distance < radius && distance_squared(spot->origin, state->input.origin) > distance)
            result = spot->type;
        else {
            if (continuous)
                distance = line_distance_squared(spot->origin, reach->start, reach->end);
            if (distance < radius && distance_squared(spot->origin, reach->start) > distance)
                result = spot->type;
        }
        if (result == 1)
            return true;
    }
    return result != 0;
}
static int candidate_compare(const void *first, const void *second) {
    const bot_move_candidate *a = first, *b = second;
    if (a->time != b->time)
        return a->time < b->time ? -1 : 1;
    return a->order < b->order ? -1 : a->order != b->order;
}
bool bot_reach_select(bot_travel *t, const qa_bot_goal *goal, uint32_t travel_flags,
                      uint32_t move_flags, uint32_t *number, uint32_t *result_flags, qa_error *e) {
    qa_bot_move_state *s = t->state;
    qa_bot_moves *m = t->moves;
    *number = *result_flags = 0;
    if (qa_nav_asset_aas(t->graph->asset) &&
        ((qa_bot_navigation_area(t->navigation, s->area).contents |
          qa_bot_navigation_area(t->navigation, (uint32_t)goal->area).contents) &
         256)) {
        travel_flags |= 0x800000;
        move_flags |= 0x800000;
    }
    uint32_t node = qa_bot_navigation_node(t->navigation, s->area);
    size_t count = qa_navigation_outgoing_count(t->runtime, node), candidates = 0;
    if (!reserve((void **)&m->candidates, &m->candidate_capacity, count, sizeof(*m->candidates), e))
        return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_nav_edge *edge = qa_navigation_outgoing(t->runtime, node, i);
        bot_reach reach;
        if (!bot_reach_describe(t, edge, &reach, e))
            return false;
        uint32_t flag = qa_nav_aas_travel_flag(reach.type);
        if (!(flag & travel_flags) || !(flag & move_flags))
            continue;
        if (s->avoid_reachability == reach.number && s->avoid_time >= m->time && s->avoid_tries > 4)
            continue;
        if (s->last_goal_area == (uint32_t)goal->area && reach.area == s->last_area)
            continue;
        qa_bot_nav_route_query query = {.area = reach.area,
                                        .goal_area = (uint32_t)goal->area,
                                        .travel_flags = travel_flags,
                                        .origin = reach.end,
                                        .has_origin = true};
        qa_bot_nav_route route;
        if (!qa_bot_navigation_route(t->navigation, &query, &route, e))
            return false;
        if (!route.found || !route.travel_time)
            continue;
        if (avoid_spots(s, &reach)) {
            *result_flags |= QA_BOT_MOVE_AVOID_SPOT;
            continue;
        }
        uint32_t time_word = route.travel_time + reach.time;
        int32_t time;
        memcpy(&time, &time_word, sizeof(time));
        m->candidates[candidates++] = (bot_move_candidate){edge, time, i};
    }
    if (candidates > 1)
        qsort(m->candidates, candidates, sizeof(*m->candidates), candidate_compare);
    for (size_t i = 0; i < candidates; ++i) {
        const qa_nav_edge *edge = m->candidates[i].edge;
        if (!qa_navigation_admit_edge(t->runtime, t->actor, edge->id, s->input.origin,
                                      &m->trajectory, e))
            return false;
        if (m->trajectory.found) {
            *number = qa_bot_navigation_source_area(t->navigation, edge->id);
            break;
        }
    }
    return true;
}
bool bot_travel_points(bot_travel *t, qa_vec3 origin, uint32_t area, const qa_bot_goal *goal,
                       uint32_t flags, bool *found, qa_error *e) {
    qa_bot_moves *m = t->moves;
    *found = false;
    m->point_count = 0;
    if (t->graph->edge_count > (SIZE_MAX - 1) / 2)
        return bot_move_fail(e, "estimated bot path capacity overflow");
    size_t old_capacity = m->visited_capacity;
    if (!reserve((void **)&m->points, &m->point_capacity, t->graph->edge_count * 2 + 1,
                 sizeof(*m->points), e) ||
        !reserve((void **)&m->visited, &m->visited_capacity, t->graph->edge_count,
                 sizeof(*m->visited), e))
        return false;
    if (m->visited_capacity > old_capacity)
        memset(m->visited + old_capacity, 0,
               (m->visited_capacity - old_capacity) * sizeof(*m->visited));
    if (!++m->visit_generation) {
        if (m->visited_capacity)
            memset(m->visited, 0, m->visited_capacity * sizeof(*m->visited));
        m->visit_generation = 1;
    }
    m->points[m->point_count++] = origin;
    while (area != (uint32_t)goal->area) {
        qa_bot_nav_route_query query = {.area = area,
                                        .goal_area = (uint32_t)goal->area,
                                        .travel_flags = flags,
                                        .origin = origin,
                                        .has_origin = true};
        qa_bot_nav_route route;
        if (!qa_bot_navigation_route(t->navigation, &query, &route, e))
            return false;
        const qa_nav_edge *edge =
            route.found ? qa_bot_navigation_reachability(t->navigation, route.next_reachability)
                        : NULL;
        if (!edge)
            return true;
        size_t ordinal = (size_t)(edge - t->graph->edges);
        if (m->visited[ordinal] == m->visit_generation)
            return true;
        m->visited[ordinal] = m->visit_generation;
        m->points[m->point_count++] = edge->start;
        m->points[m->point_count++] = edge->end;
        area = qa_bot_navigation_source_area(t->navigation, edge->to);
        origin = edge->end;
    }
    *found = true;
    return true;
}
