#ifndef QA_APPLICATION_NATIVE_Q2_CALLBACK_REGION_H
#define QA_APPLICATION_NATIVE_Q2_CALLBACK_REGION_H

#include "qa/native.h"

typedef struct application_native_q2_callback_region application_native_q2_callback_region;
typedef bool (*application_native_q2_region_current_fn)(void *, qa_error *);

/* The acquired callback document belongs to the enclosing source owner. The
 * scope copies its frame and field recipe; lowered pointer inputs remain held
 * by that owner until checked close succeeds. Live scopes exclude capture. */
bool application_native_q2_callback_region_validate(qa_native_instance *, const qa_native_declaration *,
    const qa_json_document *, qa_json_id definition, qa_error *);
bool application_native_q2_callback_region_call_validate(qa_native_instance *, const qa_native_declaration *,
    const qa_json_document *, qa_json_id definition, const qa_native_signature *, qa_error *);
bool application_native_q2_callback_region_execute(qa_native_instance *, const qa_native_declaration *,
    const qa_json_document *, qa_json_id definition, qa_native_address target,
    const qa_native_signature *, const qa_native_value *arguments, size_t argument_count,
    const qa_native_value *inputs, size_t input_count,
    application_native_q2_region_current_fn, void *context,
    application_native_q2_callback_region **scope, qa_native_value *result,
    bool *entered, qa_error *);
bool application_native_q2_callback_region_close(application_native_q2_callback_region **, qa_error *);

#endif
