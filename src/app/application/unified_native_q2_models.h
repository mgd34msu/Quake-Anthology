#ifndef QA_APPLICATION_UNIFIED_NATIVE_Q2_MODELS_H
#define QA_APPLICATION_UNIFIED_NATIVE_Q2_MODELS_H

#include "network_unified.h"
#include "qa/unified_frame_visuals.h"
#include "qa/application_visual_visibility.h"

/* Append actual original primary Q2 GAME models into the target FRAME lease.
 * A different primary implementation appends no rows. */
bool application_unified_native_q2_models(qa_application *,
    const application_unified_source *, const qa_application_visual_visibility *,
    qa_unified_frame *target, qa_unified_frame_visuals *, qa_error *);

#endif
