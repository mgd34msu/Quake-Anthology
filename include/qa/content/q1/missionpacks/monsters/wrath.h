/* Port of src/content/q1/missionpacks/monsters/wrath.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_WRATH_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_WRATH_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

void qa_q1mpm_wrath_missile(qa_q1mpm_mission_monster *monster, double attack);
extern const qa_q1mpm_named_handlers qa_q1mpm_wrath_callbacks[];
extern const size_t qa_q1mpm_wrath_callback_count;
extern const qa_q1mpm_pack_monster_definition qa_q1mpm_wrath_definition;

#endif
