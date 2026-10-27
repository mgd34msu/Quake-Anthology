/* Port of src/content/q1/missionpacks/world/hipnotic-misc.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_MISC_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_HIPNOTIC_MISC_H
#include "qa/content/q1/missionpacks/types.h"
qa_q1mp_actor *qa_q1mp_multi_explosion(qa_q1mp_game *game, qa_q1mp_actor *source, qa_vec3 origin, double radius, double damage, double duration, double pause, double volume);
void qa_q1mp_earthquake_after_physics(qa_q1mp_game *game, qa_actor_id actor);
void qa_q1mp_register_hipnotic_misc(qa_q1mp_game *game);
#endif
