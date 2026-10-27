/*
 * Port of src/movement/q2/dimensions.ts: the Q2 player hull and
 * posture heights relative to the selected character body.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q2_DIMENSIONS_H
#define QA_MOVEMENT_Q2_DIMENSIONS_H

#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"

/** TS Q2_PLAYER_BOUNDS. */
extern const qa_bounds qa_q2_player_bounds;

/** TS characterHeight. The /56 is the TS plain JS division. */
double qa_q2_character_height(qa_bounds bounds, double source_height,
                              const qa_numeric_operations *numeric);

#endif
