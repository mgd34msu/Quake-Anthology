#ifndef QA_APPLICATION_UNIFIED_PREDICTION_H
#define QA_APPLICATION_UNIFIED_PREDICTION_H

#include "network_unified.h"

/* Observe the admitted player's selected movement, arsenal and character at
 * this completed Source cut. The document owns its bytes; it borrows no VM
 * memory and does not contain a save image. Failure leaves out unchanged. */
bool application_unified_prediction_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, int64_t acknowledged_input,
    qa_unified_document **out, qa_error *);

#endif
