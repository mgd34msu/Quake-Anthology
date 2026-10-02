#ifndef QA_APPLICATION_UNIFIED_NATIVE_Q2_MODELS_H
#define QA_APPLICATION_UNIFIED_NATIVE_Q2_MODELS_H

#include "network_unified.h"

/* Owned CHECKPOINT array, observed from the actual original primary Q2 GAME.
 * A different primary implementation produces an empty array. */
bool application_unified_native_q2_models(qa_application *,
    const application_unified_source *, qa_unified_document **, qa_error *);

#endif
