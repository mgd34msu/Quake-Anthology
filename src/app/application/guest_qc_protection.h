#ifndef APPLICATION_GUEST_QC_PROTECTION_H
#define APPLICATION_GUEST_QC_PROTECTION_H
#include "guest_qc_internal.h"
#include "qa/json.h"

typedef struct application_qc_protection_profile application_qc_protection_profile;
typedef struct application_qc_protection application_qc_protection;

bool application_qc_protection_qualify(application_provider *, const qa_json_document *,
    qa_json_id, qa_error *);
void application_qc_protection_profile_free(application_qc_protection_profile *);
bool application_qc_protection_channel_declared(const application_provider *, qa_protection_channel);
const qa_qc_inline_region *application_qc_protection_regions(
    const application_qc_protection_profile *, size_t *);
bool application_qc_protection_create(struct application_qc_state *, qa_error *);
bool application_qc_protection_reserve(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_protection_activate(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_protection_source_stored(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_store_event *, qa_error *);
bool application_qc_protection_release(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_protection_suspend(struct application_qc_state *, qa_error *);
bool application_qc_protection_destroy(struct application_qc_state *, qa_error *);
bool application_qc_protection_idle(const struct application_qc_state *);
bool application_qc_protection_ready(const struct application_qc_state *, qa_error *);
bool application_qc_protection_saved_binding(struct application_qc_state *, qa_actor_id,
    qa_protection_channel, const qa_protection_claim *, qa_protection_binding *, qa_error *);
bool application_qc_protection_restore_attach(struct application_qc_state *, qa_error *);

#endif
