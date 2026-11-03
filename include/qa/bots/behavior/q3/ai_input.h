/*
 * Port of src/bots/behavior/q3/ai-input.ts (game/ai_main.c view and command
 * conversion). GPL-2.0-or-later.
 *
 * The TS UserCommand is the state's source usercmd_t cell, so the command
 * output is qa_bot_user_command_source and stores follow its int32/uint8/int8
 * cells. Delta angles are the integer player-state vector.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_INPUT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_INPUT_H
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdint.h>
float qa_bot_angle_difference(double first, double second);
float qa_bot_change_view_angle(double angle, double ideal, double speed);
void qa_bot_change_view_angles(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                               double think_time);
void qa_bot_input_to_user_command(qa_bot_input input, qa_bot_user_command_source *command,
                                  qa_bot_ivec3 delta_angles, int64_t time);
void qa_bot_add_delta_angles(qa_bot_state_bots_a *state);
void qa_bot_subtract_delta_angles(qa_bot_state_bots_a *state);
void qa_bot_update_input_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                int64_t time, int64_t elapsed_time);
#endif
