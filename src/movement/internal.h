#ifndef QA_MOVEMENT_INTERNAL_H
#define QA_MOVEMENT_INTERNAL_H

#include "qa/movement.h"
#include <string.h>

typedef struct qa_move_context {
    qa_movement_input *input;
    const qa_movement_services *services;
    qa_movement_result *result;
    qa_movement_state *state;
    qa_movement_command command;
    qa_error *error;
    uint32_t substep, milliseconds;
    uint64_t time_ns;
    float dt;
    bool failed, removed, input_open, phase_state_replaced;
} qa_move_context;

bool qa_move_nq(qa_move_context *);
bool qa_move_nq_prepare(qa_move_context *);
bool qa_move_nq_physics(qa_move_context *);
bool qa_move_qw(qa_move_context *);
bool qa_move_q2(qa_move_context *);
bool qa_move_q2r(qa_move_context *);
bool qa_move_q3(qa_move_context *);

bool qa_move_trace(qa_move_context *, qa_vec3 start, qa_vec3 end, qa_bounds,
                    uint32_t mask, bool world_only, qa_trace_result *);
bool qa_move_trace_q1(qa_move_context *, qa_vec3 start, qa_vec3 end,
                       qa_trace_shape, qa_q1_move_kind, qa_trace_result *);
bool qa_move_contents(qa_move_context *, qa_vec3, int32_t *);
qa_movement_ground qa_move_ground(const qa_trace_result *);
bool qa_move_same_ground(qa_movement_ground, qa_movement_ground);
bool qa_move_phase(qa_move_context *, qa_movement_phase);
bool qa_move_firing(qa_move_context *);
bool qa_move_emit(qa_move_context *, qa_movement_effect);
bool qa_move_contact(qa_move_context *, const qa_trace_result *, bool touch_now, bool unique);
bool qa_move_touch(qa_move_context *, const qa_trace_result *);
bool qa_move_event(qa_move_context *, int32_t event, int32_t parameter);
bool qa_move_animation(qa_move_context *, qa_movement_animation_kind, int32_t value, bool force, bool backwards);
bool qa_move_bounds(qa_move_context *, qa_bounds requested, uint32_t mask, qa_bounds *);
bool qa_move_apply_stance(qa_move_context *);
int32_t qa_move_mode_type(qa_movement_kind, qa_movement_mode);
qa_vec3 qa_move_clip(qa_vec3 velocity, qa_vec3 normal, float overbounce, float stop_epsilon);
void qa_move_angles(qa_vec3 degrees, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up);
float qa_move_angle_mod(float);
float qa_move_short_angle(int32_t);
int16_t qa_move_short(int32_t);
float qa_move_component(qa_vec3, unsigned);
void qa_move_set_component(qa_vec3 *, unsigned, float);

#endif
