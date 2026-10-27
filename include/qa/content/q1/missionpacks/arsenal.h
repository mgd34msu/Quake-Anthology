/* Port of src/content/q1/missionpacks/arsenal.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_ARSENAL_H
#define QA_CONTENT_Q1_MISSIONPACKS_ARSENAL_H
#include "qa/content/q1/missionpacks/types.h"

#include "qa/content/q1/missionpacks/grapple.h"
#include "qa/content/q1/missionpacks/player.h"
/* TS MissionPackArsenal. grapple NULL for hipnotic. */
typedef struct qa_q1mp_mission_pack_arsenal {
  qa_q1mp_game *game; qa_q1mp_pack pack;
  qa_q1mp_mission_pack_players players;
  qa_q1mp_rogue_grapple grapple_storage; qa_q1mp_rogue_grapple *grapple;
  qa_q1mp_opt_actor charmer;
} qa_q1mp_mission_pack_arsenal;
void qa_q1mp_register_hipnotic_weapons(qa_q1mp_game *game);
void qa_q1mp_mission_pack_arsenal_init(qa_q1mp_mission_pack_arsenal *arsenal, qa_q1mp_game *game, qa_q1mp_pack pack);
qa_q1mp_mission_pack_arsenal *qa_q1mp_register_mission_pack_arsenal(qa_q1mp_game *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_arsenal *out);
bool qa_q1mp_arsenal_impulse(qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id actor, double impulse);
qa_q1mp_opt_actor qa_q1mp_arsenal_horn_charmer(qa_q1mp_mission_pack_arsenal *arsenal);

#endif
