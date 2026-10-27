/*
 * Port of src/movement/q2/view.ts: Quake II PM_ClampAngles view
 * projection for the classic (short angles) and rerelease (float
 * angles) dialects. id Software / ZeniMax. GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q2_VIEW_H
#define QA_MOVEMENT_Q2_VIEW_H

#include "qa/contracts/numeric.h"
#include "qa/movement/q2/types.h"

void qa_q2_classic_view_angles(qa_q2_vec3 view, const qa_q2_vec3 angles, const qa_q2_vec3 delta,
                               double flags, const qa_numeric_operations *numeric);
void qa_q2_rerelease_view_angles(qa_q2_vec3 view, const qa_q2_vec3 angles, const qa_q2_vec3 delta,
                                 double flags, const qa_numeric_operations *numeric);

#endif
