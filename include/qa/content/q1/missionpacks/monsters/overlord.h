/* Port of src/content/q1/missionpacks/monsters/overlord.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_OVERLORD_H
#define QA_CONTENT_Q1_MISSIONPACKS_MONSTERS_OVERLORD_H

#include "qa/content/q1/missionpacks/monsters/runtime.h"

bool qa_q1mpm_is_spawn_point_empty(qa_q1mpm_game *game, qa_q1mpm_actor *point);
qa_q1mpm_actor *qa_q1mpm_overlord_destination(qa_q1mpm_game *game);
void qa_q1mpm_register_overlord_destination(qa_q1mpm_game *game);
extern const qa_q1mpm_pack_monster_definition qa_q1mpm_overlord_definition;

#endif
