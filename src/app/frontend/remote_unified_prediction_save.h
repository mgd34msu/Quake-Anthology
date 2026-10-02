#ifndef QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_PREDICTION_SAVE_H
#include "remote_unified_prediction.h"
bool frontend_remote_unified_prediction_checkpoint(
    const frontend_remote_unified_prediction *,qa_buffer *,qa_error *);
/* The enclosing owner has restored the actual recipe, identities and received
 * frame. Imports the retained graph even after a received disconnect without
 * granting live input authority or executing Source. */
bool frontend_remote_unified_prediction_restore(frontend_remote_unified *,qa_bytes,
    frontend_remote_unified_prediction **,qa_error *);
#endif
