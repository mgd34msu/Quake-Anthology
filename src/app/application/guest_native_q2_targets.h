#ifndef APPLICATION_GUEST_NATIVE_Q2_TARGETS_H
#define APPLICATION_GUEST_NATIVE_Q2_TARGETS_H
#include "internal.h"

struct application_native_q2;
struct application_native_q2_targets;

bool application_native_q2_targets_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_targets_admit(struct application_native_q2 *, uint32_t,
    qa_actor_id, bool admitting, qa_error *);
bool application_native_q2_targets_refresh(struct application_native_q2 *, qa_error *);
bool application_native_q2_targets_released(struct application_native_q2 *, qa_actor_id,
    qa_error *);
bool application_native_q2_targets_close(struct application_native_q2 *, qa_error *);
bool application_native_q2_targets_binding(application_provider *, qa_actor_id,
    qa_target_binding *, qa_error *);

#endif
