#ifndef QA_APPLICATION_EVENTS_SAVE_H
#define QA_APPLICATION_EVENTS_SAVE_H

#include "qa/application.h"

typedef struct qa_application_saved_event_counts {
    size_t builtin, q2_map, q3_map, q2_player, protocol;
    uint64_t protocol_generation;
} qa_application_saved_event_counts;

/* The record contains the five genuine pending queues and Source journal,
 * stream counters, registered immutable event-resource references, and retained
 * world text lifetimes. Resource-bearing records require the actual CONTENT
 * graph capture/import lease; they never reopen paths. Output is unchanged on
 * failure. Capture leases a committed idle application. */
bool qa_application_events_checkpoint(qa_application *, qa_buffer *, qa_error *);
/* Import into an isolated restored application with empty pending queues.
 * Its restored actor history and string table must already be installed.
 * No source admission, signon, userinfo, event listener or output runs. Failure
 * leaves queue ownership unchanged; dispose the candidate if import fails. */
bool qa_application_events_restore(qa_application *candidate, qa_bytes,
                                   qa_error *);
/* Read only the extent prefix. It grants no actor, provider or event authority;
 * full import and candidate validation must qualify the complete record. */
bool qa_application_events_saved_counts_read(qa_bytes,
    qa_application_saved_event_counts *, qa_error *);

#endif
