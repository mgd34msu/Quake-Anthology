#ifndef APPLICATION_GUEST_QC_TARGETS_H
#define APPLICATION_GUEST_QC_TARGETS_H
#include "guest_qc_internal.h"

bool application_qc_target_binding(application_provider *, qa_actor_id,
    qa_target_binding *, qa_error *);
bool application_qc_acquire_enemy(application_provider *, qa_actor_id,
    qa_actor_id, qa_error *);
bool application_qc_mark_monster_target(application_provider *, qa_actor_id,
    qa_error *);
bool application_qc_targets_bind(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_entity_access *, qa_error *);
bool application_qc_targets_stored(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_store_event *, qa_error *);
void application_qc_targets_close(struct application_qc_state *);

#endif
