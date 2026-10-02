#include "internal.h"
#include "qa/text.h"

typedef struct mover_path {
    qa_vec3 start, end, origin;
    bool bobbing, found;
} mover_path;
static bool mover_center(bot_travel *t, const bot_reach *r, qa_vec3 *out, qa_error *e) {
    qa_bot_travel_model model;
    bool found;
    if (!bot_model(t, r->face & 65535, &model, &found, e))
        return false;
    if (!found)
        return bot_move_fail(e, "source mover has no shared entity/model binding");
    qa_vec3 sum = qa_vec_add(model.bounds.mins, model.bounds.maxs);
    *out = qa_v3(model.origin.x + .5f * sum.x, model.origin.y + .5f * sum.y, r->start.z);
    return true;
}
static int32_t signed_half(uint32_t value) {
    uint16_t word = (uint16_t)value;
    int16_t signed_word;
    memcpy(&signed_word, &word, sizeof(word));
    return signed_word;
}
static bool mover_path_read(bot_travel *t, const bot_reach *r, mover_path *path, qa_error *e) {
    *path = (mover_path){.bobbing = (r->type & BOT_TRAVEL_MASK) == BOT_BOBBING};
    if (!path->bobbing)
        return true;
    qa_bot_travel_model model;
    bool found;
    if (!bot_model(t, r->face & 65535, &model, &found, e))
        return false;
    path->found = found;
    if (!found) {
        if (t->moves->services.diagnostic) {
            char number[32], message[96];
            static const char prefix[] = "BotFuncBobStartEnd: no entity with model ";
            if (!qa_format_number(r->face & 65535, number, e))
                return false;
            size_t length = strlen(number);
            memcpy(message, prefix, sizeof(prefix) - 1);
            memcpy(message + sizeof(prefix) - 1, number, length);
            message[sizeof(prefix) - 1 + length] = '\n';
            message[sizeof(prefix) + length] = 0;
            t->moves->services.diagnostic(t->moves->services.context, QA_SCRIPT_INFO, message);
        }
        return true;
    }
    qa_vec3 middle = qa_vec_scale(qa_vec_add(model.bounds.mins, model.bounds.maxs), .5f);
    if (path->bobbing) {
        uint32_t flags = (uint32_t)r->face >> 16;
        float first = (float)signed_half((uint32_t)r->edge >> 16);
        float second = (float)signed_half((uint32_t)r->edge);
        path->start = path->end = path->origin = middle;
        if (flags & 1) {
            path->start.x = first;
            path->end.x = second;
            path->origin.x += model.origin.x;
        } else if (flags & 2) {
            path->start.y = first;
            path->end.y = second;
            path->origin.y += model.origin.y;
        } else {
            path->start.z = first;
            path->end.z = second;
            path->origin.z += model.origin.z;
        }
    }
    return true;
}
static bool mover_finish(bot_travel *t, const bot_reach *r, const mover_path *path,
                         qa_bot_move_result *out, qa_error *e) {
    bot_move_record *s = t->state;
    qa_vec3 direction;
    if (!path->bobbing) {
        qa_vec3 center;
        if (!mover_center(t, r, &center, e))
            return false;
        qa_vec3 bottom = qa_vec_sub(center, bot_move_vector(s,BM_ORIGIN)), top = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
        direction = fabsf(bottom.z) < fabsf(top.z) ? bottom : top;
        return bot_move_action(t, qa_vec_normalize(direction), 300, e);
    }
    if (!path->found)
        return bot_move_fail(e, "bobbing travel consumes absent mover origin");
    direction = qa_vec_sub(path->origin, path->end);
    if (qa_vec_length(direction) < 16) {
        qa_vec3 offset = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
        if (!(bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING))
            offset.z = 0;
        float speed = 360 - (360 - 6 * fminf(qa_vec_length(offset), 60));
        if (speed > 5 && !bot_move_action(t, direction, speed, e))
            return false;
        out->direction = direction;
        if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING)
            out->flags |= QA_BOT_MOVE_SWIM_VIEW;
    } else {
        qa_vec3 center;
        if (!mover_center(t, r, &center, e))
            return false;
        qa_vec3 offset = qa_vec_sub(center, bot_move_vector(s,BM_ORIGIN));
        if (!(bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING))
            offset.z = 0;
        float distance = qa_vec_length(offset);
        if (distance > 5) {
            direction = qa_vec_normalize(offset);
            if (!bot_move_action(t, direction, 400 - (400 - 4 * fminf(distance, 100)), e))
                return false;
            out->direction = direction;
        }
    }
    return true;
}
static bool mover(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                  qa_error *e) {
    mover_path path;
    if (!mover_path_read(t, r, &path, e))
        return false;
    if (airborne)
        return mover_finish(t, r, &path, out, e);
    bot_move_record *s = t->state;
    bool riding;
    if (!bot_on_mover(t, r, &riding, e))
        return false;
    if (riding) {
        if (path.bobbing && !path.found)
            return bot_move_fail(e, "bobbing travel consumes absent mover origin");
        bool at_end = path.bobbing
                          ? qa_vec_length(qa_vec_sub(path.origin, path.end)) < 24
                          : fabsf(truncf(bot_move_vector(s,BM_ORIGIN).z - r->end.z)) < bot_variable(t, BOT_BARRIER);
        if (at_end) {
            qa_vec3 direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
            bool jumped;
            if (!bot_barrier_jump(t, direction, 100, &jumped, e) ||
                (!jumped && !bot_move_action(t, direction, 400, e)))
                return false;
            out->direction = direction;
        } else {
            qa_vec3 center;
            if (!mover_center(t, r, &center, e))
                return false;
            qa_vec3 offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), center);
            float distance = qa_vec_length(offset);
            if (distance > 10) {
                qa_vec3 direction = qa_vec_normalize(offset);
                if (!bot_move_action(t, direction, 400 - (400 - 4 * fminf(distance, 100)), e))
                    return false;
                out->direction = direction;
            }
        }
        return true;
    }
    qa_vec3 direction = qa_vec_sub(r->end, bot_move_vector(s,BM_ORIGIN));
    float distance = qa_vec_length(direction);
    bool swimming = (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING) != 0;
    if (distance < 64) {
        float speed = 360 - (360 - 6 * fminf(distance, 60));
        bool jumped = false;
        if (!swimming && !bot_barrier_jump(t, direction, 50, &jumped, e))
            return false;
        if ((swimming || !jumped) && speed > 5 && !bot_move_action(t, direction, speed, e))
            return false;
        out->direction = direction;
        if (swimming)
            out->flags |= QA_BOT_MOVE_SWIM_VIEW;
        bot_move_write_float(t->state,BM_REACHABILITY_TIME,0);
        return true;
    }
    qa_vec3 start = qa_vec_sub(r->start, bot_move_vector(s,BM_ORIGIN));
    if (!swimming)
        start.z = 0;
    float start_distance = qa_vec_length(start);
    qa_vec3 first = qa_vec_normalize(start);
    bool down;
    if (path.bobbing) {
        if (!path.found)
            return bot_move_fail(e, "bobbing travel consumes absent mover origin");
        down = qa_vec_length(qa_vec_sub(path.origin, path.start)) <= 16;
    } else if (!bot_mover_down(t, r, &down, e))
        return false;
    if (!down) {
        distance = start_distance;
        direction = first;
    } else {
        qa_vec3 middle;
        if (!mover_center(t, r, &middle, e))
            return false;
        qa_vec3 center = qa_vec_sub(middle, bot_move_vector(s,BM_ORIGIN));
        if (!swimming)
            center.z = 0;
        float center_distance = qa_vec_length(center);
        qa_vec3 second = qa_vec_normalize(center);
        if (start_distance < 20 || center_distance < start_distance ||
            qa_vec_dot(first, second) < 0) {
            distance = center_distance;
            direction = second;
        } else {
            distance = start_distance;
            direction = first;
        }
    }
    if (!bot_blocked(t, direction, false, out, e))
        return false;
    float speed =
        down ? 400 - (400 - 6 * fminf(distance, 60)) : 360 - (360 - 6 * fminf(distance, 60));
    if (!swimming) {
        bool jumped;
        if (!bot_barrier_jump(t, direction, 50, &jumped, e))
            return false;
        if (!jumped && (down || speed > 5) && !bot_move_action(t, direction, speed, e))
            return false;
    }
    out->direction = direction;
    if (swimming)
        out->flags |= QA_BOT_MOVE_SWIM_VIEW;
    if (!down) {
        out->type = path.bobbing ? 2 : 1;
        out->flags |= QA_BOT_MOVE_WAITING;
    }
    return true;
}
static bool selected_weapon(bot_travel *t, qa_nav_travel mode, int32_t *out, bool *found,
                            qa_error *e) {
    *found = false;
    return !t->moves->services.travel_weapon ||
           t->moves->services.travel_weapon(t->moves->services.context, bot_move_integer(t->state,BM_CLIENT),
                                            mode, out, found, e);
}
static bool weapon(bot_travel *t, qa_nav_travel mode, int32_t *out, qa_error *e) {
    bool found;
    if (!selected_weapon(t, mode, out, &found, e))
        return false;
    return found || bot_move_fail(e, "selected arsenal cannot execute bot weapon travel");
}
static bool weapon_jump(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                        qa_error *e) {
    bot_move_record *state = t->state;
    bot_move_record *s = state;
    qa_vec3 direction;
    if (airborne) {
        if (!bot_move_word(state,BM_JUMP_REACH))
            return true;
        bool controlled;
        float speed;
        if (!bot_air_control(t, r->end, &controlled, &direction, &speed, e))
            return false;
        if (!controlled)
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
        if (!bot_move_action(t, direction, speed, e))
            return false;
    } else {
        qa_vec3 offset = bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->start);
        float distance = qa_vec_length(offset);
        direction = qa_vec_normalize(offset);
        out->ideal_view_angles = bot_vector_angles(direction);
        out->ideal_view_angles.x = 90;
        if (distance < 5 &&
            fabsf(qa_bot_angle_difference(out->ideal_view_angles.x, bot_move_vector(s,BM_VIEW_ANGLES).x)) < 5 &&
            fabsf(qa_bot_angle_difference(out->ideal_view_angles.y, bot_move_vector(s,BM_VIEW_ANGLES).y)) < 5) {
            direction = qa_vec_normalize(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
            if (!bot_jump_action(t, false, e) || !bot_flag_action(t, QA_BOT_ATTACK, e) ||
                !bot_move_action(t, direction, 800, e))
                return false;
            bot_move_write_word(state,BM_JUMP_REACH,bot_move_word(state,BM_LAST_REACHABILITY));
        } else if (!bot_move_action(t, direction, 400 - (400 - 5 * fminf(distance, 80)), e))
            return false;
        out->ideal_view_angles = bot_vector_angles(direction);
        out->ideal_view_angles.x = 90;
        if (!bot_view_action(t,out->ideal_view_angles,e))
            return false;
        out->flags |= QA_BOT_MOVE_VIEW_SET;
        int32_t selected;
        if (!weapon(t, r->graph_edge->mode, &selected, e) ||
            !bot_weapon_action(t,selected,e) ||
            !weapon(t, r->graph_edge->mode, &out->weapon, e))
            return false;
        out->flags |= QA_BOT_MOVE_WEAPON;
    }
    out->direction = direction;
    return true;
}
static bool grapple_command(bot_travel *t, bool activate, qa_error *e) {
    if (bot_variable(t, BOT_OFFHAND_GRAPPLE) == 0)
        return true;
    const char *command = t->moves->variables[activate ? BOT_GRAPPLE_ON : BOT_GRAPPLE_OFF]->string;
    return bot_text_action(t,command,e);
}
bool bot_reset_grapple(bot_travel *t, qa_error *e) {
    bot_reach reach;
    bool found;
    if (!bot_reach_read(t, bot_move_word(t->state,BM_LAST_REACHABILITY), &reach, &found, e))
        return false;
    bot_move_record *s = t->state;
    if ((!found || (reach.type & BOT_TRAVEL_MASK) != BOT_GRAPPLE_HOOK) &&
        ((bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_ACTIVE_GRAPPLE) || bot_move_float(s,BM_GRAPPLE_VISIBLE_TIME) != 0)) {
        if (!grapple_command(t, false, e))
            return false;
        bot_move_write_word(s,BM_FLAGS,bot_move_word(s,BM_FLAGS) & (~(uint32_t)QA_BOT_MOVE_ACTIVE_GRAPPLE));
        bot_move_write_float(s,BM_GRAPPLE_VISIBLE_TIME,0);
    }
    return true;
}
static bool grapple_state(bot_travel *t, int *out, qa_error *e) {
    *out = 0;
    if (bot_move_word(t->state,BM_FLAGS) & QA_BOT_MOVE_GRAPPLE_PULL) {
        *out = 2;
        return true;
    }
    qa_bot_move_services *services = &t->moves->services;
    if (services->grapple_state) {
        qa_bot_grapple_observation observed;
        if (!services->grapple_state(services->context, bot_move_integer(t->state,BM_CLIENT), &observed, e))
            return false;
        if (observed < QA_BOT_GRAPPLE_NONE || observed > QA_BOT_GRAPPLE_PULLING)
            return bot_move_fail(e, "invalid selected grapple observation");
        *out = (int)observed;
        return true;
    }
    int32_t selected;
    bool found;
    if (!selected_weapon(t, QA_NAV_GRAPPLE, &selected, &found, e))
        return false;
    if (!found)
        return true;
    if (!services->next_entity || !services->entity_type || !services->entity_weapon)
        return true;
    double missile = trunc((double)bot_variable(t, BOT_MISSILE_TYPE));
    for (int32_t entity = services->next_entity(services->context, 0); entity;
         entity = services->next_entity(services->context, entity)) {
        if ((double)services->entity_type(services->context, entity) == missile &&
            services->entity_weapon(services->context, entity) == selected) {
            *out = 1;
            break;
        }
    }
    return true;
}
static bool grapple(bot_travel *t, const bot_reach *r, qa_bot_move_result *out, qa_error *e) {
    bot_move_record *state = t->state;
    bot_move_record *s = state;
    if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_GRAPPLE_RESET) {
        if (!grapple_command(t, false, e))
            return false;
        bot_move_write_word(s,BM_FLAGS,bot_move_word(s,BM_FLAGS) & (~(uint32_t)QA_BOT_MOVE_ACTIVE_GRAPPLE));
        return true;
    }
    bool hand_weapon = truncf(bot_variable(t, BOT_OFFHAND_GRAPPLE)) == 0;
    if (hand_weapon) {
        if (!weapon(t, QA_NAV_GRAPPLE, &out->weapon, e))
            return false;
        out->flags |= QA_BOT_MOVE_WEAPON;
    }
    if (bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_ACTIVE_GRAPPLE) {
        int hook_state;
        if (!grapple_state(t, &hook_state, e))
            return false;
        float distance = qa_vec_length(bot_horizontal(bot_move_vector(s,BM_ORIGIN), r->end));
        bool reset = false;
        if (hook_state && distance < 48)
            reset = bot_move_float(state,BM_LAST_GRAPPLE_DISTANCE) - distance < 1;
        else if (!hook_state || (hook_state == 2 && distance > bot_move_float(state,BM_LAST_GRAPPLE_DISTANCE) - 2))
            reset = bot_move_float(state,BM_GRAPPLE_VISIBLE_TIME) < (double)t->moves->time - .4;
        else
            bot_move_write_float(state,BM_GRAPPLE_VISIBLE_TIME,t->moves->time);
        if (reset) {
            if (!grapple_command(t, false, e))
                return false;
            bot_move_write_word(s,BM_FLAGS,(bot_move_word(s,BM_FLAGS) & ~(uint32_t)QA_BOT_MOVE_ACTIVE_GRAPPLE) | QA_BOT_MOVE_GRAPPLE_RESET);
            bot_move_write_float(state,BM_REACHABILITY_TIME,0);
            return true;
        }
        if (hand_weapon && !bot_flag_action(t, QA_BOT_ATTACK, e))
            return false;
        bot_move_write_float(state,BM_LAST_GRAPPLE_DISTANCE,distance);
        return true;
    }
    bot_move_write_float(state,BM_GRAPPLE_VISIBLE_TIME,t->moves->time);
    qa_vec3 offset = qa_vec_sub(r->start, bot_move_vector(s,BM_ORIGIN));
    if (!(bot_move_word(s,BM_FLAGS) & QA_BOT_MOVE_SWIMMING))
        offset.z = 0;
    qa_vec3 direction = qa_vec_normalize(offset);
    float distance = qa_vec_length(offset);
    qa_vec3 eye = qa_vec_add(bot_move_vector(s,BM_ORIGIN), bot_move_vector(s,BM_VIEW_OFFSET));
    out->ideal_view_angles = bot_vector_angles(qa_vec_sub(r->end, eye));
    out->flags |= QA_BOT_MOVE_VIEW;
    if (distance < 5 &&
        fabsf(qa_bot_angle_difference(out->ideal_view_angles.x, bot_move_vector(s,BM_VIEW_ANGLES).x)) < 2 &&
        fabsf(qa_bot_angle_difference(out->ideal_view_angles.y, bot_move_vector(s,BM_VIEW_ANGLES).y)) < 2) {
        qa_trace_result trace;
        if (!bot_trace(t, eye, r->end, NULL, bot_move_integer(s,BM_ENTITY), 1, &trace, e))
            return false;
        if (qa_vec_length(qa_vec_sub(r->end, trace.end)) > 16) {
            out->failure = true;
            return true;
        }
        if (bot_variable(t, BOT_OFFHAND_GRAPPLE) != 0) {
            if (!grapple_command(t, true, e))
                return false;
        } else if (!bot_flag_action(t, QA_BOT_ATTACK, e))
            return false;
        bot_move_write_word(s,BM_FLAGS,bot_move_word(s,BM_FLAGS) | (QA_BOT_MOVE_ACTIVE_GRAPPLE));
        bot_move_write_float(state,BM_LAST_GRAPPLE_DISTANCE,999999);
    } else {
        float speed = distance < 70 ? 300 - (300 - 4 * distance) : 400;
        if (!bot_blocked(t, direction, true, out, e) || !bot_move_action(t, direction, speed, e))
            return false;
        out->direction = direction;
    }
    uint32_t area;
    if (!qa_bot_navigation_point(t->navigation, bot_move_vector(s,BM_ORIGIN), &area, e))
        return false;
    if (area && area != bot_move_word(state,BM_REACH_AREA))
        bot_move_write_float(state,BM_REACHABILITY_TIME,0);
    return true;
}
bool bot_special_travel(bot_travel *t, const bot_reach *r, bool airborne, qa_bot_move_result *out,
                        qa_error *e) {
    *out = (qa_bot_move_result){0};
    switch (r->type & BOT_TRAVEL_MASK) {
    case BOT_ELEVATOR:
    case BOT_BOBBING:
        return mover(t, r, airborne, out, e);
    case BOT_GRAPPLE_HOOK:
        return grapple(t, r, out, e);
    case BOT_ROCKET_JUMP:
    case BOT_BFG_JUMP:
        return weapon_jump(t, r, airborne, out, e);
    default:
        return bot_move_fail(e, "source special travel type has no action routine");
    }
}
