/* Port of src/content/q1/missionpacks/monsters/decoy.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_DECOY_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_DECOY_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

extern const qa_q1mpm_pack_monster_definition qa_q1mpm_decoy_definition;
/* TS becomeDecoy: false only on OOM; *out receives the created entity. */
bool qa_q1mpm_become_decoy(qa_q1mpm_runtime *runtime, const char *target, qa_vec3 origin,
                           qa_q1mpm_actor **out);

#endif
