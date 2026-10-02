#ifndef QA_FRONTEND_REMOTE_UNIFIED_INPUT_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_INPUT_SAVE_H
#include "remote_unified_input.h"
bool frontend_unified_input_checkpoint(const frontend_unified_input *,qa_buffer *,qa_error *);
/* The actual CLIENT namespace and prediction import already exist. Restores
 * retained samples and retries without configuration or movement execution. */
bool frontend_unified_input_restore(qa_frontend *,frontend_remote_unified *,
    frontend_remote_unified_prediction *,qa_bytes,frontend_unified_input **,qa_error *);
#endif
