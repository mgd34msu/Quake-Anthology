#ifndef QA_GAME_Q1_SOURCE_RUNES_H
#define QA_GAME_Q1_SOURCE_RUNES_H

#include "qa/game_q1.h"

typedef enum qa_q1_source_rune {
    QA_Q1_RUNE_RESISTANCE,
    QA_Q1_RUNE_STRENGTH,
    QA_Q1_RUNE_HASTE,
    QA_Q1_RUNE_REGENERATION,
    QA_Q1_RUNE_COUNT
} qa_q1_source_rune;

typedef struct qa_q1_source_runes_services {
    void *context;
    bool (*current)(void *, const qa_q1_game *, qa_error *);
    bool (*touch)(void *, qa_actor_id rune, qa_actor_id player, qa_error *);
} qa_q1_source_runes_services;

bool qa_q1_source_runes_configure(qa_q1_game *, const qa_q1_source_runes_services *, qa_error *);
bool qa_q1_source_rune_read(const qa_q1_game *, qa_actor_id, qa_q1_source_rune *, qa_error *);
/* Creation, scheduling and removal belong to the actual native source entity.
 * The composition owns canonical inventory, messages and player-word effects. */
bool qa_q1_source_rune_drop(qa_q1_game *, qa_q1_source_rune, qa_vec3 origin,
    qa_actor_id *, qa_error *);
bool qa_q1_source_rune_collect(qa_q1_game *, qa_actor_id, qa_error *);

#endif
