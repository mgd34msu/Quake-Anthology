#ifndef QA_APPLICATION_RANKINGS_H
#define QA_APPLICATION_RANKINGS_H

#include "qa/actors.h"
#include "qa/rankings.h"

typedef struct qa_application qa_application;
typedef enum qa_application_ranking_effect {
    QA_APPLICATION_RANKING_STATUS,
    QA_APPLICATION_RANKING_MENU
} qa_application_ranking_effect;
/* Ordinary ranked-match output uses the actual prepared frontend context.
 * Status borrows its value during this call. Remote players without a local
 * menu have no UI effect. An absent callback leaves this consumer uninstalled.
 * Import and readonly qualification never call this. */
typedef bool (*qa_application_ranking_effect_fn)(void *, qa_application *,
    qa_actor_owner, qa_actor_id, qa_application_ranking_effect,
    const qa_ranking_player *, qa_error *);
/* Identify the actual installed output bridge without invoking an effect.
 * Decode resolves an already prepared candidate bridge/context, not a new
 * service. Capture returns owned logical identity bytes. */
typedef struct qa_application_ranking_checkpoint_refs {
    void *context;
    bool (*capture)(void *, qa_application_ranking_effect_fn, void *, qa_buffer *, qa_error *);
    bool (*resolve)(void *, qa_bytes, qa_application_ranking_effect_fn *, void **, qa_error *);
} qa_application_ranking_checkpoint_refs;
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
