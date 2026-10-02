#ifndef QA_APPLICATION_GUEST_Q3_BODY_H
#define QA_APPLICATION_GUEST_Q3_BODY_H

#include "guest_q3_body_profile.h"
#include "qa/qvm_save.h"

typedef struct application_q3_body application_q3_body;
typedef struct application_q3_body_module {
    qa_session *session;
    qa_qvm *vm;
    const qa_qvm_image *image;
    const application_q3_body_profile *profile;
    qa_actor_owner receiver;
    uint32_t seat;
    qa_q3_host_client_services client;
} application_q3_body_module;
typedef struct application_q3_body_saved {
    qa_qvm_binding *bindings;
    size_t count;
    bool present, enabled;
} application_q3_body_saved;

/* The real module retains the exact image/profile/client/service contexts.
 * Normal construction leaves hooks disabled. Import constructs the saved
 * enabled set on its isolated executor before the enclosing complete callback
 * inventory remap and RAM import. Partial construction assigns the owner. */
bool application_q3_body_create_module(const application_q3_body_module *,
    const qa_application_q3_body_services *, const application_q3_body_saved *,
    application_q3_body **, qa_error *);
bool application_q3_body_destroy(application_q3_body *, qa_error *);
bool application_q3_body_idle(const application_q3_body *);
bool application_q3_body_executor(const application_q3_body *, const qa_session *, const qa_qvm *);
bool application_q3_body_draw_begin(application_q3_body *, qa_error *);
void application_q3_body_draw_end(application_q3_body *);
/* The real trap composer calls this only after held-weapon suppression. */
bool application_q3_body_source_entity(void *, const qa_qvm_call *, int32_t,
    const qa_q3_ref_entity *, bool *suppress, qa_error *);
size_t application_q3_body_descriptor_count(const application_q3_body *);
bool application_q3_body_descriptors(const application_q3_body *, qa_qvm_saved_function *, size_t, qa_error *);
/* Only after the enclosing complete-executor qualification/remap succeeds. */
void application_q3_body_adopt(application_q3_body *, const qa_qvm_binding *);

#endif
