/* Port of src/content/q3/base/game/level.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/level.h"
#include <stdlib.h>
#include <string.h>
void qa_q3_game_level_init(qa_q3_game_level *level) {
  memset(level, 0, sizeof *level); level->team_scores.length = 4;
}
void qa_q3_game_level_destroy(qa_q3_game_level *level) {
  free(level->base.changemap); free(level->base.vote.string);
  free(level->base.team_votes[0].string); free(level->base.team_votes[1].string);
  level->base.changemap = level->base.vote.string = NULL;
  level->base.team_votes[0].string = level->base.team_votes[1].string = NULL;
}
void qa_q3_game_level_clear(qa_q3_game_level *level) {
  /* Preserve arrays' storage identity: zero them, reset scalars/votes to fresh. */
  for (size_t i = 0; i < level->team_scores.length; i++) qa_q3_game_slots_set_mirror(&level->team_scores, i, 0);
  level->base.num_team_voting_clients[0] = level->base.num_team_voting_clients[1] = 0;
  memset(level->base.sorted_clients, 0, sizeof level->base.sorted_clients);
  char *vote_str = level->base.vote.string, *tv0 = level->base.team_votes[0].string, *tv1 = level->base.team_votes[1].string;
  char *changemap = level->base.changemap;
  qa_q3_game_player_slots_mirror scores = level->team_scores;
  memset(level, 0, sizeof *level); level->team_scores = scores; level->team_scores.length = 4;
  free(vote_str); free(tv0); free(tv1); free(changemap);
}
