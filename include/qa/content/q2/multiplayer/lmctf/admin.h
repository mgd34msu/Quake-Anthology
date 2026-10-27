/* Port of src/content/q2/multiplayer/lmctf/admin.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_ADMIN_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_ADMIN_H
#include "qa/content/q2/multiplayer/lmctf/match.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
bool qa_lmctf_admin_command(qa_lmctf_context *context, qa_lmctf_match *match, qa_q2_entity *entity,
                            qa_q2_game_services *game, const char *name, const char *const *args, size_t arg_count);
#endif
