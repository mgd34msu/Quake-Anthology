#ifndef QA_GAME_Q1_SOURCE_ROGUE_FLAGS_H
#define QA_GAME_Q1_SOURCE_ROGUE_FLAGS_H

#include "qa/game_q1.h"

typedef struct qa_q1_source_rogue_flags_services {
    void *context;
    bool (*current)(void *, const qa_q1_game *, qa_error *);
    bool (*touch)(void *, qa_actor_id flag, qa_actor_id player, bool base, qa_error *);
    bool (*return_flag)(void *, qa_actor_id flag, qa_error *);
    bool (*drop_flag)(void *, qa_actor_id flag, qa_error *);
    bool (*player_frame)(void *, qa_actor_id player, double *, qa_error *);
    bool (*carrier)(void *, qa_actor_id flag, qa_actor_id player, bool *, qa_error *);
} qa_q1_source_rogue_flags_services;

typedef struct qa_q1_source_rogue_flag_view {
    qa_actor_id actor, owner;
    double team, count;
    bool base, placed;
} qa_q1_source_rogue_flag_view;

bool qa_q1_source_rogue_flags_configure(qa_q1_game *,
    const qa_q1_source_rogue_flags_services *, qa_error *);
bool qa_q1_source_rogue_flag_read(qa_q1_game *, qa_actor_id,
    qa_q1_source_rogue_flag_view *, qa_error *);
/* Caller owns the returned physical-source-order actor array. */
bool qa_q1_source_rogue_flags_snapshot(qa_q1_game *, qa_actor_id **, size_t *, qa_error *);
bool qa_q1_source_rogue_flag_return(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_rogue_flag_drop(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_rogue_flag_carry(qa_q1_game *, qa_actor_id, qa_actor_id, qa_error *);

#endif
