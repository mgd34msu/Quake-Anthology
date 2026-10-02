#ifndef QA_FRONTEND_SOURCE_ACOUSTICS_H
#define QA_FRONTEND_SOURCE_ACOUSTICS_H
#include "internal.h"
#include "qa/audio_acoustics_prepare.h"
#include "qa/q3_host_collision.h"

/* Holds the actual shared and per-seat collision parents. The returned
 * descriptor owns its references until the engine or candidate releases it. */
bool frontend_acoustics_source_hold(qa_frontend *,qa_audio_acoustics_source *,qa_error *);
bool frontend_acoustics_source_bind(qa_frontend *,qa_error *);
/* Ordinary audio frames consume the committed ENGINE toggle after Source
 * presentation returns. A pending shared publication owns its own candidate. */
bool frontend_acoustics_source_sync(qa_frontend *,qa_error *);
bool frontend_source_acoustics_shared(const qa_frontend *,bool *,qa_error *);
bool frontend_source_acoustics_private_hold(qa_frontend *,uint32_t,
    qa_q3_host_collision_scene **,bool *,qa_error *);
bool frontend_source_acoustics_private_current(const qa_frontend *,uint32_t,
    const qa_q3_host_collision_view *);
#endif
