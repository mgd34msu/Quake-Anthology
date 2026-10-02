#ifndef QA_APPLICATION_Q1_COMPOSITION_H
#define QA_APPLICATION_Q1_COMPOSITION_H

#include "qa/application.h"

/* Pure actual CTF GAME/client/roster qualification. An unrelated source or
 * absent source client returns found=false and leaves source_time_ns unchanged.
 * A found recipient carries the genuine current Q1 GAME clock. */
bool qa_application_q1_ctf_recipient_read(qa_application *, qa_actor_owner,
    qa_actor_id, uint64_t *source_time_ns, bool *found, qa_error *);

#endif
