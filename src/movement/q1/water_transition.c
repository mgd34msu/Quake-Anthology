/* Port of src/movement/q1/water-transition.ts. GPL-2.0-or-later. */
#include "qa/movement/q1/water_transition.h"

qa_q1_water_transition_result qa_q1_water_transition(double previous_type, double contents) {
  if (previous_type == 0) {
    const qa_q1_water_transition_result result = {contents, 1, false};
    return result;
  }
  if (contents <= -3) {
    const qa_q1_water_transition_result result = {contents, 1, previous_type == -1};
    return result;
  }
  const qa_q1_water_transition_result result = {-1, contents, previous_type != -1};
  return result;
}
