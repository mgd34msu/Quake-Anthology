/* Port of src/app/bootstrap/simulation/prediction/step.ts.
 * Copies deep-copy the arsenal ammo array (malloc'd; destroy with
 * qa_app_movement_prediction_snapshot_destroy). Shared-QW stance
 * provider (shared-quakeworld-movement.ts sibling) is a host hook on
 * probe options via relative_command == NULL path only; stance inputs
 * use the plain QW provider with the source bounds shape.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_STEP_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_STEP_H
#include "qa/app/bootstrap/simulation/prediction/types.h"
qa_movement_state qa_app_copy_movement_state(qa_movement_state state);
qa_app_movement_prediction_snapshot qa_app_copy_prediction_snapshot(qa_app_movement_prediction_snapshot snapshot);
typedef struct qa_app_prediction_step_result {
  qa_app_movement_prediction_snapshot player;
  qa_movement_result result; /* contacts/effects borrowed from provider result storage */
} qa_app_prediction_step_result;
/* shape NULL = TS default {kind:"box", standingBounds}. */
qa_app_prediction_step_result qa_app_predict_movement_command(const qa_app_movement_probe_options *configuration,
    qa_app_movement_prediction_snapshot snapshot, qa_app_prediction_command entry,
    qa_app_prediction_step_options options, const qa_trace_shape *shape);
#endif
