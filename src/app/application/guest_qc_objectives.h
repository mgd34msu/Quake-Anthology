#ifndef APPLICATION_GUEST_QC_OBJECTIVES_H
#define APPLICATION_GUEST_QC_OBJECTIVES_H

#include "guest_qc_internal.h"
#include "qa/json.h"

struct application_qc_profile;
struct application_qc_objectives;

bool application_qc_objectives_qualify(application_provider *, const qa_json_document *,
    qa_json_id, qa_error *);
void application_qc_objectives_release(struct application_qc_profile *);
bool application_qc_objectives_activate(struct application_qc_state *, qa_error *);
bool application_qc_objectives_suspend(struct application_qc_state *, qa_error *);
bool application_qc_objectives_sync(struct application_qc_state *, qa_error *);
bool application_qc_objectives_restored(struct application_qc_state *, qa_error *);
bool application_qc_objectives_ready(const struct application_qc_state *, qa_error *);

#endif
