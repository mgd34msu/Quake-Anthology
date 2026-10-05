#ifndef QA_FRONTEND_UNIFIED_Q3_RUNTIME_SERVICES_H
#define QA_FRONTEND_UNIFIED_Q3_RUNTIME_SERVICES_H
#include "unified_q3_runtime.h"
#include "remote_unified_events.h"

typedef struct frontend_unified_q3_runtime_services frontend_unified_q3_runtime_services;
typedef struct frontend_unified_q3_runtime_services_options {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_q3_client *client;
    frontend_unified_q3_source_view source;
    frontend_unified_events *events;
    uint64_t receiver, audio_owner;
    void *audio_context;
    bool (*audio_actor)(void *, qa_actor_id, uint64_t *, qa_error *);
    /* The stable factory row returns its runtime's actual lexical frame.
     * This cannot be replaced by the newest transport snapshot. */
    const q3n_compiled_frame *(*entered_frame)(void *);
    /* Exact runtime-held returned CLIENT frame during staged capture. */
    const frontend_unified_q3_client_frame *(*checkpoint_frame)(void *);
    /* These concrete factory services retain their own contexts. The builder
     * supplies resource/cvar/collision callbacks, preserving these input,
     * predictor, console-registration and music/movie owners. */
    frontend_unified_q3_runtime_options operations;
} frontend_unified_q3_runtime_services_options;

bool frontend_unified_q3_runtime_services_create(const frontend_unified_q3_runtime_services_options *,
    frontend_unified_q3_runtime_services **, qa_error *);
/* Borrow only already imported CLIENT and bank resources. No registration or
 * Source initialization is performed by these cold constructors/readers. */
bool frontend_unified_q3_runtime_services_create_restored(const frontend_unified_q3_runtime_services_options *,
    frontend_unified_q3_runtime_services **, qa_error *);
bool frontend_unified_q3_runtime_services_read(frontend_unified_q3_runtime_services *,
    frontend_unified_q3_runtime_options *, qa_error *);
bool frontend_unified_q3_runtime_services_read_restored(frontend_unified_q3_runtime_services *,
    frontend_unified_q3_runtime_options *, qa_error *);
bool frontend_unified_q3_runtime_services_current(const frontend_unified_q3_runtime_services *);
/* Literal constructor-owned caches; they outlive the runtime borrowing them. */
bool frontend_unified_q3_runtime_services_caches(const frontend_unified_q3_runtime_services *,
    q3n_media **, q3n_clients **, qa_error *);
/* Destroy after the runtime and all of its borrowed callbacks retire. */
bool frontend_unified_q3_runtime_services_destroy(frontend_unified_q3_runtime_services **, qa_error *);
#endif
