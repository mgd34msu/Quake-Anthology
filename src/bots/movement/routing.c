#include "internal.h"

bool bot_travel_ready(const qa_bot_moves *moves, qa_error *e) {
    for (size_t i = 0; i < BOT_MOVE_VARIABLE_COUNT; ++i)
        if (!moves->variables[i])
            return bot_move_fail(e, "source bot movement requires setup");
    return true;
}
bool bot_travel_begin_client(qa_bot_moves *moves, int32_t client, bot_travel *out, qa_error *e) {
    qa_bot_navigation *navigation =
        moves->services.navigation(moves->services.context, client);
    if (!navigation)
        return bot_move_fail(e, "bot client has no selected navigation");
    qa_navigation *runtime = qa_bot_navigation_runtime(navigation);
    *out = (bot_travel){.moves = moves,
                        .state = NULL,
                        .navigation = navigation,
                        .runtime = runtime,
                        .graph = qa_navigation_graph(runtime),
                        .actor = qa_bot_navigation_actor(navigation)};
    return true;
}
bool bot_travel_begin(qa_bot_moves *moves,bot_move_record *state,bot_travel *out,qa_error *e) {
    if(!bot_travel_begin_client(moves,bot_move_integer(state,BM_CLIENT),out,e)) return false;
    out->state=state;return true;
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
static bool avoid_spots(const bot_move_record *state, const bot_reach *reach) {
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
    for (int32_t i = 0; i < bot_move_integer(state,BM_AVOID_COUNT); ++i) {
        qa_bot_avoid_spot value=bot_move_spot(state,i);
        const qa_bot_avoid_spot *spot = &value;
        float radius = spot->radius * spot->radius;
        float distance = line_distance_squared(spot->origin, bot_move_vector(state,BM_ORIGIN), reach->start);
        if (distance < radius && distance_squared(spot->origin, bot_move_vector(state,BM_ORIGIN)) > distance)
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
typedef struct route_filter {
    bot_travel *travel;
    uint32_t first, goal, travel_flags, move_flags;
    uint32_t *result_flags;
    qa_error *error;
    bool ok;
} route_filter;
static bool reach_allowed(route_filter *filter,const qa_nav_edge *edge,bool first) {
    bot_move_record *state=filter->travel->state;
    bot_reach reach;
    if(!bot_reach_describe(filter->travel,edge,&reach,filter->error)) {
        filter->ok=false;return false;
    }
    uint32_t flag=qa_nav_aas_travel_flag(reach.type);
    if(!(flag&filter->travel_flags)) return false;
    if(!first) return true;
    if(!(flag&filter->move_flags)) return false;
    if(bot_move_word(state,BM_AVOID_REACHABILITY)==reach.number &&
       bot_move_float(state,BM_AVOID_TIME)>=filter->travel->moves->time &&
       bot_move_integer(state,BM_AVOID_TRIES)>4) return false;
    if(bot_move_word(state,BM_LAST_GOAL_AREA)==filter->goal &&
       reach.area==bot_move_word(state,BM_LAST_AREA)) return false;
    if(avoid_spots(state,&reach)) {
        *filter->result_flags|=QA_BOT_MOVE_AVOID_SPOT;return false;
    }
    return true;
}
static bool route_edge(void *opaque,const qa_nav_edge *edge) {
    route_filter *filter=opaque;
    return reach_allowed(filter,edge,edge->from==filter->first);
}
static bool retained_route(bot_travel *travel,route_filter *filter,bool *valid,qa_error *e) {
    bot_move_record *state=travel->state;
    *valid=false;
    if(!state->route.found || state->route_goal!=filter->goal ||
       state->route_flags!=filter->travel_flags || state->route_move_flags!=filter->move_flags ||
       !qa_actor_id_equal(state->route_actor,travel->actor) ||
       state->route_map.format!=travel->graph->map.format ||
       (state->route.graph && qa_nav_graph_read(state->route.graph)!=travel->graph)) return true;
    size_t cursor=state->route_cursor;
    for(size_t i=cursor+1;i<state->route.node_count;++i)
        if(state->route.nodes[i]==filter->first) {cursor=i;break;}
    if(cursor>=state->route.edge_count || state->route.nodes[cursor]!=filter->first) return true;
    for(size_t i=cursor;i<state->route.edge_count;++i) {
        const qa_nav_edge *edge=qa_navigation_edge(travel->runtime,state->route.edges[i]);
        bool allowed;
        if(!edge || !reach_allowed(filter,edge,i==cursor)) return filter->ok;
        if(!qa_navigation_edge_allowed(travel->runtime,travel->actor,edge->id,&allowed,e))
            return false;
        if(!allowed) return true;
    }
    state->route_cursor=cursor;*valid=true;return true;
}
bool bot_reach_select(bot_travel *t,const qa_bot_move_goal_source *goal,uint32_t travel_flags,
                      uint32_t move_flags,uint32_t *number,uint32_t *result_flags,qa_error *e) {
    bot_move_record *state=t->state;
    qa_bot_moves *moves=t->moves;
    *number=*result_flags=0;
    uint32_t goal_area;
    if(!bot_goal_area(goal,&goal_area,e)) return false;
    if(qa_nav_asset_aas(t->graph->asset) &&
       ((qa_bot_navigation_area(t->navigation,bot_move_word(state,BM_AREA)).contents |
         qa_bot_navigation_area(t->navigation,goal_area).contents)&256)) {
        travel_flags|=0x800000;move_flags|=0x800000;
    }
    uint32_t node=qa_bot_navigation_node(t->navigation,bot_move_word(state,BM_AREA));
    uint32_t target=qa_bot_navigation_node(t->navigation,goal_area);
    const qa_nav_node *destination=qa_navigation_node(t->runtime,target);
    if(!destination) {bot_move_route_clear(state);return true;}
    route_filter filter={.travel=t,.first=node,.goal=goal_area,.travel_flags=travel_flags,
        .move_flags=move_flags,.result_flags=result_flags,.error=e,.ok=true};
    bool retained;
    if(!retained_route(t,&filter,&retained,e)) return false;
    for(unsigned attempt=0;attempt<2;++attempt) {
        if(!retained) {
            bot_move_route_clear(state);
            qa_nav_route_query query={.actor=t->actor,.start=bot_move_vector(state,BM_ORIGIN),
                .goal=destination->origin,.start_node=node,.goal_node=target,
                .travel_flags=travel_flags,.has_travel_flags=true,
                .context=&filter,.edge_filter=route_edge};
            if(!qa_navigation_route(t->runtime,moves->workspace,&query,&state->route,e) ||
               !filter.ok) return false;
            if(!state->route.found || !state->route.edge_count) {
                bot_move_route_clear(state);return true;
            }
            state->route_map=t->graph->map;state->route_actor=t->actor;
            state->route_goal=goal_area;state->route_flags=travel_flags;
            state->route_move_flags=move_flags;
            free(state->route.points);state->route.points=NULL;
            state->route.point_count=state->route.point_capacity=0;
        }
        const qa_nav_edge *edge=qa_navigation_edge(t->runtime,
            state->route.edges[state->route_cursor]);
        if(!qa_navigation_admit_edge(t->runtime,t->actor,edge->id,
                bot_move_vector(state,BM_ORIGIN),&moves->trajectory,e)) return false;
        if(moves->trajectory.found) {
            if(!state->route.graph) {
                state->route.graph=moves->trajectory.graph;
                qa_nav_graph_retain(state->route.graph);
            }
            if(retained && state->walk_edge==edge->id) state->walk_progress=true;
            *number=qa_bot_navigation_source_area(t->navigation,edge->id);
            return true;
        }
        bot_move_route_clear(state);
        if(!retained) return true;
        retained=false;
    }
    return true;
}
bool bot_travel_points(bot_travel *t, const qa_bot_vector_source *source, uint32_t area, uint32_t goal_area,
                       uint32_t flags, bool *found, qa_error *e) {
    qa_bot_moves *m = t->moves;
    *found = false;
    m->point_count = 0;
    qa_stamp_set_begin(&m->visited);
    /* The initial point stays borrowed until its consumer reaches it. */
    m->points[m->point_count++] = qa_v3(0, 0, 0);
    qa_vec3 origin;
    qa_bot_vector_source current = *source;
    while (area != goal_area) {
        if (!area || !goal_area ||
            !qa_navigation_node(t->runtime, qa_bot_navigation_node(t->navigation, area)) ||
            !qa_navigation_node(t->runtime, qa_bot_navigation_node(t->navigation, goal_area))) return true;
        if (!qa_bot_vector_read(&current, &origin, e)) return false;
        qa_bot_nav_route_query query = {.area = area,
                                        .goal_area = goal_area,
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
        if (!qa_stamp_set_mark(&m->visited, ordinal))
            return true;
        m->points[m->point_count++] = edge->start;
        m->points[m->point_count++] = edge->end;
        area = qa_bot_navigation_source_area(t->navigation, edge->to);
        origin = edge->end;
        current = (qa_bot_vector_source){.value = &origin};
    }
    *found = true;
    return true;
}
