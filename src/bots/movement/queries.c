#include "internal.h"

bool bot_trace(bot_travel *t, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int32_t entity,
               uint32_t mask, qa_trace_result *out, qa_error *e) {
    qa_actor_id pass = entity >= 0 && t->moves->services.actor
                           ? t->moves->services.actor(t->moves->services.context, entity)
                           : (qa_actor_id){0};
    return qa_bot_navigation_trace(t->navigation, start, end, bounds, pass, mask, out, e);
}
bool bot_trace_box(bot_travel *t, qa_vec3 start, qa_vec3 end, uint32_t presence, int32_t entity,
                   qa_trace_result *out, qa_error *e) {
    qa_bounds bounds = qa_bot_navigation_presence(t->navigation, presence == 2 ? 2 : 4);
    return bot_trace(t, start, end, &bounds, entity, 0x2010001, out, e);
}
int32_t bot_trace_entity(const bot_travel *t, const qa_trace_result *trace) {
    if (trace->hit == QA_TRACE_HIT_WORLD)
        return 1022;
    if (trace->hit != QA_TRACE_HIT_ACTOR)
        return 1023;
    return t->moves->services.entity_number
               ? t->moves->services.entity_number(t->moves->services.context, trace->actor)
               : 0;
}
bool bot_on_ground(bot_travel *t, bool *out, qa_error *e) {
    bot_move_record *s = t->state;
    qa_vec3 end = bot_move_vector(s,BM_ORIGIN);
    end.z -= 10;
    qa_trace_result trace;
    if (!bot_trace_box(t, bot_move_vector(s,BM_ORIGIN), end, bot_move_word(s,BM_PRESENCE), bot_move_integer(s,BM_ENTITY), &trace, e))
        return false;
    *out = !trace.start_solid && !trace.all_solid && trace.fraction < 1 &&
           bot_move_vector(s,BM_ORIGIN).z - trace.end.z <= 10 && trace.contact &&
           trace.contact_plane.normal.z >= t->graph->profile.minimum_floor_normal;
    return true;
}
bool bot_model(bot_travel *t, int32_t model, qa_bot_travel_model *out, bool *found, qa_error *e) {
    *found = false;
    return !t->moves->services.model ||
           t->moves->services.model(t->moves->services.context, model, out, found, e);
}
bool bot_on_mover(bot_travel *t, const bot_reach *r, bool *out, qa_error *e) {
    qa_bot_travel_model model;
    bool found;
    *out = false;
    if (!bot_model(t, r->face & 65535, &model, &found, e))
        return false;
    if (!found)
        return true;
    qa_vec3 origin = bot_move_vector(t->state,BM_ORIGIN);
    qa_bounds bounds = t->graph->profile.shape.bounds;
    if (origin.x > model.origin.x + model.bounds.maxs.x - bounds.mins.x ||
        origin.x < model.origin.x + model.bounds.mins.x - bounds.maxs.x ||
        origin.y > model.origin.y + model.bounds.maxs.y - bounds.mins.y ||
        origin.y < model.origin.y + model.bounds.mins.y - bounds.maxs.y)
        return true;
    qa_vec3 start = origin, end = origin;
    start.z += 24;
    end.z -= 48;
    bounds.mins.z = -8;
    bounds.maxs.z = 8;
    qa_trace_result trace;
    if (!bot_trace(t, start, end, &bounds, bot_move_integer(t->state,BM_ENTITY), 0x10001, &trace, e))
        return false;
    int32_t entity = bot_trace_entity(t, &trace);
    *out = !trace.start_solid && !trace.all_solid && entity != 1023 &&
           t->moves->services.entity_model &&
           t->moves->services.entity_model(t->moves->services.context, entity) == (r->face & 65535);
    return true;
}
bool bot_mover_down(bot_travel *t, const bot_reach *r, bool *out, qa_error *e) {
    qa_bot_travel_model model;
    bool found;
    if (!bot_model(t, r->face & 65535, &model, &found, e))
        return false;
    *out = found && model.origin.z + model.bounds.maxs.z < r->start.z;
    return true;
}
static bool gap_distance_from(bot_travel *t,const qa_bot_vector_source *origin,qa_vec3 direction,
                              int32_t entity,float *out,qa_error *e) {
    qa_trace_result trace;
    qa_vec3 end,start;
    if(!qa_bot_vector_read(origin,&end,e)) return false;
    end.z -= 60;
    if(!qa_bot_vector_read(origin,&start,e)) return false;
    if (!bot_trace_box(t, start, end, 4, entity, &trace, e))
        return false;
    *out = 1;
    if (trace.fraction >= 1)
        return true;
    float start_z = trace.end.z + 1;
    for (int distance = 8; distance <= 100; distance += 8) {
        if(!qa_bot_vector_read(origin,&start,e)) return false;
        start = bot_ma(start, (float)distance, direction);
        start.z = start_z + 24;
        end = start;
        end.z -= 48 + bot_variable(t, BOT_BARRIER);
        if (!bot_trace_box(t, start, end, 4, entity, &trace, e))
            return false;
        if (!trace.start_solid && !trace.all_solid) {
            if (trace.end.z < start_z - bot_variable(t, BOT_STEP) - 8) {
                qa_vec3 point = trace.end;
                point.z -= 20;
                int32_t contents;
                if (!qa_bot_navigation_contents(t->navigation, point, &contents, e))
                    return false;
                if (contents & 32)
                    break;
                *out = (float)distance;
                return true;
            }
            start_z = trace.end.z;
        }
    }
    *out = 0;
    return true;
}
bool bot_gap_distance(bot_travel *t,qa_vec3 origin,qa_vec3 direction,float *out,qa_error *e) {
    qa_bot_vector_source source={.value=&origin};
    return gap_distance_from(t,&source,direction,bot_move_integer(t->state,BM_ENTITY),out,e);
}
bool bot_gap_distance_state(bot_travel *t,qa_vec3 direction,float *out,qa_error *e) {
    qa_bot_vector_source source=bot_move_origin_source(t->state);
    return gap_distance_from(t,&source,direction,bot_move_integer(t->state,BM_ENTITY),out,e);
}
bool bot_barrier_jump(bot_travel *t, qa_vec3 direction, float speed, bool *out, qa_error *e) {
    qa_bot_vector_source source = {.value = &direction};
    return bot_barrier_jump_from(t, &source, speed, out, e);
}
bool bot_barrier_jump_from(bot_travel *t, const qa_bot_vector_source *direction, float speed,
                           bool *out, qa_error *e) {
    *out = false;
    if (!(t->graph->profile.capabilities & QA_NAV_CAPABILITY(QA_NAV_JUMP)))
        return true;
    bot_move_record *s = t->state;
    qa_vec3 end = bot_move_vector(s,BM_ORIGIN);
    end.z += bot_variable(t, BOT_BARRIER);
    qa_trace_result trace;
    if (!bot_trace_box(t, bot_move_vector(s,BM_ORIGIN), end, 2, bot_move_integer(s,BM_ENTITY), &trace, e))
        return false;
    if (trace.start_solid || trace.all_solid ||
        trace.end.z - bot_move_vector(s,BM_ORIGIN).z < bot_variable(t, BOT_STEP))
        return true;
    qa_vec3 horizontal = {0};
    if (!qa_bot_vector_component(direction, 0, &horizontal.x, e) ||
        !qa_bot_vector_component(direction, 1, &horizontal.y, e)) return false;
    horizontal = qa_vec_normalize(horizontal);
    float distance = (bot_move_float(s,BM_THINK_TIME) * speed) * .5f;
    end = bot_ma(bot_move_vector(s,BM_ORIGIN), distance, horizontal);
    end.z = trace.end.z;
    if (!bot_trace_box(t, trace.end, end, 2, bot_move_integer(s,BM_ENTITY), &trace, e))
        return false;
    if (trace.start_solid || trace.all_solid)
        return true;
    end = trace.end;
    end.z = bot_move_vector(s,BM_ORIGIN).z;
    if (!bot_trace_box(t, trace.end, end, 2, bot_move_integer(s,BM_ENTITY), &trace, e))
        return false;
    if (trace.start_solid || trace.all_solid || trace.fraction >= 1 ||
        trace.end.z - bot_move_vector(s,BM_ORIGIN).z < bot_variable(t, BOT_STEP))
        return true;
    if (!qa_navigation_admit_movement(t->runtime, t->actor, bot_move_vector(s,BM_ORIGIN), trace.end, QA_NAV_JUMP,
                                      &t->moves->trajectory, e))
        return false;
    if (!t->moves->trajectory.found)
        return true;
    if (!bot_jump_action(t, false, e) || !bot_move_action(t, horizontal, speed, e))
        return false;
    bot_move_write_word(s,BM_FLAGS,bot_move_word(s,BM_FLAGS) | (QA_BOT_MOVE_BARRIER_JUMP));
    *out = true;
    return true;
}
bool bot_blocked(bot_travel *t, qa_vec3 direction, bool bottom, qa_bot_move_result *out,
                 qa_error *e) {
    bot_move_record *s = t->state;
    qa_bounds bounds = qa_bot_navigation_presence(t->navigation, bot_move_word(s,BM_PRESENCE) == 2 ? 2 : 4);
    if (fabsf(direction.z) < .7f) {
        bounds.mins.z += bot_variable(t, BOT_STEP);
        bounds.maxs.z -= 10;
    }
    qa_trace_result trace;
    if (!bot_trace(t, bot_move_vector(s,BM_ORIGIN), bot_ma(bot_move_vector(s,BM_ORIGIN), 3, direction), &bounds, bot_move_integer(s,BM_ENTITY), 0x2010001,
                   &trace, e))
        return false;
    int32_t entity = bot_trace_entity(t, &trace);
    if (!trace.start_solid && !trace.all_solid && entity != 1022 && entity != 1023) {
        out->blocked = true;
        out->block_entity = entity;
    } else if (bottom && !qa_bot_navigation_area(t->navigation, bot_move_word(t->state,BM_AREA)).reach_count) {
        bounds = qa_bot_navigation_presence(t->navigation, bot_move_word(s,BM_PRESENCE) == 2 ? 2 : 4);
        qa_vec3 end = bot_move_vector(s,BM_ORIGIN);
        end.z -= 3;
        if (!bot_trace(t, bot_move_vector(s,BM_ORIGIN), end, &bounds, bot_move_integer(s,BM_ENTITY), 0x10001, &trace, e))
            return false;
        entity = bot_trace_entity(t, &trace);
        if (!trace.start_solid && !trace.all_solid && entity != 1022 && entity != 1023) {
            out->blocked = true;
            out->block_entity = entity;
            out->flags |= QA_BOT_MOVE_ON_OBSTACLE;
        }
    }
    return true;
}
bool bot_predict(bot_travel *t, const qa_nav_prediction_query *query, qa_error *e) {
    qa_nav_prediction_query q = *query;
    q.actor = t->actor;
    q.stop_area = qa_bot_navigation_node(t->navigation, q.stop_area);
    return qa_navigation_predict(t->runtime, t->moves->workspace, &q, &t->moves->prediction, e);
}
bool bot_jump_run_start(bot_travel *t, const bot_reach *r, qa_vec3 *out, qa_error *e) {
    qa_vec3 direction = qa_vec_normalize(bot_horizontal(r->end, r->start));
    qa_vec3 start = r->start;
    start.z += 1;
    qa_nav_prediction_query q = {.origin = start,
                                 .presence = 2,
                                 .on_ground = true,
                                 .command_move = qa_vec_scale(direction, 400),
                                 .command_frames = 1,
                                 .maximum_frames = 2,
                                 .frame_ms = 100,
                                 .stop_events = 4 | 8 | 16 | 32 | 64};
    if (!bot_predict(t, &q, e))
        return false;
    *out = t->moves->prediction.stop_event & (8 | 16 | 32) ? start : t->moves->prediction.end;
    return true;
}
bool bot_jump_speed(bot_travel *t, qa_vec3 start, qa_vec3 end, float vertical, float *out,
                    qa_error *e) {
    qa_vec3 delta = bot_horizontal(start, end), direction = qa_vec_normalize(delta);
    qa_nav_prediction_query q = {
        .origin = start,
        .presence = 2,
        .velocity = {bot_move_vector(t->state,BM_VELOCITY).x, bot_move_vector(t->state,BM_VELOCITY).y, vertical},
        .command_move = qa_vec_scale(direction, 400),
        .command_frames = 30,
        .maximum_frames = 30,
        .frame_ms = 100,
        .stop_events = 1 | 8 | 16 | 32};
    if (!bot_predict(t, &q, e))
        return false;
    const qa_nav_prediction_result *p = &t->moves->prediction;
    *out = p->grounded && p->seconds > 0 ? fminf(400, qa_vec_length(delta) / p->seconds) : 400;
    return true;
}
bool bot_air_control(bot_travel *t, qa_vec3 goal, bool *controlled, qa_vec3 *direction,
                     float *speed, qa_error *e) {
    bot_move_record *s = t->state;
    qa_nav_prediction_query q = {.origin = bot_move_vector(s,BM_ORIGIN),
                                 .velocity = bot_move_vector(s,BM_VELOCITY),
                                 .presence = bot_move_word(s,BM_PRESENCE) == 2 ? 2 : 4,
                                 .maximum_frames = 50,
                                 .frame_ms = 100};
    if (!bot_predict(t, &q, e))
        return false;
    *controlled = false;
    *direction = qa_v3(0, 0, 0);
    *speed = 400;
    const qa_nav_prediction_result *p = &t->moves->prediction;
    qa_vec3 previous = bot_move_vector(s,BM_ORIGIN);
    for (size_t i = 0; i < p->trajectory_count; ++i) {
        qa_vec3 next = p->trajectory[i];
        if (next.z < previous.z && previous.z >= goal.z && next.z < goal.z) {
            float fraction = (goal.z - previous.z) / (next.z - previous.z);
            qa_vec3 position = bot_ma(previous, fraction, qa_vec_sub(next, previous));
            qa_vec3 delta = qa_vec_sub(goal, position);
            float distance = fminf(qa_vec_length(delta), 32);
            *direction = qa_vec_normalize(delta);
            *speed = 400 - (400 - 13 * distance);
            *controlled = true;
            break;
        }
        previous = next;
    }
    return true;
}
uint32_t bot_area_presence(bot_travel *t, uint32_t area) {
    uint32_t presence = qa_bot_navigation_area(t->navigation, area).presence;
    if (presence || qa_nav_asset_aas(t->graph->asset))
        return presence;
    const qa_nav_node *node =
        qa_navigation_node(t->runtime, qa_bot_navigation_node(t->navigation, area));
    return node && node->source.kind == QA_NAV_ORIGIN_NAV3 && (node->flags & 512) ? 4 : 2;
}
qa_vec3 bot_vector_angles(qa_vec3 v) {
    float yaw, pitch;
    if (v.x == 0 && v.y == 0) {
        yaw = 0;
        pitch = v.z > 0 ? 90 : 270;
    } else {
        yaw = v.x != 0.0f ? atan2f(v.y, v.x) * (180.0f / 3.14159265358979323846f) : v.y > 0 ? 90 : 270;
        if (yaw < 0)
            yaw += 360;
        pitch = atan2f(v.z, sqrtf(v.x * v.x + v.y * v.y)) * (180.0f / 3.14159265358979323846f);
        if (pitch < 0)
            pitch += 360;
    }
    return qa_v3(-pitch, yaw, 0);
}
static bool action_client(bot_travel *t, uint32_t *out, qa_error *e) {
    int32_t client=bot_move_integer(t->state,BM_CLIENT);
    if(t->moves->services.source_action_client)
        return t->moves->services.source_action_client(t->moves->services.context,client,out,e);
    *out=(uint32_t)client;return true;
}
bool bot_move_action(bot_travel *t, qa_vec3 direction, float speed, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) &&
        qa_bot_actions_move(t->moves->actions,client,direction,speed,e);
}
bool bot_flag_action(bot_travel *t, uint32_t flags, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) && qa_bot_actions_add(t->moves->actions,client,flags,e);
}
bool bot_jump_action(bot_travel *t, bool delayed, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) && qa_bot_actions_jump(t->moves->actions,client,delayed,e);
}
bool bot_view_action(bot_travel *t, qa_vec3 angles, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) && qa_bot_actions_view(t->moves->actions,client,angles,e);
}
bool bot_weapon_action(bot_travel *t, int32_t weapon, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) && qa_bot_actions_weapon(t->moves->actions,client,weapon,e);
}
bool bot_text_action(bot_travel *t, const char *command, qa_error *e) {
    uint32_t client;
    return action_client(t,&client,e) &&
        qa_bot_actions_text(t->moves->actions,(int32_t)client,QA_BOT_COMMAND,0,command,e);
}
