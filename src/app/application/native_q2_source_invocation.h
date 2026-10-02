#ifndef APPLICATION_NATIVE_Q2_SOURCE_INVOCATION_H
#define APPLICATION_NATIVE_Q2_SOURCE_INVOCATION_H
#include "qa/native_observe.h"
struct application_native_q2;
struct application_native_q2_source_invocation;
bool application_native_q2_source_invocation_guard(struct application_native_q2 *,qa_error *);
bool application_native_q2_source_invoke_original(struct application_native_q2 *,qa_actor_id,
    qa_native_entry_observer *,const qa_native_value *,size_t,qa_native_value *,bool *,qa_error *);
bool application_native_q2_source_original(struct application_native_q2 *,
    qa_native_entry_observer *,const qa_native_value *,size_t,qa_native_value *,qa_error *);
#endif
