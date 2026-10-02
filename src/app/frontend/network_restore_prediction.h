#ifndef QA_FRONTEND_NETWORK_RESTORE_PREDICTION_H
#define QA_FRONTEND_NETWORK_RESTORE_PREDICTION_H
#include "network_restore.h"
#include "network_predictor.h"

/* Genuine raw scene and command-ring observations for the detached graph.
 * The imported frontend parent owns the actual map and collision geometry. */
bool frontend_network_restore_prediction_pending(const qa_frontend *);
bool frontend_network_restore_prediction_read(const qa_frontend *,
    frontend_network_prediction_source *, bool *present, qa_error *);
bool frontend_network_restore_prediction_current(const qa_frontend *,
    const frontend_network_prediction_source *);
bool frontend_network_restore_prediction_acknowledgement(const qa_frontend *,
    const frontend_network_prediction_source *, bool *, uint64_t *, bool *, qa_error *);
bool frontend_network_restore_prediction_input_read(const qa_frontend *,
    frontend_remote_input_source *, bool *present, qa_error *);
bool frontend_network_restore_prediction_input_current(const qa_frontend *,
    const frontend_remote_input_source *);
#endif
