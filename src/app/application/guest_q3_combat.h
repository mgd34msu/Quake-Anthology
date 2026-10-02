#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_H

#include "guest_q3_combat_state.h"
#include "qa/qvm_save.h"

typedef struct application_q3_combat application_q3_combat;
bool application_q3_combat_create(struct q3g_role *, const application_q3_combat_profile *,
    application_q3_combat **, qa_error *);
bool application_q3_combat_destroy(application_q3_combat **, qa_error *);
bool application_q3_combat_idle(const application_q3_combat *);
bool application_q3_combat_admit(application_q3_combat *, qa_actor_id, qa_error *);
bool application_q3_combat_binding(application_q3_combat *, qa_actor_id, uint64_t,
    qa_combat_binding *, qa_error *);
bool application_q3_combat_actor_released(application_q3_combat *, qa_actor_record, qa_error *);
size_t application_q3_combat_descriptor_count(const application_q3_combat *);
bool application_q3_combat_descriptors(const application_q3_combat *, qa_qvm_saved_function *, size_t, qa_error *);
void application_q3_combat_adopt(application_q3_combat *, const qa_qvm_binding *);
bool application_q3_combat_sequence_read(const application_q3_combat *, uint64_t *, qa_error *);
void application_q3_combat_sequence_adopt(application_q3_combat *, uint64_t);

#endif
