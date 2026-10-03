#include "internal.h"

static bool travel(bot_travel *t, const bot_reach *reach, bool airborne, qa_bot_move_result *out,
                   bool *changed, qa_error *e) {
    *changed = true;
    qa_nav_travel mode = reach->graph_edge->mode;
    if (!(t->graph->profile.capabilities & QA_NAV_CAPABILITY(mode))) {
        *out = (qa_bot_move_result){.failure = true};
        return true;
    }
    bool weapon_required =
        mode == QA_NAV_ROCKET_JUMP || mode == QA_NAV_BFG_JUMP ||
        (mode == QA_NAV_GRAPPLE && truncf(bot_variable(t, BOT_OFFHAND_GRAPPLE)) == 0);
    if (weapon_required) {
        int32_t weapon;
        bool found = false;
        if (t->moves->services.travel_weapon &&
            !t->moves->services.travel_weapon(t->moves->services.context, bot_move_integer(t->state,BM_CLIENT),
                                              mode, &weapon, &found, e))
            return false;
        if (!found) {
            *out = (qa_bot_move_result){.failure = true};
            return true;
        }
    }
    uint32_t type = reach->type & BOT_TRAVEL_MASK;
    if ((type == BOT_CROUCH || type == BOT_TELEPORT) && airborne) {
        *changed = false;
        return true;
    }
    if (type == BOT_WALK || type == BOT_ELEVATOR) {
        const qa_nav_edge *edge;
        qa_nav_entity_state platform;
        bool found;
        uint32_t area = type == BOT_WALK ? reach->graph_edge->to : reach->graph_edge->from;
        if (!qa_navigation_boarding_elevator(t->runtime, area, &edge, &platform, &found, e))
            return false;
        if (found && platform.data.elevator.phase != QA_NAV_MOVER_BOTTOM &&
            (type != BOT_WALK || !airborne)) {
            bot_reach elevator = *reach;
            if (type == BOT_WALK && !bot_reach_describe(t, edge, &elevator, e))
                return false;
            bool riding, down;
            if (!bot_on_mover(t, &elevator, &riding, e))
                return false;
            if (!riding) {
                if (!bot_mover_down(t, &elevator, &down, e))
                    return false;
                if (!down) {
                    elevator.start = type == BOT_WALK              ? reach->start
                                     : reach->graph_edge->has_hint ? reach->graph_edge->hint.funnel
                                                                   : reach->start;
                    return bot_special_travel(t, &elevator, airborne, out, e);
                }
            }
        }
    }
    switch (type) {
    case BOT_ELEVATOR:
    case BOT_BOBBING:
    case BOT_GRAPPLE_HOOK:
    case BOT_ROCKET_JUMP:
    case BOT_BFG_JUMP:
        return bot_special_travel(t, reach, airborne, out, e);
    default:
        return bot_ground_travel(t, reach, airborne, out, e);
    }
}
static bool goal_area(bot_travel *t, const qa_bot_move_goal_source *goal, qa_bot_move_result *out,
                      qa_error *e) {
    bot_move_record *state = t->state;
    bool swimming = (bot_move_word(state,BM_FLAGS) & QA_BOT_MOVE_SWIMMING) != 0;
    qa_bot_vector_source origin = bot_goal_origin(goal);
    qa_vec3 delta = {0};
    if (!qa_bot_vector_component(&origin, 0, &delta.x, e) ||
        !qa_bot_vector_component(&origin, 1, &delta.y, e) ||
        (swimming && !qa_bot_vector_component(&origin, 2, &delta.z, e))) return false;
    delta.x -= bot_move_vector(state,BM_ORIGIN).x;
    delta.y -= bot_move_vector(state,BM_ORIGIN).y;
    if (swimming) delta.z -= bot_move_vector(state,BM_ORIGIN).z;
    qa_vec3 direction = qa_vec_normalize(delta);
    float speed = 400 - (400 - 4 * fminf(qa_vec_length(delta), 100));
    if (speed < 10)
        speed = 0;
    *out = (qa_bot_move_result){.travel_type = swimming ? BOT_SWIM : BOT_WALK};
    if (!bot_blocked(t, direction, true, out, e) || !bot_move_action(t, direction, speed, e))
        return false;
    out->direction = direction;
    if (swimming) {
        out->ideal_view_angles = bot_vector_angles(direction);
        out->flags |= QA_BOT_MOVE_SWIM_VIEW;
    }
    bot_move_set_reach(state, 0);
    bot_move_write_word(state,BM_LAST_AREA,0);
    uint32_t saved_goal;
    if (!bot_goal_area(goal, &saved_goal, e)) return false;
    bot_move_write_word(state,BM_LAST_GOAL_AREA,saved_goal);
    bot_move_write_vector(state,BM_LAST_ORIGIN,bot_move_vector(state,BM_ORIGIN));
    return true;
}
static bool standing_entity(bot_travel *t, const qa_bot_move_result_io *out, bool *stop, qa_error *e) {
    bot_move_record *state = t->state;
    bot_move_record *s = state;
    qa_vec3 end = bot_move_vector(s,BM_ORIGIN);
    end.z -= 3;
    qa_trace_result trace;
    *stop = false;
    if (!bot_trace_box(t, bot_move_vector(s,BM_ORIGIN), end, bot_move_word(s,BM_PRESENCE), bot_move_integer(s,BM_ENTITY), &trace, e))
        return false;
    int32_t entity = bot_trace_entity(t, &trace);
    if (trace.start_solid || trace.all_solid || entity == 1022 || entity == 1023)
        return true;
    int32_t model_number = t->moves->services.entity_model
                               ? t->moves->services.entity_model(t->moves->services.context, entity)
                               : 0;
    qa_bot_travel_model model;
    bool found;
    if (!bot_model(t, model_number, &model, &found, e))
        return false;
    bool static_ground = false;
    if ((!found || model.kind == QA_BOT_MODEL_STATIC) && t->moves->services.static_ground &&
        !t->moves->services.static_ground(t->moves->services.context, entity, &static_ground, e))
        return false;
    if (found && (model.kind == QA_BOT_MODEL_ELEVATOR || model.kind == QA_BOT_MODEL_BOBBING)) {
        uint32_t type = model.kind == QA_BOT_MODEL_ELEVATOR ? BOT_ELEVATOR : BOT_BOBBING;
        bot_reach prior;
        bool has_prior;
        if (!bot_reach_read(t, bot_move_word(state,BM_LAST_REACHABILITY), &prior, &has_prior, e))
            return false;
        if (!has_prior || (prior.type & BOT_TRAVEL_MASK) != type ||
            (prior.face & 65535) != model_number) {
            const qa_nav_edge *edge = NULL;
            for (size_t i = 0; i < t->graph->edge_count; ++i) {
                const qa_nav_edge *candidate = &t->graph->edges[i];
                if (candidate->mode == QA_NAV_MOVER && candidate->has_entity &&
                    candidate->entity.has_model && candidate->entity.model == model_number) {
                    edge = candidate;
                    break;
                }
            }
            if (!edge)
                *stop = true;
            else {
                if (!bot_reach_describe(t, edge, &prior, e))
                    return false;
                bot_move_set_reach(state, prior.number);
                bot_move_write_float(state,BM_REACHABILITY_TIME,t->moves->time + bot_reach_time(&prior));
            }
        }
        if (!*stop && !bot_result_flags(out, model.kind == QA_BOT_MODEL_ELEVATOR ?
            QA_BOT_MOVE_ON_ELEVATOR : QA_BOT_MOVE_ON_BOBBING, e)) return false;
    } else if (static_ground || (found && (model.kind == QA_BOT_MODEL_DOOR || model.kind == QA_BOT_MODEL_TRAIN))) {
        uint32_t observed_area;
        if (!qa_bot_navigation_fuzzy(t->navigation, bot_move_vector(s,BM_ORIGIN), &observed_area, e))
            return false;
        bot_move_write_word(state,BM_AREA,observed_area);
        *stop = !qa_bot_navigation_area(t->navigation, bot_move_word(state,BM_AREA)).reach_count;
    } else
        *stop = true;
    if (*stop) {
        if (!bot_result_write(out, QA_BOT_RESULT_BLOCKED, 1, e) ||
            !bot_result_write(out, QA_BOT_RESULT_BLOCK_ENTITY, entity, e) ||
            !bot_result_flags(out, QA_BOT_MOVE_ON_OBSTACLE, e)) return false;
    }
    return true;
}
static bool goal_grounded(bot_travel *t, const qa_bot_move_goal_source *goal, uint32_t flags,
                          const qa_bot_move_result_io *out, bool *finished, qa_error *e) {
    bot_move_record *state = t->state;
    *finished = false;
    uint32_t observed_area;
    if (!qa_bot_navigation_fuzzy(t->navigation, bot_move_vector(state,BM_ORIGIN), &observed_area, e))
        return false;
    bot_move_write_word(state,BM_AREA,observed_area);
    bot_reach prior;
    bool found;
    if (!bot_reach_read(t, bot_move_word(state,BM_LAST_REACHABILITY), &prior, &found, e))
        return false;
    if (found && prior.graph_edge->source.kind == QA_NAV_ORIGIN_NAV3 &&
        prior.graph_edge->source_travel_type == 6) {
        bool riding;
        if (!bot_on_mover(t, &prior, &riding, e))
            return false;
        if (riding) {
            bot_move_write_word(state,BM_AREA,qa_bot_navigation_source_area(t->navigation, prior.graph_edge->from));
            bot_move_write_float(state,BM_REACHABILITY_TIME,t->moves->time + 5);
        }
    }
    if (!bot_move_word(state,BM_AREA)) {
        *finished = true;
        return bot_result_write(out, QA_BOT_RESULT_FAILURE, 1, e) &&
            bot_result_write(out, QA_BOT_RESULT_BLOCKED, 1, e) &&
            bot_result_write(out, QA_BOT_RESULT_BLOCK_ENTITY, 0, e) &&
            bot_result_write(out, QA_BOT_RESULT_TYPE, 8, e);
    }
    uint32_t target_area;
    if (!bot_goal_area(goal, &target_area, e)) return false;
    if (bot_move_word(state,BM_AREA) == target_area) {
        *finished = true;
        qa_bot_move_result moved;
        return goal_area(t, goal, &moved, e) && bot_result_copy(out, &moved, e);
    }
    uint32_t number = found ? bot_move_word(state,BM_LAST_REACHABILITY) : 0;
    if (number) {
        bool allowed = false;
        if ((qa_nav_aas_travel_flag(prior.type) & flags) &&
            !qa_navigation_edge_allowed(t->runtime, t->actor, prior.graph_edge->id, &allowed, e))
            return false;
        uint32_t type = prior.type & BOT_TRAVEL_MASK;
        if (!allowed)
            number = 0;
        else if (type == BOT_GRAPPLE_HOOK) {
            if (bot_move_float(state,BM_REACHABILITY_TIME) < t->moves->time ||
                (bot_move_word(state,BM_FLAGS) & QA_BOT_MOVE_GRAPPLE_RESET))
                number = 0;
        } else if (type == BOT_ELEVATOR || type == BOT_BOBBING) {
            int32_t result_flags;
            if (!bot_result_read(out, QA_BOT_RESULT_FLAGS, &result_flags, e)) return false;
            if ((uint32_t)result_flags & QA_BOT_MOVE_ON_BOBBING)
                bot_move_write_float(state,BM_REACHABILITY_TIME,t->moves->time + 5);
            if (bot_move_word(state,BM_AREA) == prior.area || bot_move_float(state,BM_REACHABILITY_TIME) < t->moves->time)
                number = 0;
        } else {
            if (!bot_goal_area(goal, &target_area, e)) return false;
            if (bot_move_word(state,BM_LAST_GOAL_AREA) != target_area || bot_move_float(state,BM_REACHABILITY_TIME) < t->moves->time ||
                bot_move_word(state,BM_LAST_AREA) != bot_move_word(state,BM_AREA)) number = 0;
        }
    }
    uint32_t result_flags = 0;
    if (!number) {
        state->walk_progress = false;
        if (!bot_reach_select(t, goal, flags, flags, &number, &result_flags, e))
            return false;
        bot_move_write_word(state,BM_REACH_AREA,bot_move_word(state,BM_AREA));
        bot_move_write_word(state,BM_JUMP_REACH,0);
        bot_move_write_word(state,BM_FLAGS,bot_move_word(state,BM_FLAGS) & (~(uint32_t)QA_BOT_MOVE_GRAPPLE_RESET));
        if (!bot_reach_read(t, number, &prior, &found, e))
            return false;
        if (found) {
            bot_move_write_float(state,BM_REACHABILITY_TIME,t->moves->time + bot_reach_time(&prior));
            bot_move_avoid(t->moves, state, number, 6);
        }
    }
    bot_move_set_reach(state, number);
    uint32_t saved_goal;
    if (!bot_goal_area(goal, &saved_goal, e)) return false;
    bot_move_write_word(state,BM_LAST_GOAL_AREA,saved_goal);
    bot_move_write_word(state,BM_LAST_AREA,bot_move_word(state,BM_AREA));
    if (!bot_reach_read(t, number, &prior, &found, e))
        return false;
    if (!found) {
        if (!bot_result_write(out, QA_BOT_RESULT_FAILURE, 1, e)) return false;
    }
    else {
        qa_bot_move_result moved;
        bool changed;
        if (!travel(t, &prior, false, &moved, &changed, e))
            return false;
        if (changed && !bot_result_copy(out, &moved, e)) return false;
        int32_t type;
        memcpy(&type, &prior.type, sizeof(type));
        if (!bot_result_write(out, QA_BOT_RESULT_TRAVEL_TYPE, type, e)) return false;
    }
    return bot_result_flags(out, result_flags, e);
}
static bool goal_airborne(bot_travel *t, const qa_bot_move_goal_source *goal, uint32_t flags,
                          const qa_bot_move_result_io *out, qa_error *e) {
    bot_move_record *state = t->state;
    qa_vec3 end = bot_ma(bot_move_vector(state,BM_ORIGIN), -2 * bot_move_float(state,BM_THINK_TIME), bot_move_vector(state,BM_VELOCITY));
    qa_aas_crossing areas[16];
    size_t count;
    if (!qa_bot_navigation_trace_areas(t->navigation, bot_move_vector(state,BM_ORIGIN), end, areas, 16, &count,
                                       e))
        return false;
    for (size_t i = count; i; --i) {
        uint32_t area = areas[i - 1].area;
        if (!(qa_bot_navigation_area(t->navigation, area).contents & 128))
            continue;
        uint32_t previous = bot_move_word(state,BM_AREA), number, unused;
        bot_move_write_word(state,BM_AREA,area);
        bool ok = bot_reach_select(t, goal, flags, 0x40000, &number, &unused, e);
        bot_move_write_word(state,BM_AREA,previous);
        if (!ok)
            return false;
        if (!number) {
            uint32_t node = qa_bot_navigation_node(t->navigation, area);
            size_t outgoing = qa_navigation_outgoing_count(t->runtime, node);
            for (size_t j = 0; j < outgoing; ++j) {
                const qa_nav_edge *edge = qa_navigation_outgoing(t->runtime, node, j);
                bot_reach reach;
                if (!bot_reach_describe(t, edge, &reach, e))
                    return false;
                if ((reach.type & BOT_TRAVEL_MASK) == BOT_JUMP_PAD) {
                    number = reach.number;
                    break;
                }
            }
        }
        if (number) {
            bot_move_set_reach(state, number);
            bot_move_write_word(state,BM_LAST_AREA,area);
            break;
        }
    }
    bot_reach reach;
    bool found;
    if (!bot_reach_read(t, bot_move_word(state,BM_LAST_REACHABILITY), &reach, &found, e))
        return false;
    if (found) {
        qa_bot_move_result moved;
        bool changed;
        if (!travel(t, &reach, true, &moved, &changed, e))
            return false;
        if (changed && !bot_result_copy(out, &moved, e)) return false;
        int32_t type;
        memcpy(&type, &reach.type, sizeof(type));
        if (!bot_result_write(out, QA_BOT_RESULT_TRAVEL_TYPE, type, e)) return false;
    }
    return true;
}
static bool move_goal(bot_travel *t, const qa_bot_move_goal_source *goal, uint32_t flags,
                      const qa_bot_move_result_io *out, qa_error *e) {
    bot_move_record *state = t->state;
    if (!bot_reset_grapple(t, e))
        return false;
    if (!goal) {
        return bot_result_write(out, QA_BOT_RESULT_FAILURE, 1, e);
    }
    bot_move_write_word(state,BM_FLAGS,bot_move_word(state,BM_FLAGS) & (~(uint32_t)(QA_BOT_MOVE_SWIMMING | QA_BOT_MOVE_AGAINST_LADDER)));
    bool grounded;
    if (!bot_on_ground(t, &grounded, e))
        return false;
    if (grounded)
        bot_move_write_word(state,BM_FLAGS,bot_move_word(state,BM_FLAGS) | (QA_BOT_MOVE_ON_GROUND));
    if (bot_move_word(state,BM_FLAGS) & QA_BOT_MOVE_ON_GROUND) {
        bool stop;
        if (!standing_entity(t, out, &stop, e))
            return false;
        if (stop)
            return true;
    }
    bool swimming, ladder;
    if (!qa_bot_navigation_swimming(t->navigation, bot_move_vector(state,BM_ORIGIN), &swimming, e))
        return false;
    if (swimming)
        bot_move_write_word(state,BM_FLAGS,bot_move_word(state,BM_FLAGS) | (QA_BOT_MOVE_SWIMMING));
    if (!bot_against_ladder(t, &ladder, e))
        return false;
    if (ladder)
        bot_move_write_word(state,BM_FLAGS,bot_move_word(state,BM_FLAGS) | (QA_BOT_MOVE_AGAINST_LADDER));
    if (bot_move_word(state,BM_FLAGS) &
        (QA_BOT_MOVE_ON_GROUND | QA_BOT_MOVE_SWIMMING | QA_BOT_MOVE_AGAINST_LADDER)) {
        bool finished;
        if (!goal_grounded(t, goal, flags, out, &finished, e))
            return false;
        if (finished)
            return true;
    } else if (!goal_airborne(t, goal, flags, out, e))
        return false;
    int32_t blocked;
    if (!bot_result_read(out, QA_BOT_RESULT_BLOCKED, &blocked, e)) return false;
    if (blocked)
        bot_move_write_float(state,BM_REACHABILITY_TIME,bot_move_float(state,BM_REACHABILITY_TIME) - (10 * bot_move_float(state,BM_THINK_TIME)));
    bot_move_write_vector(state,BM_LAST_ORIGIN,bot_move_vector(state,BM_ORIGIN));
    return true;
}
bool qa_bot_moves_goal(qa_bot_moves *m, uint32_t handle, const qa_bot_goal *goal, uint32_t flags,
                       qa_bot_move_result *out, qa_error *e) {
    if (!out || (goal && !qa_vec_finite(goal->origin)))
        return bot_move_fail(e, "invalid bot move goal/output");
    qa_bot_move_goal_source source = {.value = goal};
    qa_bot_move_result_io target = {.value = out};
    return qa_bot_moves_goal_from(m, handle, goal ? &source : NULL, flags, &target, e);
}
static bool qa_bot_moves_goal_from_operation(qa_bot_moves *m, uint32_t handle,
                            const qa_bot_move_goal_source *goal, uint32_t flags,
                            const qa_bot_move_result_io *out, qa_error *e) {
    if (!bot_move_mutable(m, e)) return false;
    if (!out || (!out->value && (!out->read || !out->write || !out->write_vector)) ||
        (goal && !goal->value && (!goal->area || (!goal->origin.value && !goal->origin.read))))
        return bot_move_fail(e, "missing bot move goal/result fields");
    m->busy = true;
    bool ok;
    bot_move_record *state = bot_move_source_state(m, handle);
    if (!state) {
        ok=bot_result_write(out, QA_BOT_RESULT_FAILURE, 1, e);
        goto done;
    }
    ok=bot_result_clear(out,e);
    if(!ok) goto done;
    bot_travel t;
    ok = bot_travel_ready(m, e) && bot_travel_begin(m, state, &t, e) &&
        move_goal(&t, goal, flags, out, e);
done:
    m->busy = false;
    return ok;
}

bool qa_bot_moves_goal_from(qa_bot_moves *m, uint32_t handle, const qa_bot_move_goal_source *goal, uint32_t flags, const qa_bot_move_result_io *out, qa_error *e) {
    BOT_MOVE_OPERATION(m,e,qa_bot_moves_goal_from_operation(m,handle,goal,flags,out,e));
}
