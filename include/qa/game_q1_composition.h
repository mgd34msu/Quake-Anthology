#ifndef QA_GAME_Q1_COMPOSITION_H
#define QA_GAME_Q1_COMPOSITION_H

#include "qa/game_q1.h"

/* Genuine Q1SourceClients globals, independent of selected-rule scores. */
bool qa_q1_source_captures_read(const qa_q1_game *, double *red, double *blue, qa_error *);
bool qa_q1_source_capture_add(qa_q1_game *, bool blue, double *total, qa_error *);
bool qa_q1_source_random(qa_q1_game *, double *, qa_error *);
/* Submit the composition's genuine direct source damage, retaining its
 * clock, attack sequence, source providers and ordinary damage geometry. */
bool qa_q1_source_damage(qa_q1_game *, qa_actor_id target, qa_actor_id inflictor,
    qa_actor_id attacker, float amount, const char *cause, qa_error *);

#endif
