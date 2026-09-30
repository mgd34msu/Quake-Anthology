#ifndef QA_PERSISTENCE_NAVIGATION_H
#define QA_PERSISTENCE_NAVIGATION_H

#include "qa/navigation.h"
#include "qa/session.h"

/* Explicit navigation continuation for one actual graph owner. The application
 * must select the same provider/profile graph against the pinned map identity.
 * Restore targets an isolated candidate session/navigation service, validates
 * every referenced node/edge through the typed checkpoint owner, and runs no
 * source callbacks. Capture output remains unchanged on failure. */
bool qa_persistence_navigation_capture(qa_session *, const qa_navigation *, qa_buffer *, qa_error *);
bool qa_persistence_navigation_restore(qa_session *, qa_navigation *, qa_bytes, qa_error *);

#endif
