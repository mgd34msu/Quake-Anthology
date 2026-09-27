#include "internal.h"

static float centered_random(bot_travel *t) {
    return 2 * ((float)(t->moves->services.random.next(t->moves->services.random.context) & 32767) /
                    32767.0f -
                .5f);
}
static bool jump(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                 qa_error *e) {
    qa_bot_move_state *state = t->state;
    qa_vec3 origin = state->input.origin;
    qa_vec3 direction;
    if (airborne) {
        if (!state->jump_reach)
            return true;
        qa_vec3 offset = bot_horizontal(origin, r->end);
        float distance = qa_vec_length(offset);
        direction = qa_vec_normalize(offset);
        if (qa_vec_dot(direction, qa_vec_normalize(bot_horizontal(r->start, r->end))) < -.5f &&
            distance < 24)
            return true;
        if (!bot_move_action(t, direction, 800, e))
            return false;
    } else {
        qa_vec3 run_start;
        if (!bot_jump_run_start(t, r, &run_start, e))
            return false;
        direction = qa_vec_normalize(bot_horizontal(r->start, run_start));
        if (r->graph_edge->source.kind == QA_NAV_ORIGIN_AAS) {
            qa_vec3 start = r->start;
            start.z += 1;
            int scan;
            for (scan = 0; scan < 80; scan += 10) {
                qa_vec3 end = bot_ma(start, (float)(scan + 10), direction);
                end.z += 1;
                uint32_t area;
                if (!qa_bot_navigation_point(t->navigation, end, &area, e))
                    return false;
                if (area != state->reach_area)
                    break;
            }
            run_start = bot_ma(r->start, (float)scan, direction);
        }
        qa_vec3 from_start = bot_horizontal(r->start, origin),
                from_run = bot_horizontal(run_start, origin);
        float start_distance = qa_vec_length(from_start), run_distance = qa_vec_length(from_run);
        if (qa_vec_dot(qa_vec_normalize(from_start), qa_vec_normalize(from_run)) < -.8f ||
            run_distance < 5) {
            direction = qa_vec_normalize(bot_horizontal(origin, r->end));
            if (start_distance < 32 && !bot_jump_action(t, start_distance >= 24, e))
                return false;
            if (!bot_move_action(t, direction, 600, e))
                return false;
            state->jump_reach = state->last_reachability;
        } else {
            direction = qa_vec_normalize(bot_horizontal(origin, run_start));
            float speed = 400 - (400 - 5 * fminf(run_distance, 80));
            if (!bot_move_action(t, direction, speed, e))
                return false;
        }
    }
    out->direction = direction;
    return true;
}
static bool water_jump(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                       qa_error *e) {
    qa_bot_move_input *s = &t->state->input;
    qa_vec3 offset = qa_vec_sub(r->end, s->origin), direction;
    if (airborne) {
        if (s->flags & QA_BOT_MOVE_WATER_JUMP)
            return true;
        qa_vec3 point = s->origin;
        point.z -= 32;
        int32_t contents;
        if (!qa_bot_navigation_contents(t->navigation, point, &contents, e))
            return false;
        if (!(contents & 56))
            return true;
        offset.x += centered_random(t) * 10;
        offset.y += centered_random(t) * 10;
        offset.z += 70 + centered_random(t) * 10;
        direction = qa_vec_normalize(offset);
        if (!bot_move_action(t, direction, 400, e))
            return false;
    } else {
        float distance = qa_vec_length(qa_v3(offset.x, offset.y, 0));
        offset.z += 15 + centered_random(t) * 40;
        direction = qa_vec_normalize(offset);
        if (!bot_flag_action(t, QA_BOT_MOVE_FORWARD, e) ||
            (distance < 40 && !bot_flag_action(t, QA_BOT_MOVE_UP, e)))
            return false;
    }
    out->direction = direction;
    out->ideal_view_angles = bot_vector_angles(direction);
    out->flags |= QA_BOT_MOVE_VIEW;
    return true;
}
bool bot_ground_travel(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                       qa_error *e) {
    qa_bot_move_state *state = t->state;
    qa_bot_move_input *s = &state->input;
    qa_vec3 direction = {0}, offset;
    float speed = 400, distance;
    *out = (qa_bot_move_result){0};
    switch (r->type & BOT_TRAVEL_MASK) {
    case BOT_WALK: {
        offset = bot_horizontal(s->origin, r->start);
        distance = qa_vec_length(offset);
        direction = qa_vec_normalize(offset);
        bool waypoint = r->graph_edge->source.kind != QA_NAV_ORIGIN_AAS;
        if (!waypoint && !bot_blocked(t, direction, true, out, e))
            return false;
        if (waypoint && state->walk_progress && state->walk_edge != r->graph_edge->id)
            state->walk_progress = false;
        if (waypoint && distance < 10) {
            state->walk_progress = true;
            state->walk_edge = r->graph_edge->id;
        }
        if (distance < 10 || (waypoint && state->walk_progress)) {
            offset = bot_horizontal(s->origin, r->end);
            distance = qa_vec_length(offset);
            direction = qa_vec_normalize(offset);
        }
        if (waypoint && !bot_blocked(t, direction, true, out, e))
            return false;
        if (!(bot_area_presence(t, r->area) & 2) && distance < 20 &&
            !bot_flag_action(t, QA_BOT_CROUCH, e))
            return false;
        float gap;
        if (!bot_gap_distance(t, s->origin, direction, &gap, e))
            return false;
        if (s->flags & QA_BOT_MOVE_WALK) {
            speed = gap > 0 ? 200 - (180 - gap) : 200;
            if (!bot_flag_action(t, QA_BOT_WALK, e))
                return false;
        } else
            speed = gap > 0 ? 400 - (360 - 2 * gap) : 400;
        break;
    }
    case BOT_CROUCH:
        direction = qa_vec_normalize(bot_horizontal(s->origin, r->end));
        if (!bot_blocked(t, direction, true, out, e) || !bot_flag_action(t, QA_BOT_CROUCH, e))
            return false;
        break;
    case BOT_BARRIER_JUMP:
        if (airborne) {
            if (s->velocity.z >= 250)
                return true;
            direction = qa_vec_normalize(bot_horizontal(s->origin, r->end));
            if (!bot_blocked(t, direction, true, out, e))
                return false;
        } else {
            offset = bot_horizontal(s->origin, r->start);
            distance = qa_vec_length(offset);
            direction = qa_vec_normalize(offset);
            if (!bot_blocked(t, direction, true, out, e))
                return false;
            if (distance < 9) {
                if (!bot_jump_action(t, false, e))
                    return false;
                out->direction = direction;
                return true;
            }
            speed = 360 - (360 - 6 * fminf(distance, 60));
        }
        break;
    case BOT_SWIM:
        direction = qa_vec_normalize(qa_vec_sub(r->start, s->origin));
        if (!bot_blocked(t, direction, true, out, e))
            return false;
        out->ideal_view_angles = bot_vector_angles(direction);
        out->flags |= QA_BOT_MOVE_SWIM_VIEW;
        break;
    case BOT_WATER_JUMP:
        return water_jump(t, r, airborne, out, e);
    case BOT_DROP:
        if (airborne) {
            direction = qa_vec_sub(r->end, s->origin);
            if (!bot_blocked(t, direction, true, out, e))
                return false;
            offset = bot_horizontal(s->origin, r->end);
            qa_vec3 end =
                qa_vec_length(offset) > 16 ? bot_ma(r->end, 16, qa_vec_normalize(offset)) : r->end;
            bool controlled;
            if (!bot_air_control(t, end, &controlled, &direction, &speed, e))
                return false;
            if (!controlled)
                direction = qa_vec_normalize(offset);
        } else {
            if (!bot_blocked(t, qa_vec_normalize(qa_vec_sub(r->start, s->origin)), true, out, e))
                return false;
            float reach_distance = qa_vec_length(bot_horizontal(r->start, r->end));
            offset = bot_horizontal(s->origin, r->start);
            distance = qa_vec_length(offset);
            direction = qa_vec_normalize(offset);
            if (distance < 48) {
                direction = qa_vec_normalize(bot_horizontal(s->origin, r->end));
                if (reach_distance < 20)
                    speed = 100;
                else if (!bot_jump_speed(t, r->start, r->end, 0, &speed, e))
                    return false;
            } else if (reach_distance < 20)
                speed = 400 - (256 - 4 * fminf(distance, 64));
            if (!bot_blocked(t, direction, true, out, e))
                return false;
        }
        break;
    case BOT_JUMP:
        return jump(t, r, airborne, out, e);
    case BOT_LADDER:
        direction = qa_vec_normalize(qa_vec_sub(r->end, s->origin));
        out->ideal_view_angles =
            bot_vector_angles(qa_v3(direction.x, direction.y, 3 * direction.z));
        if (!bot_move_action(t, qa_v3(0, 0, 0), 0, e) ||
            !bot_flag_action(t, QA_BOT_MOVE_FORWARD, e))
            return false;
        out->flags |= QA_BOT_MOVE_VIEW;
        out->direction = direction;
        return true;
    case BOT_TELEPORT:
        if (s->flags & QA_BOT_MOVE_TELEPORTED)
            return true;
        offset = s->flags & QA_BOT_MOVE_SWIMMING ? qa_vec_sub(r->start, s->origin)
                                                 : bot_horizontal(s->origin, r->start);
        distance = qa_vec_length(offset);
        direction = qa_vec_normalize(offset);
        if (!bot_blocked(t, direction, true, out, e))
            return false;
        speed = distance < 30 ? 200 : 400;
        if (s->flags & QA_BOT_MOVE_SWIMMING)
            out->flags |= QA_BOT_MOVE_SWIM_VIEW;
        break;
    case BOT_JUMP_PAD:
        if (airborne) {
            bool controlled;
            if (!bot_air_control(t, r->end, &controlled, &direction, &speed, e))
                return false;
            if (!controlled)
                direction = qa_vec_normalize(bot_horizontal(s->origin, r->end));
        } else
            direction = qa_vec_normalize(bot_horizontal(s->origin, r->start));
        if (!bot_blocked(t, direction, true, out, e))
            return false;
        break;
    default:
        return bot_move_fail(e, "source ground travel type has no action routine");
    }
    if (!bot_move_action(t, direction, speed, e))
        return false;
    out->direction = direction;
    return true;
}
