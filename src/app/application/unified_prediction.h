#ifndef QA_APPLICATION_UNIFIED_PREDICTION_H
#define QA_APPLICATION_UNIFIED_PREDICTION_H

#include "network_unified.h"
#include "qa/unified_frame_prediction.h"

/* Observe the admitted player's selected movement, arsenal and character at
 * this completed Source cut. Allocations belong to the typed frame owner. */
bool application_unified_prediction_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, int64_t acknowledged_input,
    qa_unified_frame *, const qa_inventory_entry *, size_t, qa_error *);

#endif
