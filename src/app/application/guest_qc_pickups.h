#ifndef APPLICATION_GUEST_QC_PICKUPS_H
#define APPLICATION_GUEST_QC_PICKUPS_H
#include "guest_qc_profile.h"
bool application_qc_pickups_qualify(application_provider *,const qa_json_document *,qa_json_id,qa_error *);
void application_qc_pickups_profile_free(struct application_qc_pickups *);
bool application_qc_pickups_create(struct application_qc_state *,qa_error *);
void application_qc_pickups_destroy(struct application_qc_state *);
bool application_qc_pickups_admit(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_pickups_release(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_pickups_close(struct application_qc_state *,qa_error *);
bool application_qc_pickups_ready(struct application_qc_state *,qa_error *);
bool application_qc_pickups_saved_rule(application_provider *,qa_actor_id,qa_actor_owner,uint64_t,uint32_t,qa_pickup_rule *,qa_error *);
const qa_qc_inline_region *application_qc_pickups_regions(const application_provider *,size_t *);
bool application_qc_pickups_replace(struct application_qc_state *,qa_qc_instance *,
    const qa_qc_call_event *,qa_qc_call_next,bool *handled,qa_error *);
bool application_qc_pickups_inline(struct application_qc_state *,qa_qc_instance *,
    const qa_qc_inline_event *,qa_qc_inline_next,bool *handled,qa_error *);
bool application_qc_pickups_source_stored(struct application_qc_state *,qa_qc_instance *,
    const qa_qc_store_event *,qa_error *);
bool application_qc_pickups_idle(const struct application_qc_state *);
bool application_qc_pickups_source_present(const application_provider *);
qa_item_id application_qc_pickups_cargo_item(const application_provider *);
bool application_qc_pickups_supply(struct application_qc_state *,const qa_pickup_offer *,
    qa_supply_offer *,qa_supply_options *,qa_error *);
#endif
