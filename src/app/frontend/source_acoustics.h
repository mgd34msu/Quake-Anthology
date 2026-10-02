#ifndef QA_FRONTEND_SOURCE_ACOUSTICS_H
#define QA_FRONTEND_SOURCE_ACOUSTICS_H
#include "internal.h"
#include "qa/audio_acoustics_prepare.h"

/* Holds the actual shared and per-seat collision parents. The returned
 * descriptor owns its references until the engine or candidate releases it. */
bool frontend_acoustics_source_hold(qa_frontend *,qa_audio_acoustics_source *,qa_error *);
#endif
