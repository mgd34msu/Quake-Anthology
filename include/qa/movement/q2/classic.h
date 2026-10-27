/*
 * Port of src/movement/q2/classic.ts: Quake II classic pmove.
 * id Software / ZeniMax. GPL-2.0-or-later.
 * The TS default arguments (airAccelerate 0, strafejumpHack false,
 * flight false, speedMultiplier 1) are explicit in C.
 */
#ifndef QA_MOVEMENT_Q2_CLASSIC_H
#define QA_MOVEMENT_Q2_CLASSIC_H

#include <stdbool.h>

#include "qa/contracts/numeric.h"
#include "qa/movement/q2/types.h"

/** TS pmoveClassic. */
void qa_q2_pmove_classic(qa_q2_classic_pmove *pm, const qa_numeric_operations *numeric_ops,
                         double air_accelerate, bool strafejump_hack, bool flight,
                         double speed_multiplier);

#endif
