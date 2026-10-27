/* Port of src/bots/behavior/q3/ai-input.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_INPUT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_INPUT_H
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdint.h>
float qa_bot_angle_difference(double first, double second);
float qa_bot_change_view_angle(double angle, double ideal, double speed);
void qa_bot_change_view_angles(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               double think_time, qa_bot_action_buffer *actions);
typedef struct qa_user_command_bots_a {
  int64_t server_time;
  int32_t angles[3];
  int32_t buttons;
  int32_t forward_move, right_move, up_move;
  int64_t weapon;
} qa_user_command_bots_a;
void qa_bot_input_to_user_command(qa_bot_input input, qa_user_command_bots_a *command,
                                  qa_vec3 delta_angles, int64_t time);
void qa_bot_add_delta_angles(qa_bot_state_bots_a *state, qa_vec3 delta);
void qa_bot_subtract_delta_angles(qa_bot_state_bots_a *state, qa_vec3 delta);
void qa_bot_update_input_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                int64_t time, double elapsed, qa_bot_action_buffer *actions,
                                qa_user_command_bots_a *out);
#endif
