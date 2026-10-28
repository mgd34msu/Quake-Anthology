/* Port of src/app/bootstrap/simulation/prediction/sequence.ts.
 * trajectory/effects malloc'd (free()); player ammo owned by snapshot.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_SEQUENCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_SEQUENCE_H
#include "qa/app/bootstrap/simulation/prediction/types.h"
typedef struct qa_app_prediction_sequence_result {
  qa_app_movement_prediction_snapshot player;
  qa_ordered_movement_effect *effects;
  size_t effect_count;
  qa_vec3 *trajectory;
  size_t trajectory_count;
  double seconds;
} qa_app_prediction_sequence_result;
/* shape NULL = TS default. */
qa_app_prediction_sequence_result qa_app_predict_movement_sequence(const qa_app_movement_probe_options *options,
    qa_app_movement_prediction_snapshot initial, const qa_app_prediction_command *commands, size_t command_count,
    const qa_trace_shape *shape);
#endif
