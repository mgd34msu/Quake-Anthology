/* Port of src/content/q1/missionpacks/world/rotate-targets.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROTATE_TARGETS_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROTATE_TARGETS_H
#include "qa/content/q1/missionpacks/types.h"
qa_vec3 qa_q1mp_normalize_angles(qa_vec3 angles);
void qa_q1mp_link_rotate_targets(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_rotate_targets(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_rotate_targets_final(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_set_target_origin(qa_q1mp_game *game, qa_q1mp_actor *entity);
void qa_q1mp_damage_on_targets(qa_q1mp_game *game, qa_q1mp_actor *entity, double damage);
#endif
