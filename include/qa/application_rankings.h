#ifndef QA_APPLICATION_RANKINGS_H
#define QA_APPLICATION_RANKINGS_H

#include "qa/actors.h"
#include "qa/rankings.h"

typedef struct qa_application qa_application;
/* The native GAME client binding is independent of the actor's selected
 * character owner and source slot. Absence leaves the output unchanged. */
bool qa_application_rankings_client_slot(const qa_application *, qa_actor_id, uint32_t *);
/* Start the actual published map's match after its output consumer is ready.
 * Existing match ownership is retained without replaying a ranking frame. */
bool qa_application_rankings_start(qa_application *, qa_error *);
bool qa_application_rankings_account(qa_application *, int32_t source_slot,
    const qa_ranking_request *, qa_error *);
bool qa_application_rankings_reset(qa_application *, int32_t source_slot, qa_error *);
bool qa_application_rankings_spectate(qa_application *, int32_t source_slot, qa_error *);

#endif
