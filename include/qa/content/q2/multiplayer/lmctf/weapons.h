/* Port of src/content/q2/multiplayer/lmctf/weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_WEAPONS_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_WEAPONS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/contracts/world.h"
extern const qa_q2f_weapon_definition QA_LMCTF_PLASMA;
typedef struct qa_lmctf_weapons qa_lmctf_weapons;
qa_lmctf_weapons *qa_lmctf_weapons_create(qa_lmctf_context *context);
void qa_lmctf_weapons_destroy(qa_lmctf_weapons *weapons);
qa_q2_callback_definitions qa_lmctf_weapons_callbacks(qa_lmctf_weapons *weapons);
void qa_lmctf_weapons_launch(qa_lmctf_weapons *weapons, qa_owned_actor owner, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, bool reflect);
#endif
