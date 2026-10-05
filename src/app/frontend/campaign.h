#ifndef QA_FRONTEND_CAMPAIGN_H
#define QA_FRONTEND_CAMPAIGN_H
#include "internal.h"
#include "qa/application_q3_campaign.h"
#include "qa/arena_progress_catalog.h"
#include "qa/team_arena_progress.h"
typedef struct frontend_campaign frontend_campaign;
bool frontend_campaign_sync(qa_frontend *,qa_error *);
bool frontend_campaign_source_command(void *,qa_application *,const qa_command_invocation *,bool *,qa_error *);
bool frontend_campaign_drain(qa_frontend *,qa_error *);
bool frontend_campaign_ready(const qa_frontend *);
void frontend_campaign_audio_stopped(qa_frontend *);
bool frontend_campaign_destroy(qa_frontend *,qa_error *);
#endif
