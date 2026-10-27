/* Port of src/content/q2/multiplayer/lmctf/runes.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_RUNES_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_RUNES_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
typedef struct qa_lmctf_runes qa_lmctf_runes;
qa_lmctf_runes *qa_lmctf_runes_create(qa_lmctf_context *context,
    qa_q2_entity *(*red_flag)(void *ctx, qa_q2_game_services *game), void *red_flag_ctx);
void qa_lmctf_runes_destroy(qa_lmctf_runes *runes);
qa_q2_callback_definitions qa_lmctf_runes_callbacks(qa_lmctf_runes *runes);
bool qa_lmctf_runes_capture(qa_lmctf_runes *runes); /* TS {forward} */
void qa_lmctf_runes_restore(qa_lmctf_runes *runes, bool forward);
const char *qa_lmctf_runes_held(qa_lmctf_runes *runes, qa_actor_id actor, qa_q2_game_services *game);
bool qa_lmctf_runes_spawn(qa_lmctf_runes *runes, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_lmctf_runes_post_spawn(qa_lmctf_runes *runes, qa_q2_game_services *game);
bool qa_lmctf_runes_pickup(qa_lmctf_runes *runes, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id actor);
bool qa_lmctf_runes_drop(qa_lmctf_runes *runes, qa_actor_id actor, qa_q2_game_services *game);
double qa_lmctf_runes_damage(qa_lmctf_runes *runes, const qa_actor_id *actor, double amount, qa_q2_game_services *game);
double qa_lmctf_runes_after_power_armor(qa_lmctf_runes *runes, qa_actor_id actor, double take, qa_q2_game_services *game);
void qa_lmctf_runes_after_health(qa_lmctf_runes *runes, qa_actor_id target, const qa_actor_id *attacker, double take, qa_q2_game_services *game);
void qa_lmctf_runes_player_frame(qa_lmctf_runes *runes, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_lmctf_runes_weapon_frame(qa_lmctf_runes *runes, qa_owned_actor entity, qa_q2_game_services *game, bool firing,
                                 void (*repeat)(void *ctx), void *repeat_ctx);
#endif
