#ifndef QA_APPLICATION_GUEST_Q3_BODY_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_BODY_PRIVATE_H
#include "guest_q3_body.h"

typedef struct body_range {
    struct body_range *previous;
    const application_q3_body_submission *declaration;
    uint32_t start, end, physical;
    int64_t state;
    qa_actor_id actor;
    bool hidden;
} body_range;
typedef struct body_mesh {
    struct body_mesh *previous;
    body_range *range;
    int32_t pointer, shader;
    qa_application_q3_body_part part;
    uint32_t helper;
} body_mesh;
typedef struct body_hook {
    struct application_q3_body *owner;
    const application_q3_body_submission *declaration;
    uint32_t instruction;
    qa_qvm_binding binding;
    qa_qvm_function_hook function;
} body_hook;
struct application_q3_body {
    application_q3_body_module module;
    qa_application_q3_body_services services;
    qa_application_q3_body_draw draw;
    body_hook *hooks;
    size_t hook_count;
    body_range *ranges;
    body_mesh *meshes;
    bool drawing, enabled, busy;
};

size_t application_q3_body_profile_hooks(const application_q3_body_profile *);
bool application_q3_body_bindings_complete(const application_q3_body *);
#endif
