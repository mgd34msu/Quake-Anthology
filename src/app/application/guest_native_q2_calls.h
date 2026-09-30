#ifndef QA_APPLICATION_NATIVE_Q2_CALLS_H
#define QA_APPLICATION_NATIVE_Q2_CALLS_H
#include "qa/native.h"

typedef enum application_q2_call_operation {
    APPLICATION_Q2_DAMAGE, APPLICATION_Q2_REGULAR_ARMOR, APPLICATION_Q2_POWER_ARMOR,
    APPLICATION_Q2_PAIN, APPLICATION_Q2_DEATH, APPLICATION_Q2_PROCESS_PAIN
} application_q2_call_operation;
typedef enum application_q2_call_field {
    APPLICATION_Q2_TARGET, APPLICATION_Q2_INFLICTOR, APPLICATION_Q2_ATTACKER,
    APPLICATION_Q2_DIRECTION, APPLICATION_Q2_POINT, APPLICATION_Q2_NORMAL,
    APPLICATION_Q2_AMOUNT, APPLICATION_Q2_KNOCKBACK, APPLICATION_Q2_FLAGS,
    APPLICATION_Q2_CAUSE, APPLICATION_Q2_SPARKS, APPLICATION_Q2_KICK,
    APPLICATION_Q2_FIELD_COUNT
} application_q2_call_field;
typedef struct application_q2_call_argument application_q2_call_argument;
typedef struct application_q2_call {
    qa_native_signature signature;
    application_q2_call_argument *arguments;
    size_t field_index[APPLICATION_Q2_FIELD_COUNT];
    uint8_t pointer_bytes;
} application_q2_call;

/* Owned descriptors/defaults; the source JSON may be released after read.
 * Lowered aggregate defaults borrow call storage. Captured extras borrow the
 * original invocation and must remain alive until synchronous continuation. */
bool application_q2_call_read(const qa_json_document *, qa_json_id,
    application_q2_call_operation, qa_native_target, application_q2_call *, qa_error *);
void application_q2_call_free(application_q2_call *);
bool application_q2_call_project(const application_q2_call *, const qa_native_value *, size_t,
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT], qa_error *);
bool application_q2_call_lower(const application_q2_call *, qa_native_instance *,
    const qa_native_value fields[APPLICATION_Q2_FIELD_COUNT], const qa_native_value *captured,
    size_t captured_count, qa_native_value *, qa_error *);
#endif
