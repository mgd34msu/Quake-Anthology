#ifndef QA_APPLICATION_Q3_RESTART_H
#define QA_APPLICATION_Q3_RESTART_H
#include "native_maps.h"
#include "qa/source_save.h"

typedef struct application_q3_restart application_q3_restart;
application_q3_restart *application_q3_restart_create(qa_error *);
void application_q3_restart_destroy(application_q3_restart *);
bool application_q3_restart_idle(const application_q3_restart *);
bool application_q3_restart_pending(const application_q3_restart *);
void application_q3_restart_cancel(application_q3_restart *);
/* The real cut marks its first mutation before calling restarted source code. */
void application_q3_restart_mutated(application_q3_restart *, const qa_application *);
bool application_q3_restart_enqueue(application_q3_restart *, qa_application *,
    const qa_match_intent *, qa_error *);
/* The parser supplies the actual selected GAME mode and captured invocation. */
bool application_q3_restart_request(application_q3_restart *, qa_application *,
    qa_mode_id, const qa_command_invocation *, qa_error *);
/* Called only after existing source/frontend events have been drained. A
 * scheduled warmup publication consumes one boundary before round execution. */
bool application_q3_restart_prepare(application_q3_restart *, qa_application *,
    bool *consumed, qa_error *);
bool application_q3_restart_reconnect(application_q3_restart *, qa_application *, qa_error *);
bool application_q3_restart_stream(qa_source_save_io *, application_q3_restart *);

/* Source-owned current time and authoritative GAME configstring publication. */
bool application_q3_restart_clock(struct application_provider *, int32_t *, qa_error *);
bool application_native_q3_restart_configstring(struct application_provider *,
    uint32_t, const char *, qa_error *);
/* Root owns the actual outer-frame producer and complete destructive cut. */
uint64_t application_frame_revision(const qa_application *);
bool application_q3_round_restart(qa_application *, struct application_provider *,
    qa_mode_id, bool *mutated, qa_error *);
#endif
