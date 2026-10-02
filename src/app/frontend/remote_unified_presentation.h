#ifndef QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_H
#define QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_H
#include "remote_unified.h"

/* The CLIENT tuple comes from the actual remote admission owner. This factory
 * supplies the retained readonly world, frame, prediction and event children. */
bool frontend_remote_unified_presentation_create(qa_frontend *,
    const frontend_remote_unified_options *, frontend_remote_unified **, qa_error *);
bool frontend_remote_unified_presentation_time(const frontend_remote_unified *, double *, qa_error *);
#endif
