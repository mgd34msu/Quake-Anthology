#ifndef QA_MODES_Q3_SESSION_H
#define QA_MODES_Q3_SESSION_H

#include "qa/modes.h"

typedef enum qa_mode_q3_spectator_state {
    QA_MODE_Q3_SPECTATOR_NOT,
    QA_MODE_Q3_SPECTATOR_FREE,
    QA_MODE_Q3_SPECTATOR_FOLLOW,
    QA_MODE_Q3_SPECTATOR_SCOREBOARD
} qa_mode_q3_spectator_state;
typedef struct qa_mode_q3_session {
    int32_t team, spectator_time_ms, spectator_state, spectator_client;
    int32_t wins, losses, team_leader;
} qa_mode_q3_session;

/* A native client has the actual selected source owner's zero-based slot.
 * Userinfo remains owned and captured by the actual application client owner. */
bool qa_modes_q3_session_read(qa_modes *, qa_mode_id, qa_actor_id,
    qa_actor_owner source_owner, uint32_t *source_slot, qa_mode_q3_session *, qa_error *);
/* After player/member admission and before native ClientBegin. This restores
 * the existing source session without team-change death or admission events. */
bool qa_modes_q3_session_restore(qa_modes *, qa_mode_id, qa_actor_id,
    qa_actor_owner source_owner, uint32_t source_slot, const qa_mode_q3_session *, qa_error *);
/* Resolve preserved numeric follow slots after the complete real roster is
 * admitted. Missing clients keep their source slot but project no actor. */
bool qa_modes_q3_session_reconnect(qa_modes *, qa_mode_id, qa_actor_owner source_owner,
                                   qa_error *);
/* Retain the actual mode identity, rules and effective settings after all
 * canonical actors retire. Authored objects/spawnpoints and clients must be
 * admitted again through their ordinary producers. */
bool qa_modes_q3_round_reset(qa_modes *, qa_mode_id, int32_t source_time_ms,
                              int32_t restarted, qa_error *);

#endif
