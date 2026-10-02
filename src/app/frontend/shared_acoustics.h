#ifndef QA_FRONTEND_SHARED_ACOUSTICS_H
#define QA_FRONTEND_SHARED_ACOUSTICS_H
#include "shared_audio.h"
#include "qa/audio_acoustics_prepare.h"

typedef struct frontend_shared_acoustics frontend_shared_acoustics;
/* Prepare as a child of this exact already-admitted shared audio parent.
 * Checked cancellation/publication consume this child before that parent. */
bool frontend_shared_acoustics_prepare(qa_frontend *,const qa_cvars_edit *,
    frontend_shared_audio *,frontend_shared_acoustics **,qa_error *);
bool frontend_shared_acoustics_ready(frontend_shared_acoustics *,qa_error *);
bool frontend_shared_acoustics_ready_is(const frontend_shared_acoustics *);
void frontend_shared_acoustics_publish(frontend_shared_acoustics **);
bool frontend_shared_acoustics_abort(frontend_shared_acoustics **,qa_error *);
#endif
