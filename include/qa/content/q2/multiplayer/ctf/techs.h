/* Port of src/content/q2/multiplayer/ctf/techs.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_TECHS_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_TECHS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef struct qa_q2_ctf_techs qa_q2_ctf_techs;
qa_q2_ctf_techs *qa_q2_ctf_techs_create(qa_q2_ctf_context *context);
void qa_q2_ctf_techs_destroy(qa_q2_ctf_techs *techs);
qa_q2_callback_definitions qa_q2_ctf_techs_callbacks(qa_q2_ctf_techs *techs);
void qa_q2_ctf_techs_register(qa_q2_ctf_techs *techs);
bool qa_q2_ctf_techs_spawn(qa_q2_ctf_techs *techs, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_techs_setup(qa_q2_ctf_techs *techs, qa_q2_game_services *game);
void qa_q2_ctf_techs_reset(qa_q2_ctf_techs *techs, qa_q2_game_services *game);
void qa_q2_ctf_techs_respawn(qa_q2_ctf_techs *techs, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_techs_drop(qa_q2_ctf_techs *techs, qa_q2_entity *entity, qa_q2_game_services *game, bool dead);
double qa_q2_ctf_techs_strength(qa_q2_ctf_techs *techs, const qa_actor_id *actor, qa_q2_game_services *game, double damage);
double qa_q2_ctf_techs_resistance(qa_q2_ctf_techs *techs, qa_actor_id actor, qa_q2_game_services *game, double take);
bool qa_q2_ctf_techs_haste(qa_q2_ctf_techs *techs, qa_actor_id actor, qa_q2_game_services *game);
bool qa_q2_ctf_techs_strength_sound(qa_q2_ctf_techs *techs, qa_owned_actor entity, qa_q2_game_services *game);
void qa_q2_ctf_techs_haste_sound(qa_q2_ctf_techs *techs, qa_owned_actor entity, qa_q2_game_services *game);
bool qa_q2_ctf_techs_has_regeneration(qa_q2_ctf_techs *techs, qa_actor_id actor, qa_q2_game_services *game);
void qa_q2_ctf_techs_regenerate(qa_q2_ctf_techs *techs, qa_q2_entity *entity, qa_q2_game_services *game);
#endif
