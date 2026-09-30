#ifndef QA_APPLICATION_NATIVE_Q2_COMBAT_H
#define QA_APPLICATION_NATIVE_Q2_COMBAT_H
#include "qa/native_host.h"
struct application_native_q2;
bool application_native_q2_combat_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_combat_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_combat_admit(struct application_native_q2 *, uint32_t,
    qa_actor_id, bool admitting, qa_error *);
bool application_native_q2_combat_detach(struct application_native_q2 *, qa_actor_id, qa_error *);
void application_native_q2_combat_released(struct application_native_q2 *, qa_actor_id);
bool application_native_q2_combat_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_combat_close(struct application_native_q2 *, qa_error *);
struct application_provider;
bool application_native_q2_combat_binding(struct application_provider *, qa_actor_id,
    uint64_t saved_serial, qa_combat_binding *, qa_error *);
bool application_native_q2_combat_finish(struct application_provider *, qa_error *);
#endif
