/*
 * Port of src/movement/q3/move.ts (bg_pmove.c). GPL-2.0-or-later.
 * TS internal errors ("Walking requires a ground plane") abort via
 * qa_range_error, as do the RangeError argument checks.
 */
#ifndef QA_MOVEMENT_Q3_MOVE_H
#define QA_MOVEMENT_Q3_MOVE_H
#include "qa/movement/q3/types.h"

void qa_q3_drop_movement_timers(qa_q3_motion *state, double milliseconds);
void qa_q3_update_view_angles(qa_q3_motion *ps, qa_q3_move_command cmd);
/* Result contacts are malloc'd; caller frees. */
qa_q3_motion_result qa_q3_move_player(qa_q3_motion *state, qa_q3_move_command command,
                                      qa_q3_motion_options *options);
#endif
