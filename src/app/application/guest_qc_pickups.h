#ifndef APPLICATION_GUEST_QC_PICKUPS_H
#define APPLICATION_GUEST_QC_PICKUPS_H
#include "guest_qc_profile.h"
bool application_qc_pickups_qualify(application_provider *,const qa_json_document *,qa_json_id,qa_error *);
void application_qc_pickups_profile_free(struct application_qc_pickups *);
bool application_qc_pickups_admit(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_pickups_release(struct application_qc_state *,qa_actor_id,qa_error *);
bool application_qc_pickups_close(struct application_qc_state *,qa_error *);
bool application_qc_pickups_ready(struct application_qc_state *,qa_error *);
bool application_qc_pickups_saved_rule(application_provider *,qa_actor_id,qa_actor_owner,uint64_t,uint32_t,qa_pickup_rule *,qa_error *);
#endif
