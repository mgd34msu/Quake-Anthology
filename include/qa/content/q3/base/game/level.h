/* Port of src/content/q3/base/game/level.ts. GPL-2.0-or-later.
 * TS GameLevel extends MatchState: base IS the team-arena match state, the single
 * owner of clock, intermission, ranks and votes (match/command/admission hosts
 * point at &level->base). */
#ifndef QA_CONTENT_Q3_BASE_GAME_LEVEL_H
#define QA_CONTENT_Q3_BASE_GAME_LEVEL_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/team_arena/match.h"
typedef struct qa_q3_game_level {
  qa_q3_ta_match_state base; double frame_num, previous_time; bool new_session; double fry_sound;
  qa_q3_base_player_state_slots team_scores; /* TS PlayerStateSlots(4) over team_score_words */
  int32_t team_score_words[4];
} qa_q3_game_level;
void qa_q3_game_level_init(qa_q3_game_level *level);
void qa_q3_game_level_destroy(qa_q3_game_level *level);
void qa_q3_game_level_clear(qa_q3_game_level *level);
#endif
