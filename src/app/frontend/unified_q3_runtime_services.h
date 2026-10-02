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
    /* These concrete factory services retain their own contexts. The builder
     * supplies resource/cvar/collision callbacks, preserving these input,
     * predictor, console-registration and music/movie owners. */
    frontend_unified_q3_runtime_options operations;
} frontend_unified_q3_runtime_services_options;

bool frontend_unified_q3_runtime_services_create(const frontend_unified_q3_runtime_services_options *,
    frontend_unified_q3_runtime_services **, qa_error *);
bool frontend_unified_q3_runtime_services_read(frontend_unified_q3_runtime_services *,
    frontend_unified_q3_runtime_options *, qa_error *);
bool frontend_unified_q3_runtime_services_current(const frontend_unified_q3_runtime_services *);
bool frontend_unified_q3_runtime_services_destroy(frontend_unified_q3_runtime_services **, qa_error *);
#endif
