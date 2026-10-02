#ifndef QA_FRONTEND_UNIFIED_Q3_CLIENT_H
#define QA_FRONTEND_UNIFIED_Q3_CLIENT_H

#include "unified_q3_sources.h"
#include "../../presentation/q3_native/compiled_source.h"
#include "qa/application_native_q3_client.h"

typedef struct frontend_unified_q3_client frontend_unified_q3_client;
typedef struct frontend_unified_q3_client_frame frontend_unified_q3_client_frame;
typedef struct frontend_unified_q3_client_video frontend_unified_q3_client_video;
typedef struct frontend_video_guests frontend_video_guests;
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
const q3n_compiled_source_rebind_ticket *frontend_unified_q3_client_frame_rebind(const frontend_unified_q3_client_frame *);
const qa_command_context *frontend_unified_q3_client_frame_context(const frontend_unified_q3_client_frame *);
void frontend_unified_q3_client_commit(frontend_unified_q3_client_frame **);
void frontend_unified_q3_client_abort(frontend_unified_q3_client_frame **);
bool frontend_unified_q3_client_current(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_checkpoint_current(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_checkpoint_matches(const frontend_unified_q3_client *, const frontend_unified_q3_source_view *);
bool frontend_unified_q3_client_matches(const frontend_unified_q3_client *, const frontend_unified_q3_source_view *);
bool frontend_unified_q3_client_event_matches(const frontend_unified_q3_client *, const char *instance,
    const char *content, uint32_t source_epoch);
bool frontend_unified_q3_client_idle(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_retirement_bind(frontend_unified_q3_client *,frontend_unified_q3_source_retirement *,qa_error *);
bool frontend_unified_q3_client_retirement_unbind(frontend_unified_q3_client *,frontend_unified_q3_source_retirement *,qa_error *);
bool frontend_unified_q3_client_retirement_current(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_retirement_departed(const frontend_unified_q3_client *);
const qa_command_context *frontend_unified_q3_client_retirement_context(const frontend_unified_q3_client *);
qa_cvars *frontend_unified_q3_client_retirement_cvars(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_constructor_reset(frontend_unified_q3_client *,void *,bool (*cg_closed)(const void *),qa_error *);
bool frontend_unified_q3_client_destroy(frontend_unified_q3_client **, qa_error *);
/* The real video aggregate holds transport steady while its CG child closes,
 * registers and initializes against the reached CLIENT baseline. */
bool frontend_unified_q3_client_video_prepare(frontend_unified_q3_client *, qa_frontend *,
    const frontend_video_guests *, frontend_unified_q3_client_video **, qa_error *);
bool frontend_unified_q3_client_video_current(const frontend_unified_q3_client_video *);
bool frontend_unified_q3_client_video_begin(frontend_unified_q3_client_video *,
    void *, bool (*cg_closed)(const void *), qa_error *);
bool frontend_unified_q3_client_video_baseline(const frontend_unified_q3_client_video *,
    const frontend_unified_q3_client *, int32_t *message, int32_t *reached_command, qa_error *);
bool frontend_unified_q3_client_video_finish(frontend_unified_q3_client_video **, qa_error *);
bool frontend_unified_q3_client_video_abort(frontend_unified_q3_client_video **, qa_error *);
q3n_compiled_source *frontend_unified_q3_client_source(frontend_unified_q3_client *);
const qa_command_context *frontend_unified_q3_client_context(const frontend_unified_q3_client *);
qa_cvars *frontend_unified_q3_client_cvars(const frontend_unified_q3_client *);
const qa_command_context *frontend_unified_q3_client_checkpoint_context(const frontend_unified_q3_client *);
qa_cvars *frontend_unified_q3_client_checkpoint_cvars(const frontend_unified_q3_client *);
bool frontend_unified_q3_client_register(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_cvars_update(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_cvar_read(const frontend_unified_q3_client *, const char *,
    qa_native_q3_client_cvar *, qa_error *);
bool frontend_unified_q3_client_cvar_number(frontend_unified_q3_client *, const char *, float, qa_error *);
bool frontend_unified_q3_client_local_server_read(const frontend_unified_q3_client *, int32_t *, qa_error *);
/* Called by the actual CG constructor only after all required child stages. */
bool frontend_unified_q3_client_initialization_complete(frontend_unified_q3_client *, qa_error *);
bool frontend_unified_q3_client_latest(const frontend_unified_q3_client *, int32_t *, int32_t *, qa_error *);
bool frontend_unified_q3_client_snapshot(const frontend_unified_q3_client *, int32_t,
    const qa_q3_snapshot **, qa_error *);
bool frontend_unified_q3_client_snapshot_actor(const frontend_unified_q3_client *, int32_t message,
    uint32_t source_number, qa_actor_id *, bool *present, qa_error *);
bool frontend_unified_q3_client_snapshot_number(const frontend_unified_q3_client *, int32_t message,
    qa_actor_id, uint32_t *source_number, bool *present, qa_error *);
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
bool frontend_unified_q3_client_restore_retired(frontend_remote_unified *,frontend_unified_q3_sources *,
    frontend_unified_q3_source_retirement *,uint64_t receiver,qa_bytes,frontend_unified_q3_client **,qa_error *);

#endif
