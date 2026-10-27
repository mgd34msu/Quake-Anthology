/* Port of src/content/q3/base/game/level.ts. GPL-2.0-or-later.
 * Mirror of ../../team-arena/match.ts MatchState/VoteState/TeamVoteState (TS owner unported). */
#ifndef QA_CONTENT_Q3_BASE_GAME_LEVEL_H
#define QA_CONTENT_Q3_BASE_GAME_LEVEL_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
typedef struct qa_q3_vote_state_mirror { double time, yes, no, needed; char *string; bool executed; } qa_q3_vote_state_mirror;
typedef struct qa_q3_match_state_mirror {
  double time, start_time, warmup_time, warmup_modification_count; bool restarted;
  double num_connected_clients, num_non_spectator_clients, num_playing_clients, num_voting_clients;
  double num_team_voting_clients[2]; double sorted_clients[QA_Q3_MAX_CLIENTS]; double follow1, follow2;
  double intermission_time, intermission_queued; qa_vec3 intermission_origin, intermission_angle;
  char *changemap; bool ready_to_exit; double exit_time;
  qa_q3_vote_state_mirror vote, team_votes[2];
} qa_q3_match_state_mirror;
typedef struct qa_q3_game_level {
  qa_q3_match_state_mirror base; double frame_num, previous_time; bool new_session; double fry_sound;
  qa_q3_game_player_slots_mirror team_scores;
} qa_q3_game_level;
void qa_q3_game_level_init(qa_q3_game_level *level);
void qa_q3_game_level_destroy(qa_q3_game_level *level);
void qa_q3_game_level_clear(qa_q3_game_level *level);
#endif
