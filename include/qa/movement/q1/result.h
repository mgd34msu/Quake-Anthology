/*
 * Port of src/movement/q1/result.ts: finishMovement runs the weapon
 * and animation stages and assembles the movement result. The
 * returned result takes over the context's contact/effect arrays
 * (caller frees them); actor-removed results carry effects only.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q1_RESULT_H
#define QA_MOVEMENT_Q1_RESULT_H

#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q1/common.h"

/* state is the q1-netquake or q1-quakeworld movement state. */
qa_movement_result qa_q1_finish_movement(qa_q1_movement_context *context, qa_movement_state state,
                                         qa_vec3 view_angles, qa_trace_hit ground, double water_level,
                                         double water_type);

#endif
