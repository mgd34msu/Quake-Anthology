/* Port of src/content/q2/multiplayer/lmctf/spawns.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_SPAWNS_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_SPAWNS_H
#include "qa/content/q2/multiplayer/lmctf/types.h"
qa_q2_entity *qa_lmctf_team_spawn(qa_lmctf_context *context, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_select_lmctf_spawn(qa_lmctf_context *context, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 *out_origin, qa_vec3 *out_angles);
#endif
