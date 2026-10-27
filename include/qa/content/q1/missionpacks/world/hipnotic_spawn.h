/* Port of src/content/q1/missionpacks/world/hipnotic-spawn.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_SPAWN_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_SPAWN_H
#include "qa/content/q1/missionpacks/types.h"
struct qa_q1mp_world_hooks;
void qa_q1mp_register_hipnotic_spawn(qa_q1mp_game *game, const struct qa_q1mp_world_hooks *hooks);
#endif
