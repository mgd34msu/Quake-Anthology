#ifndef QA_APPLICATION_Q3_CLIENT_H
#define QA_APPLICATION_Q3_CLIENT_H

#include "qa/application.h"

typedef struct qa_application_q3_client_context {
    qa_session *session;
    qa_actor_owner receiver, source_owner;
    qa_actor_id source_actor;
    uint32_t seat, source_client;
    uint64_t service_owner;
    void *frontend_lifetime;
    qa_console *console;
    qa_cvars *cvars, *source_cvars;
    qa_cvars *client_time_cvars;
    qa_actor_owner client_time_owner;
    qa_command_context command_context;
    qa_source_frame source_frame;
    int32_t source_milliseconds;
    bool native_source, initialized;
} qa_application_q3_client_context;

/* Borrows the exact local CGAME host and its admitted GAME source. This may
 * run during a synchronous client effect before CGAME Init. It acquires no
 * lease and invokes no source program. UI uses its separate frontend clock. */
bool qa_application_q3_client_context_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *, qa_error *);

/* Pure retained identity check, including the full physical source actor.
 * Does not read source clocks; valid during GAME cvar observer callbacks. */
bool qa_application_q3_client_context_current(qa_application *,
    const qa_application_q3_client_context *);

#endif
