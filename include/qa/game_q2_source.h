#ifndef QA_GAME_Q2_SOURCE_H
#define QA_GAME_Q2_SOURCE_H
#include "qa/game_q2_player.h"
#include "qa/console.h"

/* Copy current source values into the compiled GAME consumers. This does not
 * dispatch commands, apply latches, emit events, or replace host services.
 * Reset rotation only for an actual map-list current-value change; ordinary
 * refresh and import retain the source's shuffled order. */
bool qa_q2_source_apply(qa_q2_game *, const qa_cvars *, bool reset_rotation, qa_error *);
bool qa_q2_source_player_rules(const qa_cvars *, qa_q2_player_rules *, qa_error *);
uint32_t qa_q2_source_deathmatch_flags(const qa_cvars *);
/* Read through this GAME owner's configured source service. Standalone GAME
 * users without that service retain the supplied default. */
bool qa_q2_source_value(qa_q2_game *, const char *, float default_value, float *, qa_error *);
#endif
