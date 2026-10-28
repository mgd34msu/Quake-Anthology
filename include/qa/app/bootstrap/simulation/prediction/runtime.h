/* Port of src/app/bootstrap/simulation/prediction/runtime.ts.
 * Command queue capacity 64 (TS BoundedQueue). relative command for the
 * Q2 disabled path mirrors q3-commands.ts via probe.relative_command.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_RUNTIME_H
#include "qa/app/bootstrap/simulation/prediction/types.h"
qa_user_command qa_app_copy_prediction_command(qa_user_command command);
typedef struct qa_app_selected_movement_prediction qa_app_selected_movement_prediction;
/* NULL only on OOM; validation errors abort (qa_range_error). */
qa_app_selected_movement_prediction *qa_app_selected_movement_prediction_create(
    qa_app_movement_prediction_options options, qa_app_movement_prediction_snapshot initial);
void qa_app_selected_movement_prediction_destroy(qa_app_selected_movement_prediction *prediction);
const qa_app_movement_prediction_options *qa_app_selected_movement_prediction_options(
    const qa_app_selected_movement_prediction *prediction);
void qa_app_selected_movement_prediction_receive(qa_app_selected_movement_prediction *prediction,
    qa_app_movement_prediction_snapshot snapshot);
void qa_app_selected_movement_prediction_submit(qa_app_selected_movement_prediction *prediction,
    qa_app_prediction_command entry);
/* observe NULL absent; observed snapshot is a copy the callee must destroy. */
qa_app_movement_prediction_result qa_app_selected_movement_prediction_replay(
    qa_app_selected_movement_prediction *prediction,
    void (*observe)(void *ctx, qa_app_movement_prediction_snapshot snapshot), void *observe_ctx);
#endif
