/* Port of src/content/q1/missionpacks/world/common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_COMMON_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_COMMON_H
#include "qa/content/q1/missionpacks/types.h"
void qa_q1mp_world_number(qa_q1mp_actor *entity, const char *key, double value);
void qa_q1mp_world_vector(qa_q1mp_actor *entity, const char *key, qa_vec3 value);
void qa_q1mp_world_later(qa_q1mp_game *game, qa_q1mp_actor *entity, double delay, const char *name);
void qa_q1mp_world_trigger(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_world_brush(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_world_target_event(qa_q1mp_game *game, qa_q1mp_actor *entity, const char *target, const char *message);
#endif
