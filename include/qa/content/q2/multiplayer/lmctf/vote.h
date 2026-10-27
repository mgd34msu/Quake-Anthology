/* Port of src/content/q2/multiplayer/lmctf/vote.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_VOTE_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_VOTE_H
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/persistence/value.h"
typedef struct qa_lmctf_vote qa_lmctf_vote;
qa_lmctf_vote *qa_lmctf_vote_create(qa_lmctf_context *context);
void qa_lmctf_vote_destroy(qa_lmctf_vote *vote);
bool qa_lmctf_vote_started_at(qa_lmctf_vote *vote, double *out);
void qa_lmctf_vote_menu(qa_lmctf_vote *vote, qa_actor_id actor);
void qa_lmctf_vote_start(qa_lmctf_vote *vote, qa_actor_id actor, qa_q2_game_services *game);
void qa_lmctf_vote_ballot(qa_lmctf_vote *vote, qa_actor_id actor, qa_q2_game_services *game, bool yes);
void qa_lmctf_vote_frame(qa_lmctf_vote *vote, qa_q2_game_services *game);
bool qa_lmctf_vote_capture(qa_lmctf_vote *vote, double *out_started_at);
void qa_lmctf_vote_restore(qa_lmctf_vote *vote, const qa_save_reader *reader);
#endif
