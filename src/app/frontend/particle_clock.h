#ifndef QA_FRONTEND_PARTICLE_CLOCK_H
#define QA_FRONTEND_PARTICLE_CLOCK_H

#include "qa/frontend.h"

/* The committed transformed host duration precedes GAME execution. Complete
 * clamps the retained Classic client clock before source effect delivery. */
bool frontend_particle_source_begin(qa_frontend *, uint64_t elapsed_ns, qa_error *);
bool frontend_particle_source_complete(qa_frontend *, qa_error *);

#endif
