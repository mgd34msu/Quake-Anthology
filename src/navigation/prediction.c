#include "internal.h"

static bool trace(void *context, const qa_trace_query *query, qa_trace_result *out, qa_error *e) {
    nav_prediction *p = context;
    bool ok = p->supplied.trace != NULL
                  ? p->supplied.trace(p->supplied.context, query, out, e)
                  : qa_world_trace(p->navigation->services.world, query, out, e);
    if (ok) {
        p->has_trace = true;
        p->last_query = *query;
        p->last_trace = *out;
    }
    return ok;
}
static bool contents(void *context, const qa_point_query *query, qa_point_contents *out,
                     qa_error *e) {
    nav_prediction *p = context;
    return p->supplied.point_contents != NULL
               ? p->supplied.point_contents(p->supplied.context, query, out, e)
               : qa_world_point_contents(p->navigation->services.world, query, out, e);
}
static qa_movement_control phase(void *context, qa_movement_phase kind, qa_movement_call *call,
                                 qa_error *e) {
    nav_prediction *p = context;
    return p->supplied.phase == NULL ? QA_MOVEMENT_CONTINUE
                                     : p->supplied.phase(p->supplied.context, kind, call, e);
}
static qa_movement_control touch(void *context, const qa_trace_result *hit, qa_movement_call *call,
                                 qa_error *e) {
    nav_prediction *p = context;
    return p->supplied.touch == NULL ? QA_MOVEMENT_CONTINUE
                                     : p->supplied.touch(p->supplied.context, hit, call, e);
}
static qa_movement_control effect(void *context, const qa_movement_effect *effect,
                                  qa_movement_call *call, qa_error *e) {
    nav_prediction *p = context;
    if (p->input.profile.kind == QA_MOVEMENT_Q3 && effect->kind == QA_MOVE_EFFECT_EVENT &&
        (effect->value == 11 || effect->value == 12))
        p->damaging_fall = true;
    return p->supplied.effect == NULL ? QA_MOVEMENT_CONTINUE
                                      : p->supplied.effect(p->supplied.context, effect, call, e);
}
static bool firing(void *context, const qa_movement_call *call) {
    nav_prediction *p = context;
    return p->supplied.firing != NULL && p->supplied.firing(p->supplied.context, call);
}
static bool is_bsp(void *context, const qa_trace_result *trace, bool *out, qa_error *e) {
    nav_prediction *p = context;
    if (p->supplied.is_bsp != NULL)
        return p->supplied.is_bsp(p->supplied.context, trace, out, e);
    *out = trace->hit == QA_TRACE_HIT_WORLD;
    if (trace->hit != QA_TRACE_HIT_ACTOR) return true;
    qa_actor_collision collision;
    qa_error local = {0};
    if (!qa_world_get_collision(p->navigation->services.world, trace->actor, &collision, &local)) {
        if (e && local.code) *e = local;
        return !local.code;
    }
    *out = collision.inline_model;
    return true;
}
void nav_prediction_close(nav_prediction *p) {
    if (p->has_traversal)
        p->navigation->services.traversal_end(p->navigation->services.context, p->lease);
    if (p->has_lease)
        p->navigation->services.prediction_end(p->navigation->services.context, p->lease);
    qa_movement_result_free(&p->result);
    qa_navigation *n = p->navigation;
    *p = (nav_prediction){.navigation = n};
}
static bool begin(nav_prediction *p, qa_actor_id actor, qa_vec3 origin, qa_error *e) {
    qa_navigation *n = p->navigation;
    if (!nav_services_valid(&n->services, true, e) ||
        !n->services.movement_input(n->services.context, actor, &p->input, e))
        return false;
    if (!qa_actor_id_equal(p->input.actor, actor) ||
        p->input.state.kind != n->graph->view.profile.movement.kind ||
        p->input.profile.kind != n->graph->view.profile.movement.kind) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Navigation prediction does not match the actor's selected movement");
        return false;
    }
    p->input.profile = n->graph->view.profile.movement;
    p->input.prediction = true;
    p->input.has_trace_policy = true;
    p->input.trace_policy = n->graph->view.profile.policy;
    p->input.shape = n->graph->view.profile.shape;
    p->pml_origin = p->input.q2r_pml_origin == NULL ? origin : *p->input.q2r_pml_origin;
    p->input.q2r_pml_origin = &p->pml_origin;
    if (!qa_movement_set_origin(&p->input.state, origin, e) ||
        !qa_movement_set_velocity(&p->input.state, qa_v3(0, 0, 0), e))
        return false;
    if (p->input.state.kind == QA_MOVEMENT_Q3)
        p->input.state.data.q3.movement_flags &= ~UINT32_C(2);
    if (n->services.prediction_begin != NULL) {
        if (!n->services.prediction_begin(n->services.context, actor, &p->supplied, &p->lease, e))
            return false;
        p->has_lease = true;
    }
    p->services = (qa_movement_services){.context = p,
                                         .trace = trace,
                                         .point_contents = contents,
                                         .phase = phase,
                                         .touch = touch,
                                         .effect = effect,
                                         .firing = firing,
                                         .is_bsp = is_bsp};
    p->initialized = true;
    return true;
}
static int32_t signed_word(uint32_t word) {
    int32_t result;
    memcpy(&result, &word, sizeof(result));
    return result;
}
static bool command_vector(nav_prediction *p, qa_vec3 move, uint32_t milliseconds, qa_error *e) {
    qa_movement_input *input = &p->input;
    uint64_t elapsed = (uint64_t)milliseconds * UINT64_C(1000000);
    if (input->time_ns > UINT64_MAX - elapsed || p->sequence == UINT64_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Navigation prediction time/sequence overflow");
        return false;
    }
    float yaw = atan2f(move.y, move.x) * 57.29577951308232f,
          horizontal = fminf(400, hypotf(move.x, move.y));
    float up = fmaxf(-400, fminf(400, move.z));
    int32_t word = (int32_t)floorf(yaw * (65536.0f / 360.0f) + 0.5f) & 65535;
    input->elapsed_ns = elapsed;
    input->time_ns += input->elapsed_ns;
    qa_movement_command c = {.kind = input->profile.kind,
                             .sequence = p->sequence++,
                             .milliseconds = milliseconds,
                             .angles = {0, yaw, 0},
                             .forward_move = horizontal,
                             .up_move = up,
                             .weapon = input->command.weapon};
    c.server_frame = signed_word((uint32_t)c.sequence);
    switch (input->state.kind) {
    case QA_MOVEMENT_NETQUAKE:
        c.acknowledged_server_seconds = (double)input->time_ns / 1000000000;
        c.buttons = up > 0 ? 2 : 0;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        c.buttons = up > 0 ? 2 : 0;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        c.angle_words[0] = -input->state.data.q2.delta_angle_shorts[0];
        c.angle_words[1] = word - input->state.data.q2.delta_angle_shorts[1];
        c.angle_words[2] = -input->state.data.q2.delta_angle_shorts[2];
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        c.angles = qa_vec_sub(c.angles, input->state.data.q2r.delta_angles);
        c.buttons = up > 0 ? 8 : up < 0 ? 16 : 0;
        c.up_move = 0;
        input->snap_initial = true;
        break;
    case QA_MOVEMENT_Q3:
        c.server_time_ms =
            signed_word((uint32_t)input->state.data.q3.command_time_ms + milliseconds);
        c.angle_words[0] = signed_word(0u - (uint32_t)input->state.data.q3.delta_angle_words[0]);
        c.angle_words[1] =
            (int32_t)(((uint32_t)word - (uint32_t)input->state.data.q3.delta_angle_words[1]) &
                      65535);
        c.angle_words[2] = signed_word(0u - (uint32_t)input->state.data.q3.delta_angle_words[2]);
        c.forward_move = floorf(horizontal * 127 / 400 + 0.5f);
        c.up_move = floorf(up * 127 / 400 + 0.5f);
        break;
    }
    input->command = c;
    return true;
}
static bool command(nav_prediction *p, qa_vec3 target, qa_nav_travel mode, qa_error *e) {
    qa_vec3 delta = qa_vec_sub(target, qa_movement_origin(&p->input.state));
    float distance = hypotf(delta.x, delta.y),
          scale = distance == 0 ? 0 : fminf(400, distance * 20) / distance;
    qa_vec3 move =
        qa_v3(delta.x * scale, delta.y * scale,
              mode == QA_NAV_JUMP || mode == QA_NAV_WATER_JUMP || mode == QA_NAV_LADDER ? 400
              : mode == QA_NAV_CROUCH                                                   ? -400
                                                                                        : 0);
    return command_vector(p, move, 16, e);
}
static bool same_vector(qa_vec3 a, qa_vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
static bool same_q2_prediction(const qa_movement_result *a, const qa_movement_result *b) {
    if (a->state.kind != QA_MOVEMENT_Q2_CLASSIC || b->state.kind != QA_MOVEMENT_Q2_CLASSIC)
        return false;
    const qa_q2_movement_state *left = &a->state.data.q2, *right = &b->state.data.q2;
    if (left->type != right->type || left->wide_coordinates != right->wide_coordinates ||
        left->flags != right->flags || left->time_eight_ms != right->time_eight_ms ||
        left->gravity != right->gravity ||
        (left->wide_coordinates && left->wide.time_ms != right->wide.time_ms))
        return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (qa_q2_movement_coordinate(left, false, axis) !=
                qa_q2_movement_coordinate(right, false, axis) ||
            qa_q2_movement_coordinate(left, true, axis) !=
                qa_q2_movement_coordinate(right, true, axis) ||
            left->delta_angle_shorts[axis] != right->delta_angle_shorts[axis])
            return false;
    for (unsigned channel = 0; channel < 4; ++channel)
        if (a->screen_blend[channel] != b->screen_blend[channel]) return false;
    return a->state.kind == b->state.kind && qa_actor_id_equal(a->actor, b->actor) &&
        same_vector(a->bounds.mins, b->bounds.mins) &&
        same_vector(a->bounds.maxs, b->bounds.maxs) &&
        same_vector(a->view_angles, b->view_angles) &&
        same_vector(a->view_offset, b->view_offset) &&
        a->view_height == b->view_height && a->horizontal_speed == b->horizontal_speed &&
        a->ground.hit == b->ground.hit && a->ground.model == b->ground.model &&
        qa_actor_id_equal(a->ground.actor, b->ground.actor) &&
        a->water_level == b->water_level && a->water_type == b->water_type &&
        a->contact_count == b->contact_count && a->effect_count == b->effect_count &&
        a->render_flags == b->render_flags &&
        a->impact_delta == b->impact_delta && a->step_clip == b->step_clip &&
        a->jump_sound == b->jump_sound;
}
static bool same_q2_command(const qa_movement_command *a, const qa_movement_command *b) {
    if (a->kind != b->kind || a->milliseconds != b->milliseconds ||
        !same_vector(a->angles, b->angles) || a->forward_move != b->forward_move ||
        a->side_move != b->side_move || a->up_move != b->up_move ||
        a->buttons != b->buttons || a->impulse != b->impulse ||
        a->light_level != b->light_level || a->weapon != b->weapon)
        return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (a->angle_words[axis] != b->angle_words[axis]) return false;
    return true;
}
bool nav_predict(nav_prediction *p, qa_actor_id actor, qa_vec3 from, qa_vec3 to, qa_nav_travel mode,
                 qa_nav_route *route, bool *admitted, qa_error *e) {
    *admitted = false;
    qa_navigation_services *s = &p->navigation->services;
    if (s->traversal_admit != NULL) {
        if (!p->has_traversal) {
            if (!s->traversal_begin(s->context, actor, &p->lease, e)) return false;
            p->has_traversal = true;
        }
        const qa_vec3 *points = NULL;
        size_t count = 0;
        float seconds = 0;
        if (!s->traversal_admit(s->context, p->lease, from, to, mode,
                &points, &count, &seconds, admitted, e)) return false;
        if (!*admitted) return true;
        if ((count && !points) || !isfinite(seconds) || seconds < 0) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid source traversal trajectory");
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            if (!qa_vec_finite(points[i]) || !nav_route_point(route, points[i], e)) return false;
        }
        route->travel_seconds += seconds;
        return true;
    }
    if (!p->initialized && !begin(p, actor, from, e))
        return false;
    bool fixed_point = p->input.profile.kind == QA_MOVEMENT_Q2_CLASSIC && !p->has_lease &&
        p->supplied.trace == NULL && p->supplied.point_contents == NULL &&
        p->supplied.phase == NULL && p->supplied.touch == NULL &&
        p->supplied.effect == NULL && p->supplied.firing == NULL && p->supplied.is_bsp == NULL;
    float seconds = 0;
    for (unsigned index = 0; index < 512 && seconds < 8; ++index) {
        qa_movement_result previous = p->result;
        qa_movement_command previous_command = p->input.command;
        if (!command(p, to, mode, e))
            return false;
        qa_movement_result *result = &p->result;
        if (!qa_movement_move(&p->input, &p->services, result, e))
            return false;
        if ((p->input.profile.kind == QA_MOVEMENT_Q2_CLASSIC ||
             p->input.profile.kind == QA_MOVEMENT_Q2_RERELEASE) &&
            !qa_movement_apply_q2_contacts(&p->input, &p->services, result, e))
            return false;
        if (result->status != QA_MOVEMENT_ACTIVE)
            return true;
        qa_vec3 origin = qa_movement_origin(&result->state);
        bool grounded = result->ground.hit != QA_TRACE_HIT_NONE || result->water_level > 0 ||
                        mode == QA_NAV_LADDER;
        p->input.state = result->state;
        p->input.current_bounds = result->bounds;
        p->input.has_current_bounds = true;
        if (!nav_route_point(route, origin, e))
            return false;
        seconds += 0.016f;
        if (nav_distance(origin, to) <= 8 && grounded) {
            route->travel_seconds += seconds;
            *admitted = true;
            return true;
        }
        /* Classic PM has no clock input. With no deferred Source callbacks,
         * an unchanged complete state under this command cannot launch again. */
        if (fixed_point && index != 0 && same_q2_prediction(&previous, result) &&
            same_q2_command(&previous_command, &p->input.command))
            return true;
    }
    return true;
}
static bool prediction_stop(qa_navigation *n, qa_nav_workspace *w, const qa_nav_prediction_query *q,
                            const qa_movement_result *result, qa_vec3 start, bool was_grounded,
                            uint32_t frame, bool damaging_fall, qa_vec3 *end, uint32_t *area,
                            uint32_t *events, qa_error *e) {
    const qa_aas_view *aas = qa_nav_asset_aas(n->graph->view.asset);
    if (aas != NULL && (q->stop_events & (512 | 128 | 256 | 4096)) != 0) {
        qa_aas_crossing crossings[20];
        size_t count;
        if (!qa_aas_trace_areas(aas, w->aas, start, *end, crossings, 20, &count, e))
            return false;
        for (size_t i = 0; i < count; ++i) {
            uint32_t contents = (uint32_t)aas->settings[crossings[i].area].contents;
            uint32_t flag = (q->stop_events & 512) != 0 && crossings[i].area == q->stop_area ? 512
                            : (q->stop_events & 128) != 0 && frame != 0 && (contents & 128) != 0
                                ? 128
                            : (q->stop_events & 256) != 0 && (contents & 64) != 0 ? 256
                            : (q->stop_events & 4096) != 0 && (contents & 8) != 0 ? 4096
                                                                                  : 0;
            if (flag != 0) {
                *events = flag;
                *end = crossings[i].point;
                *area = crossings[i].area;
                return true;
            }
        }
    }
    bool found, grounded = result->ground.hit != QA_TRACE_HIT_NONE;
    if (!qa_navigation_area(n, q->actor, *end, area, &found, e))
        return false;
    if (!found)
        *area = QA_NAV_NO_INDEX;
    uint32_t flags = !was_grounded && grounded ? 1 : was_grounded && !grounded ? 2 : 0;
    if (result->water_level != 0) {
        bool q1 = result->state.kind == QA_MOVEMENT_NETQUAKE ||
                  result->state.kind == QA_MOVEMENT_QUAKEWORLD;
        flags |= (q1 ? result->water_type == -4 : (result->water_type & 16) != 0)  ? 8
                 : (q1 ? result->water_type == -5 : (result->water_type & 8) != 0) ? 16
                                                                                   : 4;
    }
    if (found && q->stop_area != QA_NAV_NO_INDEX && *area == q->stop_area)
        flags |= 512 | (!was_grounded && grounded ? 1024 : 0);
    if (damaging_fall)
        flags |= 32;
    *events = flags & q->stop_events;
    return true;
}
bool qa_navigation_predict(qa_navigation *n, qa_nav_workspace *w, const qa_nav_prediction_query *q,
                           qa_nav_prediction_result *out, qa_error *e) {
    if (n == NULL || w == NULL || q == NULL || out == NULL || !qa_vec_finite(q->origin) ||
        !qa_vec_finite(q->velocity) || !qa_vec_finite(q->command_move) ||
        (q->presence != 2 && q->presence != 4) || q->frame_ms == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid selected movement projection query");
        return false;
    }
    qa_vec3 *trajectory = out->trajectory;
    size_t capacity = out->trajectory_capacity;
    *out = (qa_nav_prediction_result){.end = q->origin,
                                      .velocity = q->velocity,
                                      .grounded = q->on_ground,
                                      .end_area = QA_NAV_NO_INDEX,
                                      .trajectory = trajectory,
                                      .trajectory_capacity = capacity};
    if (!nav_reserve((void **)&out->trajectory, &out->trajectory_capacity, 1,
                     sizeof(*out->trajectory), e))
        return false;
    out->trajectory[out->trajectory_count++] = q->origin;
    if (q->maximum_frames == 0)
        return true;
    nav_prediction p = {.navigation = n};
    if (!begin(&p, q->actor, q->origin, e)) {
        nav_prediction_close(&p);
        return false;
    }
    bool ok = qa_movement_set_velocity(&p.input.state, q->velocity, e);
    if (p.input.state.kind == QA_MOVEMENT_Q3)
        p.input.state.data.q3.movement_flags =
            (p.input.state.data.q3.movement_flags & ~UINT32_C(1)) | (q->presence == 4 ? 1 : 0);
    for (uint32_t frame = 0; ok && frame < q->maximum_frames; ++frame) {
        qa_vec3 move = frame < q->command_frames ? q->command_move : qa_v3(0, 0, 0);
        if (q->presence == 4)
            move.z = -400;
        if (!command_vector(&p, move, q->frame_ms, e)) {
            ok = false;
            break;
        }
        qa_movement_result *result = &p.result;
        p.damaging_fall = false;
        if (!qa_movement_move(&p.input, &p.services, result, e)) {
            ok = false;
            break;
        }
        if ((p.input.profile.kind == QA_MOVEMENT_Q2_CLASSIC ||
             p.input.profile.kind == QA_MOVEMENT_Q2_RERELEASE) &&
            !qa_movement_apply_q2_contacts(&p.input, &p.services, result, e)) {
            ok = false;
            break;
        }
        if (result->status != QA_MOVEMENT_ACTIVE) {
            qa_error_set(e, QA_ERROR_ARGUMENT, frame,
                         "Detached movement projection removed its actor");
            ok = false;
            break;
        }
        qa_vec3 start = out->end, end = qa_movement_origin(&result->state);
        bool was_grounded = out->grounded;
        out->grounded = result->ground.hit != QA_TRACE_HIT_NONE;
        out->water_level = result->water_level;
        out->has_bounds = true;
        out->bounds = result->bounds;
        out->velocity = qa_movement_velocity(&result->state);
        ok = prediction_stop(n, w, q, result, start, was_grounded, frame, p.damaging_fall, &end,
                             &out->end_area, &out->stop_event, e);
        p.input.state = result->state;
        p.input.current_bounds = result->bounds;
        p.input.has_current_bounds = true;
        if (!ok)
            break;
        out->end = end;
        out->seconds += (float)q->frame_ms / 1000;
        ++out->frames;
        if (!nav_reserve((void **)&out->trajectory, &out->trajectory_capacity,
                         out->trajectory_count + 1, sizeof(*out->trajectory), e)) {
            ok = false;
            break;
        }
        out->trajectory[out->trajectory_count++] = end;
        if (out->stop_event != 0)
            break;
    }
    out->has_trace = p.has_trace;
    out->last_query = p.last_query;
    out->last_trace = p.last_trace;
    nav_prediction_close(&p);
    return ok;
}
void qa_nav_prediction_result_free(qa_nav_prediction_result *result) {
    if (result == NULL)
        return;
    free(result->trajectory);
    *result = (qa_nav_prediction_result){0};
}
