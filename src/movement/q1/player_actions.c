/* Port of src/movement/q1/player-actions.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/player_actions.h"

#include "qa/movement/q1/common.h"

qa_q1_jump_result qa_q1_player_jump(qa_q1_movement_state state, qa_movement_services services) {
  qa_q1_jump_result result = {state, "none"};
  if (qa_q1_flag_test(state.flags, QA_Q1_FLAG_WATERJUMP)) return result;
  if (state.water_level >= 2) {
    const double vertical = state.water_type == QA_Q1_CONTENTS_WATER ? 100 : state.water_type == QA_Q1_CONTENTS_SLIME ? 80 : 50;
    result.state.velocity.z = vertical;
    result.action = "swim";
    return result;
  }
  if (!qa_q1_flag_test(state.flags, QA_Q1_FLAG_ONGROUND) || !qa_q1_flag_test(state.flags, QA_Q1_FLAG_JUMPRELEASED))
    return result;
  result.state.flags = qa_q1_flag_clear(state.flags, QA_Q1_FLAG_ONGROUND | QA_Q1_FLAG_JUMPRELEASED);
  result.state.velocity.z = qa_q1_n_store(&services.numeric, qa_q1_n_add(&services.numeric, state.velocity.z, 270));
  result.action = "jump";
  return result;
}

qa_q1_movement_state qa_q1_check_water_jump(qa_q1_movement_input input, qa_movement_services services,
                                            qa_q1_movement_options options) {
  qa_q1_movement_context context;
  qa_q1_movement_context_init(&context, input, services, options);
  qa_q1_movement_context *c = &context;
  const qa_q1_movement_math *m = &c->math;
  const qa_numeric_operations *n = m->n;
  qa_q1_movement_state s = input.state.as.q1;
  const qa_angle_vectors axes = qa_q1_math_angles(m, s.angles);
  /* progs106 ignores normalize(v_forward)'s return value after zeroing Z. */
  const qa_vec3 forward = qa_q1_math_vec(m, axes.forward.x, axes.forward.y, 0);
  const qa_trace_shape point = {"point", {qa_q1_zero, qa_q1_zero}};
  const qa_vec3 up8 = {0, 0, 8};
  qa_vec3 start = qa_q1_math_add(m, s.origin, up8);
  const qa_trace_result low = qa_q1_context_trace_ex(c, start, qa_q1_math_ma(m, start, 24, forward), &point, "no-monsters");
  if (low.fraction == 1) {
    qa_q1_movement_context_destroy(c);
    return s;
  }
  const qa_vec3 raise = {0, 0, qa_q1_n_subtract(n, qa_q1_context_bounds(c).max.z, 8)};
  start = qa_q1_math_add(m, start, raise);
  const qa_vec3 direction = qa_q1_math_scale(m, qa_q1_trace_source_normal(&low), -50);
  const qa_trace_result high = qa_q1_context_trace_ex(c, start, qa_q1_math_ma(m, start, 24, forward), &point, "no-monsters");
  if (high.fraction != 1) {
    s.water_jump_direction = direction;
    qa_q1_movement_context_destroy(c);
    return s;
  }
  s.flags = qa_q1_flag_clear(qa_q1_flag_set(s.flags, QA_Q1_FLAG_WATERJUMP), QA_Q1_FLAG_JUMPRELEASED);
  s.velocity = qa_q1_math_vec(m, s.velocity.x, s.velocity.y, 225);
  s.water_jump_direction = direction;
  s.teleport_time_seconds = qa_q1_n_store(n, qa_q1_n_add(n, qa_q1_seconds(input.frame.time), 2));
  qa_q1_movement_context_destroy(c);
  return s;
}
