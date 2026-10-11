#ifndef QA_APPLICATION_NATIVE_Q2_CALLBACK_REGION_H
#define QA_APPLICATION_NATIVE_Q2_CALLBACK_REGION_H

#include "qa/native.h"

typedef struct application_native_q2_callback_region application_native_q2_callback_region;
typedef struct application_native_q2_region_recipe application_native_q2_region_recipe;
typedef bool (*application_native_q2_region_current_fn)(void *, qa_error *);

bool application_native_q2_callback_region_prepare(qa_native_instance *, const qa_native_declaration *,
    const qa_json_document *, qa_json_id definition, const qa_native_signature *, qa_native_address,
    application_native_q2_region_recipe **, qa_error *);
void application_native_q2_callback_region_dispose(application_native_q2_region_recipe *);
bool application_native_q2_callback_region_execute(qa_native_instance *, application_native_q2_region_recipe *, qa_native_address target,
    const qa_native_signature *, const qa_native_value *arguments, size_t argument_count,
    const qa_native_value *inputs, size_t input_count,
    application_native_q2_region_current_fn, void *context,
    application_native_q2_callback_region **scope, qa_native_value *result,
    bool *entered, qa_error *);
bool application_native_q2_callback_region_close(application_native_q2_callback_region **, qa_error *);

#endif
