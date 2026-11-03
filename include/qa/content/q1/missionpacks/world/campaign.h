/* Port of src/content/q1/missionpacks/world/campaign.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_CAMPAIGN_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_CAMPAIGN_H
#include "qa/content/q1/missionpacks/types.h"
typedef struct qa_q1mp_world_hooks qa_q1mp_world_hooks;
void qa_q1mp_start_finale_timer(qa_q1_services *game);
void qa_q1mp_register_mission_campaign(qa_q1_services *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks);
#endif
