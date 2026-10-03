#ifndef APPLICATION_GUEST_QC_ITEM_WEAPONS_H
#define APPLICATION_GUEST_QC_ITEM_WEAPONS_H
#include "guest_qc_profile.h"
struct application_qc_item_weapons;

bool application_qc_item_weapons_qualify(application_provider *, const qa_json_document *, qa_json_id, qa_error *);
void application_qc_item_weapons_profile_free(struct application_qc_item_weapons *);
bool application_qc_item_weapons_reserve(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_item_weapons_admit(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_item_weapons_finish(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_item_weapons_release(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_item_weapons_close(struct application_qc_state *, qa_error *);
const qa_qc_inline_region *application_qc_item_weapons_regions(const application_provider *, size_t *);
bool application_qc_item_weapons_replace(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_call_event *, qa_qc_call_next, bool *handled, qa_error *);
bool application_qc_item_weapons_inline(struct application_qc_state *, qa_qc_instance *,
    const qa_qc_inline_event *, qa_qc_inline_next, bool *handled, qa_error *);
bool application_qc_item_weapons_selected_read(struct application_qc_state *, qa_actor_id, qa_item_id *, qa_error *);
bool application_qc_item_weapons_accepts(struct application_qc_state *, qa_actor_id, qa_item_id, bool *, qa_error *);
bool application_qc_item_weapons_select(struct application_qc_state *, qa_actor_id, qa_item_id, bool *, qa_error *);
bool application_qc_item_weapons_resume(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_item_weapons_settled(struct application_qc_state *, qa_actor_id, bool *, qa_error *);
bool application_qc_item_weapons_model_read(struct application_qc_state *, qa_actor_id,
    const application_qc_resource **, float *, qa_error *);
#endif
