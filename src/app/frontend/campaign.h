#ifndef QA_FRONTEND_CAMPAIGN_H
#define QA_FRONTEND_CAMPAIGN_H
#include "internal.h"
#include "qa/application_q3_campaign.h"
#include "qa/arena_progress_catalog.h"
#include "qa/team_arena_progress.h"
#include "qa/persistence_content.h"
typedef struct frontend_campaign frontend_campaign;
bool frontend_campaign_sync(qa_frontend *,qa_error *);
bool frontend_campaign_source_command(void *,qa_application *,const qa_command_invocation *,bool *,qa_error *);
bool frontend_campaign_drain(qa_frontend *,qa_error *);
bool frontend_campaign_ready(const qa_frontend *);
void frontend_campaign_audio_stopped(qa_frontend *);
bool frontend_campaign_destroy(qa_frontend *,qa_error *);
bool frontend_campaign_content_visit(const qa_frontend *,const qa_application_content_visitor *,qa_error *);
bool frontend_campaign_checkpoint(qa_frontend *,const qa_application_content_graph *,qa_buffer *,qa_error *);
/* Import after actual GAME/source private state and frontend source groups.
 * Pure candidate construction never runs normal setup or archive writes. */
bool frontend_campaign_restore(qa_frontend *,qa_application_content_graph *,qa_bytes,qa_error *);
bool frontend_campaign_publish_ready(const qa_frontend *,qa_error *);
void frontend_campaign_publish_restored(qa_frontend *);
#endif
