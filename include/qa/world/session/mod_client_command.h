/*
 * Port of src/world/session/mod-client-command.ts: source user-command
 * scaling, aim, and scalar input outputs. Commands pass by value;
 * each call returns the updated command (the TS spread copy).
 * Math.fround staging is a double -> float cast at the TS point.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_COMMAND_H
#define QA_WORLD_SESSION_MOD_CLIENT_COMMAND_H

#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/protocol.h"

double qa_mod_client_move_scale(qa_user_command command);
double qa_mod_client_command_impulse(qa_user_command command);
qa_user_command qa_mod_client_command_aim(qa_user_command command,
                                          qa_vec3 aim, qa_vec3 previous);
/* input excludes "view-angles" (exact TS strings). */
qa_user_command qa_mod_client_command_scalar(
    qa_user_command command, qa_mod_client_input input, double value);
/* TS modClientOutputChanges: consume expands to one zeroing set per
 * input; a set passes through. Count/index form of the TS array. */
size_t qa_mod_client_output_change_count(qa_mod_client_input_output output);
qa_mod_client_input_output qa_mod_client_output_change_at(
    qa_mod_client_input_output output, size_t index);

#endif
