#ifndef QA_FRONTEND_REMOTE_Q1_PREDICTION_H
#define QA_FRONTEND_REMOTE_Q1_PREDICTION_H
#include "remote_q1_client.h"
#include "qa/movement.h"
#include "qa/source_save.h"
typedef struct frontend_remote_q1_prediction frontend_remote_q1_prediction;
/* The transport supplies accepted channel sequences, never render ordinals. */
bool remote_q1_prediction_sent(frontend_remote_q1 *,uint32_t,const qa_qw_command *,uint64_t,qa_error *);
bool remote_q1_prediction_acknowledged(frontend_remote_q1 *,uint32_t,qa_error *);
bool remote_q1_prediction_receipt(frontend_remote_q1 *,uint32_t,uint64_t,qa_error *);
bool remote_q1_prediction_choked(frontend_remote_q1 *,uint8_t,qa_error *);
bool remote_q1_prediction_invalid_delta(frontend_remote_q1 *,qa_error *);
bool remote_q1_prediction_loss(frontend_remote_q1 *,uint32_t outgoing_sequence,uint8_t *,qa_error *);
bool remote_q1_prediction_receive(frontend_remote_q1 *,qa_error *);
bool remote_q1_prediction_camera_receive(frontend_remote_q1 *,qa_vec3,qa_error *);
bool remote_q1_prediction_read(const frontend_remote_q1 *,qa_qw_movement_state *,float *,bool *);
bool remote_q1_collision_acquire(frontend_remote_q1 *,qa_collision_geometry **,qa_error *);
bool remote_q1_prediction_camera_trace(frontend_remote_q1 *,qa_vec3,qa_vec3,qa_trace_result *,qa_error *);
void remote_q1_prediction_clear(frontend_remote_q1 *);
bool remote_q1_prediction_fields(frontend_remote_q1 *,qa_source_save_io *,qa_error *);
#endif
