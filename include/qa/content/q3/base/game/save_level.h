/* Port of src/content/q3/base/game/save-level.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_LEVEL_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_LEVEL_H
#include <stddef.h>
#include "qa/content/q3/base/game/level.h"
#include "qa/content/q3/base/game/save_values.h"
#include "qa/persistence/value.h"
typedef struct qa_q3_level_capture {
  qa_q3_level_values values; double *team_scores; size_t team_score_count;
  double num_team_voting_clients[2]; double *sorted_clients; size_t sorted_count;
  qa_q3_ta_vote_state vote; qa_q3_ta_team_vote_state team_votes[2];
} qa_q3_level_capture;
qa_q3_level_capture qa_q3_capture_level(qa_q3_game_level *level);
void qa_q3_level_capture_destroy(qa_q3_level_capture *capture);
void qa_q3_restore_level(qa_q3_game_level *level, const qa_save_value *value);
#endif
