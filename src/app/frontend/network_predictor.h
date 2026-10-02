#ifndef QA_FRONTEND_NETWORK_PREDICTOR_H
#define QA_FRONTEND_NETWORK_PREDICTOR_H

#include "network_prediction.h"
#include "remote_prediction.h"

typedef struct frontend_network_predictor frontend_network_predictor;

bool frontend_network_predictor_create(qa_frontend *,
    const qa_application_control_prediction_configuration *, frontend_network_predictor **, qa_error *);
bool frontend_network_predictor_restore(qa_frontend *, qa_bytes, frontend_network_predictor **, qa_error *);
void frontend_network_predictor_destroy(frontend_network_predictor *);
frontend_remote_prediction *frontend_network_predictor_read(const frontend_network_predictor *);
bool frontend_network_predictor_source_current(frontend_network_predictor *, const frontend_remote_prediction_source *);
bool frontend_network_predictor_bound(const frontend_network_predictor *, const qa_frontend *);
void frontend_network_predictor_rebind(frontend_network_predictor *, qa_frontend *);
bool frontend_network_client_predictor_read(qa_frontend *, frontend_remote_prediction **, bool *, qa_error *);
bool frontend_network_client_predictor_finish_restore(qa_frontend *, qa_error *);
bool frontend_network_client_predictor_admit(qa_frontend *, frontend_remote_prediction *, qa_error *);
bool frontend_network_client_sample(qa_frontend *, uint32_t physical_seat, qa_actor_id,
    const qa_movement_command *selected, frontend_remote_prediction_angle_space,
    const qa_seat_input_sample *, double source_frame_ms, qa_error *);

/* These observations retain the actual transport ring and source-input owner.
 * They neither append a command nor create a prediction configuration. */
bool frontend_network_prediction_input_read(const qa_frontend *, frontend_remote_input_source *, bool *, qa_error *);
bool frontend_network_prediction_input_current(const qa_frontend *, const frontend_remote_input_source *);
bool frontend_network_prediction_acknowledgement(const qa_frontend *,
    const frontend_network_prediction_source *, bool *, uint64_t *, bool *, qa_error *);
bool frontend_network_prediction_actor_at(const qa_frontend *, uint32_t, qa_actor_id *, bool *, qa_error *);
bool frontend_network_prediction_number_of(const qa_frontend *, qa_actor_id, uint32_t *, bool *, qa_error *);

#endif
