/* Port of src/content/q2/multiplayer/lmctf/match.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_MATCH_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_MATCH_H
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/persistence/value.h"
typedef struct qa_lmctf_match_checkpoint {
  bool has_pending_map; qa_lmctf_map_change pending_map;
  const char *phase; double remaining, next_think; bool paused, teams_locked;
} qa_lmctf_match_checkpoint;
typedef struct qa_lmctf_match qa_lmctf_match;
qa_lmctf_match *qa_lmctf_match_create(qa_lmctf_context *context);
void qa_lmctf_match_destroy(qa_lmctf_match *match);
const char *qa_lmctf_match_phase(qa_lmctf_match *match);
void qa_lmctf_match_set_phase(qa_lmctf_match *match, const char *phase);
const qa_lmctf_map_change *qa_lmctf_match_pending_map(qa_lmctf_match *match);
double qa_lmctf_match_remaining(qa_lmctf_match *match);
double qa_lmctf_match_next_think(qa_lmctf_match *match);
bool qa_lmctf_match_paused(qa_lmctf_match *match);
void qa_lmctf_match_set_paused(qa_lmctf_match *match, bool paused);
bool qa_lmctf_match_teams_locked(qa_lmctf_match *match);
void qa_lmctf_match_set_teams_locked(qa_lmctf_match *match, bool locked);
void qa_lmctf_match_toggle_pause(qa_lmctf_match *match, qa_q2_game_services *game);
void qa_lmctf_match_change_map(qa_lmctf_match *match, const char *map, bool countdown);
bool qa_lmctf_match_can_score(qa_lmctf_match *match);
void qa_lmctf_match_start(qa_lmctf_match *match, qa_q2_game_services *game);
void qa_lmctf_match_stop(qa_lmctf_match *match);
void qa_lmctf_match_frame(qa_lmctf_match *match, qa_q2_game_services *game);
qa_lmctf_match_checkpoint qa_lmctf_match_capture(qa_lmctf_match *match); /* pending map borrowed */
void qa_lmctf_match_restore(qa_lmctf_match *match, const qa_save_reader *reader);
#endif
