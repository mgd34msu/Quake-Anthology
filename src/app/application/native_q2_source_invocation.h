#ifndef APPLICATION_NATIVE_Q2_SOURCE_INVOCATION_H
#define APPLICATION_NATIVE_Q2_SOURCE_INVOCATION_H
#include "qa/native_observe.h"
#include "native_q2_callbacks.h"
struct application_native_q2;
struct application_native_q2_source_invocation;
bool application_native_q2_source_invocation_begin(struct application_native_q2 *,qa_actor_id,
    struct application_native_q2_source_invocation **,qa_error *);
bool application_native_q2_source_invocation_guard(struct application_native_q2 *,
    const application_native_q2_source_authority *,const qa_error *,qa_error *);
void application_native_q2_source_invocation_unguard(struct application_native_q2 *,
    const qa_error *,const qa_error *);
bool application_native_q2_source_invocation_accepts(void *,const qa_error *);
bool application_native_q2_source_invocation_close(struct application_native_q2_source_invocation **,qa_error *);
bool application_native_q2_source_invocation_drain(struct application_native_q2 *,qa_error *);
bool application_native_q2_source_invoke_original(struct application_native_q2 *,qa_actor_id,
    qa_native_entry_observer *,const qa_native_value *,size_t,qa_native_value *,bool *,qa_error *);
bool application_native_q2_source_original(struct application_native_q2 *,
    qa_native_entry_observer *,const qa_native_value *,size_t,qa_native_value *,qa_error *);
#endif
