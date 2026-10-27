/* Port of src/movement/q1/result.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/result.h"

#include <string.h>

#include "qa/foundation/support.h"

static const char *kind_name(qa_movement_state_kind kind) {
  return kind == QA_MOVEMENT_STATE_Q1 ? "q1-netquake" : "q1-quakeworld";
}

static qa_vec3 state_velocity(const qa_movement_state *state) {
  return state->kind == QA_MOVEMENT_STATE_Q1 ? state->as.q1.velocity : state->as.qw.velocity;
}

qa_movement_result qa_q1_finish_movement(qa_q1_movement_context *context, qa_movement_state state,
                                         qa_vec3 view_angles, qa_trace_hit ground, double water_level,
                                         double water_type) {
  const qa_q1_player_input *input = &context->input;
  state = qa_q1_context_source_state(context, state);
  if (state.kind != QA_MOVEMENT_STATE_Q1 && state.kind != QA_MOVEMENT_STATE_QW)
    qa_range_error("Source client output changed movement dialect");
  qa_weapon_step_input weapon_input;
  memset(&weapon_input, 0, sizeof weapon_input);
  weapon_input.actor = input->actor;
  weapon_input.command = input->command;
  weapon_input.frame = input->frame;
  weapon_input.arsenal = input->arsenal;
  weapon_input.animation = input->animation;
  weapon_input.environment = input->environment;
  weapon_input.gauntlet_hit = false;
  const qa_weapon_step_result weapon = context->services.weapon_step(context->services.ctx, weapon_input, state);
  for (size_t i = 0; i < weapon.effect_count; i++) qa_q1_context_effect(context, weapon.effects[i]);
  const qa_movement_continuation *continuation = weapon.continuation;
  if (continuation != NULL && !continuation->is_continue) {
    qa_movement_result result;
    memset(&result, 0, sizeof result);
    result.kind = kind_name(state.kind);
    result.status = "actor-removed";
    result.actor = input->actor.id;
    result.command_sequence = input->command_sequence;
    qa_q1_context_take_effects(context, &result);
    return result;
  }
  if (continuation != NULL) {
    const qa_movement_state next = continuation->state;
    if ((next.kind != QA_MOVEMENT_STATE_Q1 && next.kind != QA_MOVEMENT_STATE_QW) || next.kind != state.kind)
      qa_range_error("Weapon callback changed movement family");
    state = next;
    view_angles = next.kind == QA_MOVEMENT_STATE_Q1 ? next.as.q1.view_angles : next.as.qw.angles;
    ground = next.kind == QA_MOVEMENT_STATE_Q1 ? next.as.q1.ground : next.as.qw.ground;
    if (next.kind == QA_MOVEMENT_STATE_Q1) {
      water_level = next.as.q1.water_level;
      water_type = next.as.q1.water_type;
    }
  }
  const double horizontal_speed = qa_q1_math_horizontal(&context->math, state_velocity(&state));
  const double forward_move = input->command.kind == QA_USER_COMMAND_QW ? input->command.as.qw.forward_move
                                                                         : input->command.as.q1.forward_move;
  const qa_locomotion_animation locomotion =
      water_level >= 2 ? "swim"
      : ground.kind == QA_TRACE_HIT_NONE ? "jump"
      : horizontal_speed > 0             ? (forward_move < 0 ? "backward" : "run")
                                         : "idle";
  qa_animation_step_input animation_input;
  memset(&animation_input, 0, sizeof animation_input);
  animation_input.actor = input->actor;
  animation_input.frame = input->frame;
  animation_input.animation = weapon.animation;
  animation_input.locomotion = locomotion;
  animation_input.backwards = forward_move < 0;
  animation_input.force = false;
  const qa_animation_step_result animation =
      context->services.animation_step(context->services.ctx, animation_input);
  for (size_t i = 0; i < animation.effect_count; i++) qa_q1_context_effect(context, animation.effects[i]);
  qa_movement_result result;
  memset(&result, 0, sizeof result);
  result.kind = kind_name(state.kind);
  result.status = "active";
  result.actor = input->actor.id;
  result.command_sequence = input->command_sequence;
  result.bounds = qa_q1_context_bounds(context);
  result.view_angles = view_angles;
  result.view_height = qa_q1_context_view_height(context);
  result.ground = ground;
  result.water_level = water_level;
  result.water_type = water_type;
  result.horizontal_speed = horizontal_speed;
  result.arsenal = weapon.arsenal;
  result.animation = animation.animation;
  result.state = state;
  qa_q1_context_take_results(context, &result);
  return result;
}
