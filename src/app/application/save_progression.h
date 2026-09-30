#ifndef QA_APPLICATION_SAVE_PROGRESSION_H
#define QA_APPLICATION_SAVE_PROGRESSION_H
#include "qa/persistence_application.h"

/* Enclosing QAPR framing, exact installed presence and typed external service
 * prerequisites, before restored owner constructors run. Root construction
 * uses qa_rankings_create_restored and, only for a real profile root,
 * qa_player_progress_create_restored before any consumer can borrow them. */
bool application_save_progression_prepare(const qa_application_options *,
    const qa_application_persistence_ops *, qa_bytes, qa_error *);
bool application_save_progression_capture(qa_application *,
    const qa_application_persistence_ops *, qa_buffer *empty, qa_error *);
bool application_save_progression_restore(qa_application *,
    const qa_application_persistence_ops *, qa_bytes, qa_error *);
bool application_save_progression_matches(qa_application *,
    const qa_application_persistence_ops *, qa_bytes, qa_error *);
/* All private/shared/frontend owners and complete byte recapture must already
 * be validated. This is the LAST fallible operation before pointer publication;
 * success cannot be followed by a fallible check or cleanup. */
bool application_save_progression_handoff(qa_application *active, qa_application *candidate,
    const qa_application_persistence_ops *, bool *relinquish_active, qa_error *);
/* Only after the successful handoff above. No callbacks or allocations. */
void application_save_progression_publish(qa_application *active, qa_application *candidate,
                                          bool relinquish_active);
#endif
