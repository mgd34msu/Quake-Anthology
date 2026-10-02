#ifndef QA_GAME_Q1_SOURCE_ROGUE_RUNES_H
#define QA_GAME_Q1_SOURCE_ROGUE_RUNES_H

#include "qa/game_q1.h"

/* This is the actual RogueRunes owner. Its state is keyed by full canonical
 * actor identity and is independent of selected inventory relics. */
bool qa_q1_source_rogue_runes_frame(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_rogue_runes_drop(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_rogue_runes_damage(qa_q1_game *, qa_actor_id, float *, qa_error *);
bool qa_q1_source_rogue_runes_resistance(qa_q1_game *, qa_actor_id, float *, qa_error *);
bool qa_q1_source_rogue_runes_before_fire(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_rogue_runes_attack_delay(qa_q1_game *, qa_actor_id, float *, qa_error *);
bool qa_q1_source_rogue_runes_read(const qa_q1_game *, qa_actor_id, uint32_t *, bool *found, qa_error *);

#endif
