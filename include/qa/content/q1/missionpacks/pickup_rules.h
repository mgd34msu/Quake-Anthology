/* Port of src/content/q1/missionpacks/pickup-rules.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PICKUP_RULES_H
#define QA_CONTENT_Q1_MISSIONPACKS_PICKUP_RULES_H
#include "qa/content/q1/missionpacks/player.h"
#include "qa/content/q1/missionpacks/types.h"

/* Registers players->pickup_rules (players must not move; it is the rules' ctx). */
void qa_q1mp_register_mission_pack_pickup_rules(qa_q1_services *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_players *players);
/* TS rogueRank (file-local in TS; exposed for the ported test). */
double qa_q1mp_rogue_weapon_rank(qa_q1_weapon weapon);

#endif
