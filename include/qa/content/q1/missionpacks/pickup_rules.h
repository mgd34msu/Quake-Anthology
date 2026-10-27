/* Port of src/content/q1/missionpacks/pickup-rules.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PICKUP_RULES_H
#define QA_CONTENT_Q1_MISSIONPACKS_PICKUP_RULES_H
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/player.h"


void qa_q1mp_register_mission_pack_pickup_rules(qa_q1mp_game *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_players *players);
/* TS rogueRank (file-local in TS; exposed for the ported test). */
double qa_q1mp_rogue_weapon_rank(const char *weapon);

#endif
