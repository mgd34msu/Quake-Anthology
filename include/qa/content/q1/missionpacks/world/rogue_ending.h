/* Port of src/content/q1/missionpacks/world/rogue-ending.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_ENDING_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_ENDING_H
#include "qa/content/q1/missionpacks/types.h"
typedef struct qa_q1mp_world_hooks qa_q1mp_world_hooks;
void qa_q1mp_start_rogue_ending(qa_q1_services *game, qa_actor_id player, const qa_q1mp_world_hooks *hooks);
void qa_q1mp_register_rogue_ending(qa_q1_services *game);
#endif
