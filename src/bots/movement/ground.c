#include "internal.h"

static float centered_random(bot_travel *t) {
    return 2 * ((float)(t->moves->services.random.next(t->moves->services.random.context) & 32767) /
                    32767.0f -
                .5f);
}
static bool jump(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                 qa_error *e) {
    bot_move_record *state = t->state;
    qa_vec3 direction;
    if (airborne) {
        if (!bot_move_word(state,BM_JUMP_REACH))
            return true;
        qa_vec3 offset = bot_horizontal(bot_move_vector(state,BM_ORIGIN), r->end);
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
                if (area != bot_move_word(state,BM_REACH_AREA))
                    break;
            }
            run_start = bot_ma(r->start, (float)scan, direction);
        }
        qa_vec3 from_start = bot_horizontal(r->start, bot_move_vector(state,BM_ORIGIN)),
                from_run = bot_horizontal(run_start, bot_move_vector(state,BM_ORIGIN));
        float start_distance = qa_vec_length(from_start), run_distance = qa_vec_length(from_run);
        if (qa_vec_dot(qa_vec_normalize(from_start), qa_vec_normalize(from_run)) < -.8f ||
            run_distance < 5) {
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(state,BM_ORIGIN), r->end));
            if (start_distance < 32 && !bot_jump_action(t, start_distance >= 24, e))
                return false;
            if (!bot_move_action(t, direction, 600, e))
                return false;
            bot_move_write_word(state,BM_JUMP_REACH,bot_move_word(state,BM_LAST_REACHABILITY));
        } else {
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(state,BM_ORIGIN), run_start));
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
    bot_move_record *s = t->state;
    qa_vec3 offset, direction;
    if (airborne) {
        if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_WATER_JUMP)
            return true;
        qa_vec3 point = bot_move_vector(s,BM_ORIGIN);
        point.z -= 32;
        int32_t contents;
        if (!qa_bot_navigation_contents(t->navigation, point, &contents, e))
            return false;
        if (!(contents & 56))
            return true;
        offset = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
        offset.x += centered_random(t) * 10;
        offset.y += centered_random(t) * 10;
        offset.z += 70 + centered_random(t) * 10;
        direction = qa_vec_normalize(offset);
        if (!bot_move_action(t, direction, 400, e))
            return false;
    } else {
        offset = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
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
    bot_move_record *state = t->state;
    bot_move_record *s = state;
    qa_vec3 direction = {0}, offset;
    float speed = 400, distance;
    *out = (qa_bot_move_result){0};
    switch (r->type & BOT_TRAVEL_MASK) {
    case BOT_WALK: {
        offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start);
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
            offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end);
            distance = qa_vec_length(offset);
            direction = qa_vec_normalize(offset);
        }
        if (waypoint && !bot_blocked(t, direction, true, out, e))
            return false;
        if (!(bot_area_presence(t, r->area) & 2) && distance < 20 &&
            !bot_flag_action(t, QA_BOT_CROUCH, e))
            return false;
        float gap;
        if (!bot_gap_distance_state(t, direction, &gap, e))
            return false;
        if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_WALK) {
            speed = gap > 0 ? 200 - (180 - gap) : 200;
            if (!bot_flag_action(t, QA_BOT_WALK, e))
                return false;
        } else
            speed = gap > 0 ? 400 - (360 - 2 * gap) : 400;
        break;
    }
    case BOT_CROUCH:
        direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
        if (!bot_blocked(t, direction, true, out, e) || !bot_flag_action(t, QA_BOT_CROUCH, e))
            return false;
        break;
    case BOT_BARRIER_JUMP:
        if (airborne) {
            if (bot_move_vector(s,BM_VELOCITY).z >= 250)
                return true;
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
            if (!bot_blocked(t, direction, true, out, e))
                return false;
        } else {
            offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start);
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
        direction = qa_vec_normalize(qa_vec_sub(r->start, bot_move_vector(s,BM_ORIGIN)));
        if (!bot_blocked(t, direction, true, out, e))
            return false;
        out->ideal_view_angles = bot_vector_angles(direction);
        out->flags |= QA_BOT_MOVE_SWIM_VIEW;
        break;
    case BOT_WATER_JUMP:
        return water_jump(t, r, airborne, out, e);
    case BOT_DROP:
        if (airborne) {
            direction = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
            if (!bot_blocked(t, direction, true, out, e))
                return false;
            offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end);
            qa_vec3 end =
                qa_vec_length(offset) > 16 ? bot_ma(r->end, 16, qa_vec_normalize(offset)) : r->end;
            bool controlled;
            if (!bot_air_control(t, end, &controlled, &direction, &speed, e))
                return false;
            if (!controlled)
                direction = qa_vec_normalize(offset);
        } else {
            if (!bot_blocked(t, qa_vec_normalize(qa_vec_sub(r->start, bot_move_vector(s,BM_ORIGIN))), true, out, e))
                return false;
            float reach_distance = qa_vec_length(bot_horizontal(r->start, r->end));
            offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start);
            distance = qa_vec_length(offset);
            direction = qa_vec_normalize(offset);
            if (distance < 48) {
                direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
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
        direction = qa_vec_normalize(qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN)));
        out->ideal_view_angles =
            bot_vector_angles(qa_v3(direction.x, direction.y, 3 * direction.z));
        if (!bot_move_action(t, qa_v3(0, 0, 0), 0, e) ||
            !bot_flag_action(t, QA_BOT_MOVE_FORWARD, e))
            return false;
        out->flags |= QA_BOT_MOVE_VIEW;
        out->direction = direction;
        return true;
    case BOT_TELEPORT:
        if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_TELEPORTED)
            return true;
        offset = bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING ? qa_vec_sub(r->start, bot_move_vector(s,BM_ORIGIN))
                                                 : bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start);
        distance = qa_vec_length(offset);
        direction = qa_vec_normalize(offset);
        if (!bot_blocked(t, direction, true, out, e))
            return false;
        speed = distance < 30 ? 200 : 400;
        if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING)
            out->flags |= QA_BOT_MOVE_SWIM_VIEW;
        break;
    case BOT_JUMP_PAD:
        if (airborne) {
            bool controlled;
            if (!bot_air_control(t, r->end, &controlled, &direction, &speed, e))
                return false;
            if (!controlled)
                direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
        } else
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start));
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
