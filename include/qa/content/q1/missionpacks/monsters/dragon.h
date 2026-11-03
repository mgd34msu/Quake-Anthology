/* Port of src/content/q1/missionpacks/monsters/dragon.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_DRAGON_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_DRAGON_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

qa_q1_actor *qa_q1mpm_launch_dragon_fireball(qa_q1_services *game, qa_actor_id owner,
                                             qa_vec3 origin, qa_vec3 direction);
/* TS dragonDefinition(runtime): fills *out. */
void qa_q1mpm_dragon_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out);
void qa_q1mpm_register_dragon_corners(qa_q1_services *game);

#endif
