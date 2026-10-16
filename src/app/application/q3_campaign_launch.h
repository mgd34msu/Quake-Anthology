#ifndef QA_APPLICATION_Q3_CAMPAIGN_LAUNCH_H
#define QA_APPLICATION_Q3_CAMPAIGN_LAUNCH_H
#include "internal.h"
#include "qa/application_q3_campaign.h"

bool application_q3_campaign_launch_cvars(application_provider *, qa_cvars *, uint64_t cvar_owner, qa_error *);
bool application_q3_campaign_launch_guest_handoff(application_provider *, qa_cvars *, uint64_t cvar_owner, qa_error *);
bool application_q3_campaign_launch_retired(qa_application *, application_publication *, qa_cvars *, qa_error *);
bool application_q3_campaign_launch_admitted(qa_application *, application_publication *, qa_error *);
bool application_q3_campaign_launch_finish(qa_application *, bool published, qa_error *);
bool application_q3_map_launch_prepare(qa_application *, qa_launch_draft *, qa_map_launch_policy, qa_error *);
bool application_q3_map_launch_finish(qa_application *, qa_map_launch_policy, qa_error *);
#endif
