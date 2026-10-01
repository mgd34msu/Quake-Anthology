#ifndef QA_FRONTEND_RANKINGS_H
#define QA_FRONTEND_RANKINGS_H
#include "qa/frontend.h"
#include "qa/application_rankings.h"

bool frontend_ranking_effect(void *, qa_application *, qa_actor_owner, qa_actor_id,
    qa_application_ranking_effect, const qa_ranking_player *, qa_error *);
/* Borrows the actual prepared frontend for the capture/restore operation. */
qa_application_ranking_checkpoint_refs frontend_ranking_refs(qa_frontend *);
#endif
