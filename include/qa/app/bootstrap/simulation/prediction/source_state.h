/* Port of src/app/bootstrap/simulation/prediction/source-state.ts.
 * SourcePlayerState is landed qa_q3_base_player_state_record; Q2 player
 * union split by rerelease flag. Returned snapshots own ammo copies.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_SOURCE_STATE_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_SOURCE_STATE_H
#include "qa/app/bootstrap/simulation/prediction/types.h"
#include "qa/content/q3/base/shared/player_state.h"
typedef struct qa_app_prediction_source_entities {
  void *ctx;
  bool (*actor_at)(void *ctx, double number, qa_actor_id *out); /* false = TS null */
  bool (*number_of)(void *ctx, qa_actor_id actor, double *out);
} qa_app_prediction_source_entities;
qa_trace_hit qa_app_prediction_source_hit(double number, qa_app_prediction_source_entities entities);
double qa_app_prediction_source_number(qa_trace_hit hit, qa_app_prediction_source_entities entities);
qa_app_movement_prediction_snapshot qa_app_q2_prediction_snapshot(qa_app_movement_prediction_snapshot base,
    qa_q2_player_state classic, qa_q2_rerelease_player_state rerelease, bool is_rerelease,
    double sequence, double command_time_milliseconds);
qa_app_movement_prediction_snapshot qa_app_read_prediction_source_state(qa_app_movement_prediction_snapshot base,
    const qa_q3_base_player_state_record *ps, qa_app_prediction_source_entities entities);
void qa_app_write_prediction_source_state(qa_q3_base_player_state_record *ps,
    qa_app_movement_prediction_snapshot output, qa_app_prediction_source_entities entities);
#endif
