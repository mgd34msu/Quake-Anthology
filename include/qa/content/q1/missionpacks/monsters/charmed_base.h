/* Port of src/content/q1/missionpacks/monsters/charmed_base.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_CHARMED_BASE_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_CHARMED_BASE_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

extern const qa_q1mpm_pack_monster_definition qa_q1mpm_hipnotic_army_definition;
/* TS hipnoticDogDefinition(runtime): fills *out (callbacks capture runtime). */
void qa_q1mpm_hipnotic_dog_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out);

#endif
