/*
 * Port of src/movement/q1/water-transition.ts: SV_CheckWaterTransition
 * preserves source initialization and empty waterlevel values.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_Q1_WATER_TRANSITION_H
#define QA_MOVEMENT_Q1_WATER_TRANSITION_H

#include <stdbool.h>

typedef struct qa_q1_water_transition_result {
  double water_type;
  double water_level;
  bool splash;
} qa_q1_water_transition_result;

qa_q1_water_transition_result qa_q1_water_transition(double previous_type, double contents);

#endif
