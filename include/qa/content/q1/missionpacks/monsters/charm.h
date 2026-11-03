/* Port of src/content/q1/missionpacks/monsters/charm.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_CHARM_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_CHARM_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

/* TS charmer(monster): false = TS null. */
bool qa_q1mpm_charmer(qa_q1mpm_mission_monster *monster, qa_actor_id *out);
void qa_q1mpm_hunt_charmer(qa_q1mpm_mission_monster *monster, bool flee);
/* TS boolean | null: *out_null set when TS returns null. */
bool qa_q1mpm_find_charmed_target(qa_q1mpm_mission_monster *monster, bool *out_null);
bool qa_q1mpm_find_hipnotic_target(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_walk_with_charmer(qa_q1mpm_mission_monster *monster, double distance);

#endif
