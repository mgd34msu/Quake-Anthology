#ifndef QA_APPLICATION_NATIVE_Q2_CALLBACKS_H
#define QA_APPLICATION_NATIVE_Q2_CALLBACKS_H

#include "qa/native.h"
#include "qa/session.h"

struct application_native_q2;
typedef struct application_native_q2_callbacks application_native_q2_callbacks;
typedef enum application_native_callback_value_kind {
    APPLICATION_NATIVE_VALUE_ABSENT, APPLICATION_NATIVE_VALUE_NUMBER,
    APPLICATION_NATIVE_VALUE_VECTOR, APPLICATION_NATIVE_VALUE_STRING,
    APPLICATION_NATIVE_VALUE_ACTOR
} application_native_callback_value_kind;
typedef struct application_native_callback_value {
    const char *name;
    application_native_callback_value_kind kind;
    union { double number; qa_vec3 vector; const char *string; qa_actor_id actor; } value;
} application_native_callback_value;
typedef struct application_native_callback_inputs {
    const application_native_callback_value *values;
    size_t count;
    qa_bytes user_command;
} application_native_callback_inputs;

/* Owns the acquired callback document. It borrows the actual engine until
 * checked disposal. Calls resolve source addresses anew after original restore. */
bool application_native_q2_callbacks_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_validate(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_register(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_run(struct application_native_q2 *, const char *section,
    const application_native_callback_inputs *, bool *accepted, qa_error *);
bool application_native_q2_callbacks_call(application_native_q2_callbacks *, qa_json_id,
    const application_native_callback_inputs *, double *, qa_error *);
bool application_native_q2_callbacks_record(application_native_q2_callbacks *, qa_actor_id,
    const char *, qa_native_address *, qa_error *);
bool application_native_q2_callbacks_address(application_native_q2_callbacks *, qa_json_id,
    qa_native_address *, qa_error *);
bool application_native_q2_callbacks_idle(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_current(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_close(struct application_native_q2 *, qa_error *);
const qa_json_document *application_native_q2_callbacks_document(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_source_before(void *, qa_error *);
bool application_native_q2_callbacks_source_after(void *, qa_error *);
bool application_native_q2_callbacks_release_actor(struct application_native_q2 *, qa_actor_id, qa_error *);
bool application_native_q2_callbacks_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_callbacks_restore(struct application_native_q2 *, qa_bytes, qa_error *);
bool application_native_q2_callbacks_finish_restore(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_arrays_validate(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_reserved_slot(void *,uint32_t,bool *,qa_error *);
bool application_native_q2_callbacks_userinfo_validate(struct application_native_q2 *,const char *,qa_error *);
bool application_native_q2_callbacks_import(void *,const qa_native_import_call *,qa_native_value *,bool *,qa_error *);

#endif
