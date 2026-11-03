/* Port of src/content/q1/missionpacks/world/hipnotic-hazards.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_HAZARDS_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_HAZARDS_H
#include "qa/content/q1/missionpacks/types.h"
bool qa_q1mp_is_struck_by_mjolnir(qa_q1_services *game, qa_actor_id actor);
void qa_q1mp_register_hipnotic_hazards(qa_q1_services *game);
#endif
