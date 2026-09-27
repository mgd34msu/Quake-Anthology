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
            !t->moves->services.travel_weapon(t->moves->services.context, t->state->input.client,
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
static bool goal_area(bot_travel *t, const qa_bot_goal *goal, qa_bot_move_result *out,
                      qa_error *e) {
    qa_bot_move_state *state = t->state;
    bool swimming = (state->input.flags & QA_BOT_MOVE_SWIMMING) != 0;
    qa_vec3 delta = qa_vec_sub(goal->origin, state->input.origin);
    if (!swimming)
        delta.z = 0;
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
    state->last_area = 0;
    state->last_goal_area = (uint32_t)goal->area;
    state->last_origin = state->input.origin;
    return true;
}
static bool standing_entity(bot_travel *t, qa_bot_move_result *out, bool *stop, qa_error *e) {
    qa_bot_move_state *state = t->state;
    qa_bot_move_input *s = &state->input;
    qa_vec3 end = s->origin;
    end.z -= 3;
    qa_trace_result trace;
    *stop = false;
    if (!bot_trace_box(t, s->origin, end, s->presence, s->entity, &trace, e))
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
    if (found && (model.kind == QA_BOT_MODEL_ELEVATOR || model.kind == QA_BOT_MODEL_BOBBING)) {
        uint32_t type = model.kind == QA_BOT_MODEL_ELEVATOR ? BOT_ELEVATOR : BOT_BOBBING;
        bot_reach prior;
        bool has_prior;
        if (!bot_reach_read(t, state->last_reachability, &prior, &has_prior, e))
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
                state->reachability_time = t->moves->time + bot_reach_time(&prior);
            }
        }
        if (!*stop)
            out->flags |= model.kind == QA_BOT_MODEL_ELEVATOR ? QA_BOT_MOVE_ON_ELEVATOR
                                                              : QA_BOT_MOVE_ON_BOBBING;
    } else if (found && (model.kind == QA_BOT_MODEL_DOOR || model.kind == QA_BOT_MODEL_TRAIN)) {
        if (!qa_bot_navigation_fuzzy(t->navigation, s->origin, &state->area, e))
            return false;
        *stop = !qa_bot_navigation_area(t->navigation, state->area).reach_count;
    } else
        *stop = true;
    if (*stop) {
        out->blocked = true;
        out->block_entity = entity;
        out->flags |= QA_BOT_MOVE_ON_OBSTACLE;
    }
    return true;
}
static bool goal_grounded(bot_travel *t, const qa_bot_goal *goal, uint32_t flags,
                          qa_bot_move_result *out, bool *finished, qa_error *e) {
    qa_bot_move_state *state = t->state;
    *finished = false;
    if (!qa_bot_navigation_fuzzy(t->navigation, state->input.origin, &state->area, e))
        return false;
    bot_reach prior;
    bool found;
    if (!bot_reach_read(t, state->last_reachability, &prior, &found, e))
        return false;
    if (found && prior.graph_edge->source.kind == QA_NAV_ORIGIN_NAV3 &&
        prior.graph_edge->source_travel_type == 6) {
        bool riding;
        if (!bot_on_mover(t, &prior, &riding, e))
            return false;
        if (riding) {
            state->area = qa_bot_navigation_source_area(t->navigation, prior.graph_edge->from);
            state->reachability_time = t->moves->time + 5;
        }
    }
    if (!state->area) {
        out->failure = true;
        out->blocked = true;
        out->block_entity = 0;
        out->type = 8;
        *finished = true;
        return true;
    }
    if (state->area == (uint32_t)goal->area) {
        *finished = true;
        return goal_area(t, goal, out, e);
    }
    uint32_t number = found ? state->last_reachability : 0;
    if (number) {
        bool allowed = false;
        if ((qa_nav_aas_travel_flag(prior.type) & flags) &&
            !qa_navigation_edge_allowed(t->runtime, t->actor, prior.graph_edge->id, &allowed, e))
            return false;
        uint32_t type = prior.type & BOT_TRAVEL_MASK;
        if (!allowed)
            number = 0;
        else if (type == BOT_GRAPPLE_HOOK) {
            if (state->reachability_time < t->moves->time ||
                (state->input.flags & QA_BOT_MOVE_GRAPPLE_RESET))
                number = 0;
        } else if (type == BOT_ELEVATOR || type == BOT_BOBBING) {
            if (out->flags & QA_BOT_MOVE_ON_BOBBING)
                state->reachability_time = t->moves->time + 5;
            if (state->area == prior.area || state->reachability_time < t->moves->time)
                number = 0;
        } else if (state->last_goal_area != (uint32_t)goal->area ||
                   state->reachability_time < t->moves->time || state->last_area != state->area)
            number = 0;
    }
    uint32_t result_flags = 0;
    if (!number) {
        state->walk_progress = false;
        if (!bot_reach_select(t, goal, flags, flags, &number, &result_flags, e))
            return false;
        state->reach_area = state->area;
        state->jump_reach = 0;
        state->input.flags &= ~QA_BOT_MOVE_GRAPPLE_RESET;
        if (!bot_reach_read(t, number, &prior, &found, e))
            return false;
        if (found) {
            state->reachability_time = t->moves->time + bot_reach_time(&prior);
            bot_move_avoid(t->moves, state, number, 6);
        }
    }
    bot_move_set_reach(state, number);
    state->last_goal_area = (uint32_t)goal->area;
    state->last_area = state->area;
    if (!bot_reach_read(t, number, &prior, &found, e))
        return false;
    if (!found)
        out->failure = true;
    else {
        qa_bot_move_result moved;
        bool changed;
        if (!travel(t, &prior, false, &moved, &changed, e))
            return false;
        if (changed)
            *out = moved;
        out->travel_type = (int32_t)prior.type;
    }
    out->flags |= result_flags;
    return true;
}
static bool goal_airborne(bot_travel *t, const qa_bot_goal *goal, uint32_t flags,
                          qa_bot_move_result *out, qa_error *e) {
    qa_bot_move_state *state = t->state;
    qa_vec3 end = bot_ma(state->input.origin, -2 * state->input.think_time, state->input.velocity);
    qa_aas_crossing areas[16];
    size_t count;
    if (!qa_bot_navigation_trace_areas(t->navigation, state->input.origin, end, areas, 16, &count,
                                       e))
        return false;
    for (size_t i = count; i; --i) {
        uint32_t area = areas[i - 1].area;
        if (!(qa_bot_navigation_area(t->navigation, area).contents & 128))
            continue;
        uint32_t previous = state->area, number, unused;
        state->area = area;
        bool ok = bot_reach_select(t, goal, flags, 0x40000, &number, &unused, e);
        state->area = previous;
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
            state->last_area = area;
            break;
        }
    }
    bot_reach reach;
    bool found;
    if (!bot_reach_read(t, state->last_reachability, &reach, &found, e))
        return false;
    if (found) {
        qa_bot_move_result moved;
        bool changed;
        if (!travel(t, &reach, true, &moved, &changed, e))
            return false;
        if (changed)
            *out = moved;
        out->travel_type = (int32_t)reach.type;
    }
    return true;
}
static bool move_goal(bot_travel *t, const qa_bot_goal *goal, uint32_t flags,
                      qa_bot_move_result *out, qa_error *e) {
    qa_bot_move_state *state = t->state;
    if (!bot_reset_grapple(t, e))
        return false;
    if (!goal) {
        out->failure = true;
        return true;
    }
    state->input.flags &= ~(QA_BOT_MOVE_SWIMMING | QA_BOT_MOVE_AGAINST_LADDER);
    bool grounded;
    if (!bot_on_ground(t, &grounded, e))
        return false;
    if (grounded)
        state->input.flags |= QA_BOT_MOVE_ON_GROUND;
    if (state->input.flags & QA_BOT_MOVE_ON_GROUND) {
        bool stop;
        if (!standing_entity(t, out, &stop, e))
            return false;
        if (stop)
            return true;
    }
    bool swimming, ladder;
    if (!qa_bot_navigation_swimming(t->navigation, state->input.origin, &swimming, e))
        return false;
    if (swimming)
        state->input.flags |= QA_BOT_MOVE_SWIMMING;
    if (!bot_against_ladder(t, &ladder, e))
        return false;
    if (ladder)
        state->input.flags |= QA_BOT_MOVE_AGAINST_LADDER;
    if (state->input.flags &
        (QA_BOT_MOVE_ON_GROUND | QA_BOT_MOVE_SWIMMING | QA_BOT_MOVE_AGAINST_LADDER)) {
        bool finished;
        if (!goal_grounded(t, goal, flags, out, &finished, e))
            return false;
        if (finished)
            return true;
    } else if (!goal_airborne(t, goal, flags, out, e))
        return false;
    if (out->blocked)
        state->reachability_time -= 10 * state->input.think_time;
    state->last_origin = state->input.origin;
    return true;
}
bool qa_bot_moves_goal(qa_bot_moves *m, uint32_t handle, const qa_bot_goal *goal, uint32_t flags,
                       qa_bot_move_result *out, qa_error *e) {
    if (!bot_move_mutable(m, e))
        return false;
    if (!out || (goal && !qa_vec_finite(goal->origin)))
        return bot_move_fail(e, "invalid bot move goal/output");
    out->failure = false;
    out->type = 0;
    out->blocked = false;
    out->block_entity = 0;
    out->travel_type = 0;
    out->flags = 0;
    qa_bot_move_state *state = bot_move_state(m, handle, e);
    if (!state) {
        out->failure = true;
        return false;
    }
    m->busy = true;
    bot_travel t;
    bool ok = bot_travel_begin(m, state, &t, e) && move_goal(&t, goal, flags, out, e);
    m->busy = false;
    return ok;
}
