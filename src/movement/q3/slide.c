#include "local.h"

qa_vec3 q3_clip(qa_vec3 velocity, qa_vec3 normal) {
    float dot = qa_vec_dot(velocity, normal);
    float backoff = dot < 0 ? dot * 1.001f : dot / 1.001f;
    return qa_vec_sub(velocity, qa_vec_scale(normal, backoff));
}

bool q3_trace(qa_q3_step *step, qa_vec3 start, qa_vec3 end, qa_trace_result *trace) {
    return qa_move_trace(step->context, start, end, step->context->result->bounds,
                         step->mask, false, trace);
}

bool q3_contact(qa_q3_step *step, const qa_trace_result *trace) {
    qa_move_context *context = step->context;
    qa_movement_result *result = context->result;
    if (trace->hit != QA_TRACE_HIT_ACTOR || result->contact_count >= 32) return true;
    return qa_move_contact(context, trace, false, true);
}

bool q3_slide(qa_q3_step *step, bool gravity) {
    qa_q3_movement_state *state = q3_state(step);
    qa_vec3 primal = state->velocity, end_velocity = state->velocity;
    if (gravity) {
        end_velocity.z -= q3_gravity(step) * step->dt;
        state->velocity.z = (state->velocity.z + end_velocity.z) * 0.5f;
        primal.z = end_velocity.z;
        if (step->ground_plane) state->velocity = q3_clip(state->velocity, step->ground_normal);
    }
    qa_vec3 planes[5];
    size_t plane_count = 0;
    if (step->ground_plane) planes[plane_count++] = step->ground_normal;
    planes[plane_count++] = qa_vec_normalize(state->velocity);
    float remaining = step->dt;
    bool collided = false;
    for (unsigned bump = 0; bump < 4; ++bump) {
        qa_trace_result trace;
        qa_vec3 end = qa_vec_add(state->origin, qa_vec_scale(state->velocity, remaining));
        if (!q3_trace(step, state->origin, end, &trace)) return true;
        if (trace.all_solid) {
            state->velocity.z = 0;
            return true;
        }
        if (trace.fraction > 0) state->origin = trace.end;
        if (trace.fraction == 1) break;
        collided = true;
        if (!q3_contact(step, &trace)) return true;
        remaining -= remaining * trace.fraction;
        if (plane_count >= 5) {
            state->velocity = qa_v3(0, 0, 0);
            return true;
        }
        if (!trace.contact) {
            qa_error_set(step->context->error, QA_ERROR_FORMAT, 0,
                         "Q3 movement impact requires a collision plane");
            step->context->failed = true;
            return true;
        }
        qa_vec3 normal = trace.contact_plane.normal;
        bool duplicate = false;
        for (size_t i = 0; i < plane_count; ++i) {
            if (qa_vec_dot(normal, planes[i]) > 0.99f) {
                state->velocity = qa_vec_add(state->velocity, normal);
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        planes[plane_count++] = normal;
        for (size_t i = 0; i < plane_count; ++i) {
            float into = qa_vec_dot(state->velocity, planes[i]);
            if (into >= 0.1f) continue;
            step->impact_speed = fmaxf(step->impact_speed, -into);
            qa_vec3 clipped = q3_clip(state->velocity, planes[i]);
            qa_vec3 end_clipped = q3_clip(end_velocity, planes[i]);
            for (size_t j = 0; j < plane_count; ++j) {
                if (j == i || qa_vec_dot(clipped, planes[j]) >= 0.1f) continue;
                clipped = q3_clip(clipped, planes[j]);
                end_clipped = q3_clip(end_clipped, planes[j]);
                if (qa_vec_dot(clipped, planes[i]) >= 0) continue;
                qa_vec3 direction = qa_vec_normalize(qa_vec_cross(planes[i], planes[j]));
                clipped = qa_vec_scale(direction, qa_vec_dot(direction, state->velocity));
                end_clipped = qa_vec_scale(direction, qa_vec_dot(direction, end_velocity));
                for (size_t k = 0; k < plane_count; ++k) {
                    if (k != i && k != j && qa_vec_dot(clipped, planes[k]) < 0.1f) {
                        state->velocity = qa_v3(0, 0, 0);
                        return true;
                    }
                }
            }
            state->velocity = clipped;
            end_velocity = end_clipped;
            break;
        }
    }
    if (gravity) state->velocity = end_velocity;
    if (state->movement_time_ms != 0) state->velocity = primal;
    return collided;
}

void q3_step_slide(qa_q3_step *step, bool gravity) {
    qa_q3_movement_state *state = q3_state(step);
    qa_vec3 start_origin = state->origin, start_velocity = state->velocity;
    if (!q3_slide(step, gravity) || !q3_active(step)) return;
    qa_trace_result ground;
    if (!q3_trace(step, start_origin, qa_vec_add(start_origin, qa_v3(0, 0, -18)), &ground)) return;
    if (state->velocity.z > 0 && (ground.fraction == 1 || !ground.contact ||
                                   ground.contact_plane.normal.z < 0.7f)) return;
    qa_trace_result raised;
    if (!q3_trace(step, start_origin, qa_vec_add(start_origin, qa_v3(0, 0, 18)), &raised) ||
        raised.all_solid) return;
    float step_size = raised.end.z - start_origin.z;
    state->origin = raised.end;
    state->velocity = start_velocity;
    q3_slide(step, gravity);
    if (!q3_active(step)) return;
    qa_trace_result dropped;
    if (!q3_trace(step, state->origin, qa_vec_add(state->origin, qa_v3(0, 0, -step_size)), &dropped)) return;
    if (!dropped.all_solid) state->origin = dropped.end;
    if (dropped.fraction < 1 && dropped.contact)
        state->velocity = q3_clip(state->velocity, dropped.contact_plane.normal);
    float delta = state->origin.z - start_origin.z;
    if (delta > 2)
        qa_move_event(step->context, delta < 7 ? Q3_EV_STEP_4 : delta < 11 ? Q3_EV_STEP_8 :
                                      delta < 15 ? Q3_EV_STEP_12 : Q3_EV_STEP_16, 0);
}
