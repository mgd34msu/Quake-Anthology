/* Port of src/content/q2/multiplayer/lmctf/flags.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_FLAGS_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_FLAGS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/contracts/session.h"
typedef struct qa_lmctf_flag_checkpoint { qa_lmctf_playing_team team; qa_saved_actor_id actor; } qa_lmctf_flag_checkpoint;
typedef struct qa_lmctf_flags_checkpoint {
  qa_lmctf_flag_checkpoint *flags; size_t flag_count;
  double last_taken_sound;
} qa_lmctf_flags_checkpoint;
typedef struct qa_lmctf_flags qa_lmctf_flags;
qa_lmctf_flags *qa_lmctf_flags_create(qa_lmctf_context *context);
void qa_lmctf_flags_destroy(qa_lmctf_flags *flags);
qa_q2_callback_definitions qa_lmctf_flags_callbacks(qa_lmctf_flags *flags);
/* flags array malloc'd; free with free(3). */
void qa_lmctf_flags_capture(qa_lmctf_flags *flags, qa_lmctf_flags_checkpoint *out);
void qa_lmctf_flags_restore(qa_lmctf_flags *flags, qa_q2_game_services *game, qa_lmctf_flags_checkpoint saved);
qa_q2_entity *qa_lmctf_flags_flag(qa_lmctf_flags *flags, qa_lmctf_playing_team team, qa_q2_game_services *game);
qa_q2_entity *qa_lmctf_flags_carried(qa_lmctf_flags *flags, qa_actor_id actor, qa_q2_game_services *game);
bool qa_lmctf_flags_at_home(qa_lmctf_flags *flags, qa_q2_entity *flag, qa_q2_game_services *game);
const char *qa_lmctf_flags_state(qa_lmctf_flags *flags, qa_lmctf_playing_team team, qa_q2_game_services *game);
void qa_lmctf_flags_reset(qa_lmctf_flags *flags, qa_q2_entity *flag, const qa_actor_id *player, qa_q2_game_services *game);
void qa_lmctf_flags_reset_all(qa_lmctf_flags *flags, qa_q2_game_services *game);
bool qa_lmctf_flags_spawn(qa_lmctf_flags *flags, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_lmctf_flags_post_spawn(qa_lmctf_flags *flags, qa_q2_game_services *game);
void qa_lmctf_flags_drop(qa_lmctf_flags *flags, qa_q2_entity *player, qa_q2_game_services *game);
bool qa_lmctf_flags_pickup(qa_lmctf_flags *flags, qa_q2_entity *flag, qa_q2_game_services *game, qa_actor_id actor);
void qa_lmctf_flags_hurt_carrier(qa_lmctf_flags *flags, qa_actor_id target, const qa_actor_id *attacker, qa_q2_game_services *game);
void qa_lmctf_flags_frag(qa_lmctf_flags *flags, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game);
#endif
