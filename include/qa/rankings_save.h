#ifndef QA_RANKINGS_SAVE_H
#define QA_RANKINGS_SAVE_H
#include "qa/rankings.h"

typedef struct qa_rankings_checkpoint_player {
    int32_t slot;
    qa_ranking_player state;
} qa_rankings_checkpoint_player;
typedef struct qa_rankings_continuation {
    qa_ranking_state state;
    uint64_t match;
    bool has_match;
    const qa_rankings_checkpoint_player *players;
    size_t count, capacity;
} qa_rankings_continuation;
/* Bindings identify actual installed services. Capture returns owned opaque
 * logical identity bytes; restore resolves an already prepared candidate
 * service, whose functions/context/endpoint must match the stable owner.
 * All callbacks are readonly: no login/report/logout/poll, lifecycle mutation,
 * or service construction. Configured providers additionally qualify the real
 * backend's exact saved match/account continuation (including an inactive
 * failed begin's retained match value). This codec does not clone a backend. */
typedef struct qa_rankings_checkpoint_refs {
    void *context;
    bool (*provider_capture)(void *, const qa_ranking_provider *, qa_buffer *empty, qa_error *);
    bool (*provider_resolve)(void *, qa_bytes, qa_ranking_provider *, qa_error *);
    bool (*observers_capture)(void *, const qa_ranking_observers *, qa_buffer *empty, qa_error *);
    bool (*observers_resolve)(void *, qa_bytes, qa_ranking_observers *, qa_error *);
    bool (*continuation_ready)(void *, const qa_ranking_provider *,
                               const qa_rankings_continuation *, qa_error *);
} qa_rankings_checkpoint_refs;

/* Same real empty lifecycle as create, with ordinary operations gated until
 * publication and callback-free disposal while the candidate is pending. */
bool qa_rankings_create_restored(const qa_ranking_provider *, const qa_ranking_observers *,
                                 qa_rankings **empty, qa_error *);
bool qa_rankings_provider_configured(const qa_rankings *);
typedef bool (*qa_rankings_handoff_fn)(void *, qa_rankings *active, qa_rankings *candidate,
                                      bool *relinquish_active, qa_error *);
/* Invoke an actual external ownership transfer with both lifecycle operations
 * fenced. Failure preserves the caller's result; callback must preserve both
 * backend ownerships on failure. This does not invoke ranking source methods. */
bool qa_rankings_handoff(qa_rankings *active, qa_rankings *candidate,
                         qa_rankings_handoff_fn, void *, bool *relinquish_active, qa_error *);
bool qa_rankings_checkpoint(const qa_rankings *, const qa_rankings_checkpoint_refs *,
                            qa_buffer *empty, qa_error *);
bool qa_rankings_restore(qa_rankings *, const qa_rankings_checkpoint_refs *, qa_bytes, qa_error *);
void qa_rankings_publish_restored(qa_rankings *);
/* After the external backend's successful ownership handoff, prevent replaced
 * source teardown from logging out/finishing the continued match. The old
 * local owner remains destroyable, with no backend/observer callbacks. */
void qa_rankings_relinquish_continuation(qa_rankings *);
#endif
