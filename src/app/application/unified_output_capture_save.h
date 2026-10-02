#ifndef QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_SAVE_H
#define QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_SAVE_H

#include "unified_output_capture.h"

bool application_unified_output_capture_checkpoint(const application_unified_output_capture *,
    qa_buffer *, qa_error *);
bool application_unified_output_capture_restore(qa_bytes, qa_application *,
    const application_unified_source *, qa_net_client_id, const qa_unified_session_player *,
    application_unified_component_publisher *, application_unified_output_capture **, qa_error *);

#endif
