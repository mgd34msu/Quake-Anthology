#ifndef QA_CINEMATIC_RESTORE_H
#define QA_CINEMATIC_RESTORE_H
#include "qa/cinematic.h"
/* Isolated owner reconstruction uses the already-restored engine queue and a
 * qualified wall-clock anchor. It neither samples the installed clock nor
 * replaces an audio bus; decoder diagnostics/output remain suppressed until
 * the constructor has completed. The returned owner uses its installed clock
 * normally after publication. */
bool qa_cinematic_restore_qualified(const qa_cinematic_source *, const qa_cinematic_options *,
    const qa_cinematic_checkpoint *, double wall_milliseconds, qa_cinematic **, qa_error *);
#endif
