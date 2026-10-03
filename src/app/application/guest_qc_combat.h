#ifndef APPLICATION_GUEST_QC_COMBAT_H
#define APPLICATION_GUEST_QC_COMBAT_H
#include "guest_qc_profile.h"

typedef struct application_qc_combat_profile application_qc_combat_profile;
typedef struct application_qc_combat application_qc_combat;

bool application_qc_combat_qualify(application_provider *, const qa_json_document *,
    qa_json_id, qa_error *);
void application_qc_combat_profile_free(application_qc_combat_profile *);
const qa_qc_inline_region *application_qc_combat_regions(
    const application_qc_combat_profile *, size_t *);
bool application_qc_combat_create(struct application_qc_state *, qa_error *);
bool application_qc_combat_prepare(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_entity_access *, qa_error *);
bool application_qc_combat_source_stored(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_store_event *, qa_error *);
bool application_qc_combat_health_owned(const struct application_qc_state *, qa_actor_id,
    const qa_qc_definition *);
bool application_qc_combat_replace(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_call_event *, qa_qc_call_next, bool *handled, qa_error *);
bool application_qc_combat_inline(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_inline_event *, qa_qc_inline_next, bool *handled, qa_error *);
bool application_qc_combat_release(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_combat_suspend(struct application_qc_state *, qa_error *);
bool application_qc_combat_destroy(struct application_qc_state *, qa_error *);
bool application_qc_combat_idle(const struct application_qc_state *);
bool application_qc_combat_ready(const struct application_qc_state *, qa_error *);
bool application_qc_combat_saved_binding(struct application_qc_state *, qa_actor_id,
    uint64_t serial, qa_combat_binding *, qa_error *);
bool application_qc_combat_restore_attach(struct application_qc_state *, qa_error *);
bool application_qc_combat_damage_amount(struct application_qc_state *, qa_actor_id,
    float amount, float *out, bool *available, qa_error *);

#endif
