#ifndef QA_CINEMATIC_RESTORE_H
#define QA_CINEMATIC_RESTORE_H
#include "qa/cinematic.h"
/* Isolated owner reconstruction uses the already-restored engine queue and a
 * qualified wall-clock anchor. It neither samples the installed clock nor
 * replaces an audio bus; decoder diagnostics/output remain suppressed until
 * publication. The returned unadopted owner rejects ordinary playback,
 * publication, queries and destruction until commit. Its restored audio
 * queue remains borrowed from the enclosing candidate engine. */
bool qa_cinematic_restore_qualified(const qa_cinematic_source *, const qa_cinematic_options *,
    const qa_cinematic_checkpoint *, double wall_milliseconds, qa_cinematic **, qa_error *);
/* After the enclosing stream finishes and qualifies every owner, commit is
 * a no-fail ownership transfer with no callbacks or queue change. Discard
 * frees only unadopted local owners/references, preserving the engine queue
 * and suppressing completion/diagnostic callbacks. Both require idle owners;
 * ordinary live destruction keeps its normal owned stream retirement. */
void qa_cinematic_restore_commit(qa_cinematic *);
void qa_cinematic_restore_discard(qa_cinematic *);
#endif
