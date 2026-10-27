/* Port of src/network/q1/prediction.ts. The C move function also takes
 * movement options (the TS closes over provider options). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_PREDICTION_H
#define QA_NETWORK_Q1_PREDICTION_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"
#include "qa/movement/q1/quakeworld.h"
#include "qa/network/q1/commands.h"
typedef struct qa_qw_prediction_frame {
  double sequence, sent_at_seconds; qa_qw_user_command command;
} qa_qw_prediction_frame;
typedef struct qa_qw_prediction {
  bool predicted; const char *reason;
  qa_movement_result result; qa_vec3 origin, velocity; double sequence;
} qa_qw_prediction;
typedef struct qa_qw_prediction_history qa_qw_prediction_history;
void qa_qw_prediction_history_init(qa_qw_prediction_history *h);
void qa_qw_prediction_history_destroy(qa_qw_prediction_history *h);
double qa_qw_prediction_history_latency(const qa_qw_prediction_history *h);
void qa_qw_prediction_history_set_latency(qa_qw_prediction_history *h, double seconds);
void qa_qw_prediction_history_record(qa_qw_prediction_history *h, qa_qw_prediction_frame frame);
void qa_qw_prediction_history_acknowledged(qa_qw_prediction_history *h, double sequence, double received_at_seconds);
qa_quakeworld_move qa_qw_prediction_history_bundle(qa_qw_prediction_history *h, double sequence, double loss_percent);
qa_qw_prediction qa_qw_prediction_history_predict(qa_qw_prediction_history *h, qa_qw_movement_input input,
                                                  double acknowledged_sequence, double outgoing_sequence,
                                                  double realtime_seconds, double push_latency_ms,
                                                  qa_movement_services services, qa_q1_movement_options options);
#endif
