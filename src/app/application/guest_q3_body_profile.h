#ifndef QA_APPLICATION_GUEST_Q3_BODY_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_BODY_PROFILE_H

#include "qa/application_q3_body_source.h"

typedef struct application_q3_body_call {
    uint32_t instruction;
    qa_application_q3_body_part part;
} application_q3_body_call;
typedef struct application_q3_body_submission {
    uint32_t entry, actor_argument, reference_argument;
    uint64_t entity_number_offset;
    bool argument_reference, conditional, mesh;
    uint32_t condition_argument;
    int64_t condition_value;
    uint32_t mesh_entry, mesh_entity_argument, mesh_state_argument, mesh_shader_offset;
    application_q3_body_call *calls;
    size_t call_count;
} application_q3_body_submission;
typedef struct application_q3_body_profile {
    char *artifact_path;
    const qa_qvm_image *image;
    qa_qvm_abi abi;
    application_q3_body_submission *submissions;
    size_t count;
    bool present;
} application_q3_body_profile;

/* NULL declaration is genuine absence. A present empty opening is invalid
 * JSON, rather than permission to use a stock fallback. The artifact owner
 * retains the declaration's actual resource/receipt separately. Output must
 * be an empty initialized owner; failure leaves it unchanged. */
bool application_q3_body_profile_read(const qa_qvm_image *, qa_qvm_role,
    qa_qvm_abi, const char *artifact_path, const qa_bytes *declaration,
    application_q3_body_profile *, qa_error *);
bool application_q3_body_profile_qualify(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, const application_q3_body_profile *, qa_error *);
void application_q3_body_profile_free(application_q3_body_profile *);

#endif
