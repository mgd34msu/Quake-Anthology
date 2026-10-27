/* Port of src/movement/q3/postures.ts. GPL-2.0-or-later. */
#ifndef QA_MOVEMENT_Q3_POSTURES_H
#define QA_MOVEMENT_Q3_POSTURES_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/movement/q3/types.h"
extern const qa_bounds qa_q3_source_standing_bounds;
extern const qa_q3_postures qa_q3_source_postures;
qa_fixed_movement_pose qa_q3_invulnerability_pose(bool expanded, qa_q3_postures postures);
#endif
