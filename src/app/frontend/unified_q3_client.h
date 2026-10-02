#ifndef QA_FRONTEND_UNIFIED_Q3_CLIENT_H
#define QA_FRONTEND_UNIFIED_Q3_CLIENT_H

#include "unified_q3_sources.h"
#include "../../presentation/q3_native/compiled_source.h"
#include "qa/application_native_q3_client.h"

typedef struct frontend_unified_q3_client frontend_unified_q3_client;
typedef struct frontend_unified_q3_client_frame frontend_unified_q3_client_frame;
typedef struct frontend_unified_q3_command {
    const frontend_unified_q3_client *owner;
    uint64_t revision;
    int32_t sequence;
    bool present;
    const qa_command_tokens *arguments;
} frontend_unified_q3_command;

/* The caller supplies a real allocated receiver namespace. This per-Source
 * CLIENT owns its 32 snapshots, 64 reliable commands and reached dictionary. */
bool frontend_unified_q3_client_create(frontend_remote_unified *,
    frontend_unified_q3_sources *, const frontend_unified_q3_source_view *,
    uint64_t receiver, frontend_unified_q3_client **, qa_error *);
bool frontend_unified_q3_client_prepare(frontend_unified_q3_client *,
    const frontend_unified_q3_source_view *, frontend_unified_q3_client_frame **, qa_error *);
bool frontend_unified_q3_client_ready(const frontend_unified_q3_client_frame *);
void frontend_unified_q3_client_commit(frontend_unified_q3_client_frame **);
void frontend_unified_q3_client_abort(frontend_unified_q3_client_frame **);
bool frontend_unified_q3_client_current(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_matches(const frontend_unified_q3_client *, const frontend_unified_q3_source_view *);
bool frontend_unified_q3_client_idle(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_destroy(frontend_unified_q3_client **, qa_error *);
q3n_compiled_source *frontend_unified_q3_client_source(frontend_unified_q3_client *);
const qa_command_context *frontend_unified_q3_client_context(const frontend_unified_q3_client *);
qa_cvars *frontend_unified_q3_client_cvars(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_register(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_cvars_update(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_cvar_read(const frontend_unified_q3_client *, const char *,
    qa_native_q3_client_cvar *, qa_error *);
/* Called by the actual CG constructor only after all required child stages. */
bool frontend_unified_q3_client_initialization_complete(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_latest(const frontend_unified_q3_client *, int32_t *, int32_t *, qa_error *);
bool frontend_unified_q3_client_snapshot(const frontend_unified_q3_client *, int32_t,
    const qa_q3_snapshot **, qa_error *);
bool frontend_unified_q3_client_snapshot_actor(const frontend_unified_q3_client *, int32_t message,
    uint32_t source_number, qa_actor_id *, bool *present, qa_error *);
/* Reaching a command applies only that command's actual cs value, retaining
 * the authoritative Source strings independently. No command is skipped. */
bool frontend_unified_q3_client_command(frontend_unified_q3_client *, int32_t,
    frontend_unified_q3_command *, qa_error *);
bool frontend_unified_q3_command_current(const frontend_unified_q3_command *);
bool frontend_unified_q3_client_server_command(frontend_unified_q3_client *,
    uint64_t event_sequence, int32_t recipient, const char *text, qa_error *);
/* The factory calls this after the real committed FRAME event queue drains,
 * before the CG cache can borrow its new snapshot. */
bool frontend_unified_q3_client_seal(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_actor_fields(frontend_unified_q3_client *,
    qa_source_save_io *, qa_actor_id *);
bool frontend_unified_q3_client_checkpoint(const frontend_unified_q3_client *, qa_buffer *, qa_error *);
bool frontend_unified_q3_client_restore(frontend_remote_unified *, frontend_unified_q3_sources *,
    const frontend_unified_q3_source_view *, uint64_t receiver, qa_bytes,
    frontend_unified_q3_client **, qa_error *);

#endif
