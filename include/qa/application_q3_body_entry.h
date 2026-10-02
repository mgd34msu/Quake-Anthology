#ifndef QA_APPLICATION_Q3_BODY_ENTRY_H
#define QA_APPLICATION_Q3_BODY_ENTRY_H

#include "qa/application_q3_client.h"

typedef struct qa_application_q3_body_entry {
    qa_q3_host *host;
    qa_q3_host_client_context context;
    qa_application_q3_client_context source;
    int32_t server_time, stereo_view, demo_playback;
    bool local_source;
} qa_application_q3_body_entry;

/* Borrows the actual entered CGAME Draw admission before interpreter entry.
 * Draw arguments and the completed local GAME publication are separate cells.
 * This never enters source code or manufactures a QVM call frame. */
bool qa_application_q3_body_entry_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_body_entry *, qa_error *);
bool qa_application_q3_body_entry_current(qa_application *,
    const qa_application_q3_body_entry *);

#endif
