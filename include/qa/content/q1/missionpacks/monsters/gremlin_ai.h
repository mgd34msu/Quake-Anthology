/* Port of src/content/q1/missionpacks/monsters/gremlin_ai.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_GREMLIN_AI_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_GREMLIN_AI_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

qa_q1mpm_opt_actor qa_q1mpm_gremlin_find_victim(qa_q1mpm_mission_monster *monster);
bool qa_q1mpm_gremlin_find_target(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_walk(qa_q1mpm_mission_monster *monster, double distance);
void qa_q1mpm_gremlin_stand(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_gremlin_run(qa_q1mpm_mission_monster *monster, double distance);

#endif
