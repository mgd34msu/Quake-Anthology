#ifndef QA_APPLICATION_NATIVE_Q3_VOTES_H
#define QA_APPLICATION_NATIVE_Q3_VOTES_H

#include "internal.h"

/* Source level vote state is separate from pers, sess and PS, which retain
 * their existing native GAME client owners. Init clears this level owner;
 * restore imports it without issuing commands or configstring writes. */
bool application_native_q3_votes_create(application_provider *, qa_error *);
bool application_native_q3_votes_destroy(application_provider *, qa_error *);
bool application_native_q3_votes_idle(const application_provider *);
/* A selected rules adapter uses this only for its native Q3 vote binding.
 * Other rule families keep their own vote producers. */
bool application_native_q3_votes_bound(const application_provider *);
bool application_native_q3_votes_reset(application_provider *, qa_error *);

/* The native dispatcher invokes this after its intermission chat gate. END
 * calls frame after CheckTournament, CheckExitRules and team status, before
 * CheckCvars. Neither entry runs the generic modes vote producer. */
bool application_native_q3_votes_command(application_provider *, qa_actor_id,
    const qa_command_invocation *, bool *handled, qa_error *);
bool application_native_q3_votes_frame(application_provider *, qa_error *);
/* SetTeam and successful team votes share the actual source leader producers.
 * CheckTeamLeader preserves its two unconditional source selection loops. */
bool application_native_q3_votes_set_leader(application_provider *, int32_t team,
    int32_t source_client, qa_error *);
bool application_native_q3_votes_check_team_leader(application_provider *, int32_t team,
    qa_error *);

bool application_native_q3_votes_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_votes_restore(application_provider *, qa_bytes, qa_error *);

#endif
