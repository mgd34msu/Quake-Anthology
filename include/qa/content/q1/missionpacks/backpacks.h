/* Port of src/content/q1/missionpacks/backpacks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_BACKPACKS_H
#define QA_CONTENT_Q1_MISSIONPACKS_BACKPACKS_H
#include "qa/content/q1/missionpacks/player.h"
#include "qa/content/q1/missionpacks/types.h"

/* NULL = TS null. */
qa_q1_actor *qa_q1mp_drop_mission_pack_backpack(qa_q1_services *game, qa_owned_actor actor, qa_q1mp_pack pack);
qa_q1_actor *qa_q1mp_toss_rogue_backpack(qa_q1_services *game, qa_q1_player_state *player);
qa_q1_actor *qa_q1mp_toss_rogue_weapon(qa_q1_services *game, qa_q1_player_state *player);
/* players is borrowed for the life of the game. */
void qa_q1mp_register_rogue_toss_callbacks(qa_q1_services *game, qa_q1mp_mission_pack_players *players);
/* TS baseWeapon (file-local in TS; exposed for the ported test). */
qa_q1_weapon qa_q1mp_backpack_base_weapon(qa_q1_weapon weapon);

#endif
