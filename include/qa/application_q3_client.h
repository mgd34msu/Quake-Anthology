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

typedef struct qa_application_q3_client_host {
    qa_q3_host *host;
    qa_q3_host_client_context context;
    qa_application_q3_client_context source;
} qa_application_q3_client_host;
/* Pure local original CGAME inventory and its actual GAME client binding.
 * A completed uninitialized host is present. Only a genuinely absent role
 * clears the outputs; incomplete or retired roles fail. No clocks, source
 * program entry, or cache refresh occur here. The host retains its lease. */
bool qa_application_q3_client_host_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_host *, bool *present, qa_error *);
/* First CGAME Init may change initialized without changing this namespace. */
bool qa_application_q3_client_host_current(qa_application *,
    const qa_application_q3_client_host *);

/* After synchronous effects unwind, retire this exact initialized local CGAME
 * role at an idle boundary. GAME and its physical client remain admitted.
 * Failed physical cleanup retains the retired role for enclosing owner cleanup. */
bool qa_application_q3_client_retire(qa_application *,
    const qa_application_q3_client_context *, qa_error *);

/* Borrows the single actual launch seat's CGAME host with external services.
 * GAME/source fields are absent; the network owner supplies and qualifies its
 * actual remote connection, physical client ordinal and presentation clock. */
bool qa_application_q3_remote_context_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *, qa_error *);
bool qa_application_q3_remote_context_current(qa_application *,
    const qa_application_q3_client_context *);

/* Qualifies the actual published receiver while a detached candidate is routed.
 * Uses the published snapshot and physical provider inventory without mutation. */
bool qa_application_q3_remote_published_context_read(qa_application *, qa_actor_owner receiver,
    uint32_t seat, qa_application_q3_client_context *, qa_error *);
bool qa_application_q3_remote_published_context_current(qa_application *,
    const qa_application_q3_client_context *);

#endif
