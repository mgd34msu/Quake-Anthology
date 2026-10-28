/* Port of src/app/bootstrap/simulation/prediction/qw-source-state.ts.
 * QwMoveVariables mirror: landed network/q1 quakeworld variables struct
 * when available is passed as qa_q1_movement_parameters (same fields).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_QW_SOURCE_STATE_H
#define QA_APP_BOOTSTRAP_SIMULATION_PREDICTION_QW_SOURCE_STATE_H
#include "qa/app/bootstrap/simulation/prediction/runtime.h"
typedef qa_q1_movement_parameters qa_app_qw_move_variables;
qa_movement_profile qa_app_qw_prediction_profile(qa_movement_profile base, qa_app_qw_move_variables variables);
typedef struct qa_app_qw_prediction_status { double health, spectator; } qa_app_qw_prediction_status;
/* acknowledged NULL = TS null. */
qa_app_movement_prediction_snapshot qa_app_qw_prediction_snapshot(qa_app_movement_prediction_snapshot base,
    qa_qw_player_state player, qa_app_qw_prediction_status status,
    const qa_app_movement_prediction_snapshot *acknowledged, double sequence, double command_time_milliseconds);
typedef struct qa_app_quakeworld_prediction qa_app_quakeworld_prediction;
qa_app_quakeworld_prediction *qa_app_quakeworld_prediction_create(qa_app_movement_prediction_options options,
    qa_app_movement_prediction_snapshot initial, qa_app_qw_move_variables variables);
void qa_app_quakeworld_prediction_destroy(qa_app_quakeworld_prediction *prediction);
void qa_app_quakeworld_prediction_sent(qa_app_quakeworld_prediction *prediction, double sequence,
    qa_qw_user_command command, double now);
void qa_app_quakeworld_prediction_acknowledged(qa_app_quakeworld_prediction *prediction, double sequence, double now);
void qa_app_quakeworld_prediction_receive(qa_app_quakeworld_prediction *prediction,
    qa_app_movement_prediction_snapshot base, qa_qw_player_state player, qa_app_qw_move_variables variables,
    qa_app_qw_prediction_status status);
qa_app_movement_prediction_result qa_app_quakeworld_prediction_replay(qa_app_quakeworld_prediction *prediction);
#endif
