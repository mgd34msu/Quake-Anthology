/* Port of src/content/q1/missionpacks/rogue-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_ROGUE_WEAPONS_H
#define QA_CONTENT_Q1_MISSIONPACKS_ROGUE_WEAPONS_H
#include "qa/content/q1/missionpacks/types.h"

qa_q1_actor *qa_q1mp_launch_rogue_lava_spike(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction, bool powered);
bool qa_q1mp_fire_rogue_lava(qa_q1_services *game, qa_q1_player_state *player);
qa_q1_actor *qa_q1mp_launch_rogue_multi_grenade(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles);
bool qa_q1mp_fire_rogue_multi_grenade(qa_q1_services *game, qa_q1_player_state *player);
bool qa_q1mp_fire_rogue_multi_rocket(qa_q1_services *game, qa_q1_player_state *player);
qa_q1_actor *qa_q1mp_launch_rogue_plasma(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction);
bool qa_q1mp_fire_rogue_plasma(qa_q1_services *game, qa_q1_player_state *player);
void qa_q1mp_register_rogue_weapon_callbacks(qa_q1_services *game);

#endif
