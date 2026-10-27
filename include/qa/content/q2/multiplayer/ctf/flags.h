/* Port of src/content/q2/multiplayer/ctf/flags.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_FLAGS_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_FLAGS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
bool qa_ctf_flag_team(const char *classname, qa_q2_ctf_playing_team *out); /* false = null */
bool qa_ctf_can_see(qa_q2_entity *target, qa_q2_entity *viewer, qa_q2_game_services *game);
typedef struct qa_q2_ctf_flags qa_q2_ctf_flags;
qa_q2_ctf_flags *qa_q2_ctf_flags_create(qa_q2_ctf_context *context);
void qa_q2_ctf_flags_destroy(qa_q2_ctf_flags *flags);
qa_q2_callback_definitions qa_q2_ctf_flags_callbacks(qa_q2_ctf_flags *flags);
void qa_q2_ctf_flags_register(qa_q2_ctf_flags *flags);
bool qa_q2_ctf_flags_spawn(qa_q2_ctf_flags *flags, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_entity *qa_q2_ctf_flags_base(qa_q2_ctf_flags *flags, qa_q2_game_services *game, qa_q2_ctf_playing_team team);
const char *qa_q2_ctf_flags_state(qa_q2_ctf_flags *flags, qa_q2_game_services *game, qa_q2_ctf_playing_team team); /* "base"|"dropped"|"taken" */
void qa_q2_ctf_flags_reset(qa_q2_ctf_flags *flags, qa_q2_game_services *game, qa_q2_ctf_playing_team team);
void qa_q2_ctf_flags_reset_all(qa_q2_ctf_flags *flags, qa_q2_game_services *game);
qa_q2_entity *qa_q2_ctf_flags_drop(qa_q2_ctf_flags *flags, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_flags_hurt_carrier(qa_q2_ctf_flags *flags, qa_actor_id target, const qa_actor_id *attacker, qa_q2_game_services *game);
void qa_q2_ctf_flags_frag(qa_q2_ctf_flags *flags, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game);
void qa_q2_ctf_flags_effects(qa_q2_ctf_flags *flags, qa_q2_entity *entity, qa_q2_game_services *game);
#endif
