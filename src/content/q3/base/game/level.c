/* Port of src/content/q3/base/game/level.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/level.h"
#include <stdlib.h>
#include <string.h>
void qa_q3_game_level_init(qa_q3_game_level *level) {
  memset(level, 0, sizeof *level);
  (void)qa_q3_base_player_state_slots_init(&level->team_scores, 4, level->team_score_words, 4, true, NULL, NULL, NULL);
  qa_q3_ta_match_state_init(&level->base);
}
void qa_q3_game_level_destroy(qa_q3_game_level *level) {
  qa_q3_ta_match_state_destroy(&level->base);
}
void qa_q3_game_level_clear(qa_q3_game_level *level) {
  /* TS assigns a fresh GameLevel over this one, keeping the identity of
   * teamScores, the client tables, and the vote records: embedded storage
   * keeps its address here, so zero and re-initialize in place. */
  for (double i = 0; i < level->team_scores.length; i++) qa_q3_base_player_state_slots_set(&level->team_scores, i, 0);
  qa_q3_base_player_state_slots scores = level->team_scores;
  qa_q3_ta_match_state_destroy(&level->base);
  memset(level, 0, sizeof *level); level->team_scores = scores;
  qa_q3_ta_match_state_init(&level->base);
}
