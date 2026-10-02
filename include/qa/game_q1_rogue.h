#ifndef QA_GAME_Q1_ROGUE_H
#define QA_GAME_Q1_ROGUE_H

#include "qa/game_q1.h"

typedef enum qa_q1_rogue_field {
    QA_Q1_ROGUE_FIELD_STEAM,
    QA_Q1_ROGUE_FIELD_FLAGS,
    QA_Q1_ROGUE_FIELD_KILLED,
    QA_Q1_ROGUE_FIELD_SUICIDE_COUNT,
    QA_Q1_ROGUE_FIELD_LAST_HURT_CARRIER,
    QA_Q1_ROGUE_FIELD_LAST_FRAGGED_CARRIER,
    QA_Q1_ROGUE_FIELD_LAST_RETURNED_FLAG,
    QA_Q1_ROGUE_FIELD_FLAG_SINCE,
    QA_Q1_ROGUE_FIELD_FLY_SOUND,
    QA_Q1_ROGUE_FIELDS
} qa_q1_rogue_field;

/* State is a genuine source actor, created lazily for the full physical client.
 * Lookup/current never allocates or replays source callbacks. Its raw words
 * belong to the native actor checkpoint, including nonnumeric authored text. */
bool qa_q1_rogue_state(qa_q1_game *, qa_actor_id player, qa_actor_id *state, qa_error *);
bool qa_q1_rogue_state_find(const qa_q1_game *, qa_actor_id player,
    qa_actor_id *state, bool *found, qa_error *);
bool qa_q1_rogue_state_current(const qa_q1_game *, qa_actor_id player,
    qa_actor_id state, qa_error *);
bool qa_q1_rogue_number_read(qa_q1_game *, qa_actor_id state,
    qa_q1_rogue_field, double *, qa_error *);
bool qa_q1_rogue_number_write(qa_q1_game *, qa_actor_id state,
    qa_q1_rogue_field, double, qa_error *);
bool qa_q1_rogue_field_read(const qa_q1_game *, qa_actor_id state,
    qa_q1_rogue_field, qa_bytes *, qa_error *);
bool qa_q1_rogue_field_write(qa_q1_game *, qa_actor_id state,
    qa_q1_rogue_field, qa_bytes, qa_error *);
bool qa_q1_rogue_world_update_read(qa_q1_game *, double *, qa_error *);
bool qa_q1_rogue_world_update_write(qa_q1_game *, double, qa_error *);

#endif
