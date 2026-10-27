/* Port of src/content/q2/foundation/monsters/ai.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_AI_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_AI_H
#include "qa/content/q2/foundation/monsters/types.h"

#define QA_Q2M_MASK_MONSTERSOLID ((double)(1 | 2 | 0x20000 | 0x2000000))
#define QA_Q2M_MASK_SHOT ((double)(1 | 2 | 8 | 16 | 0x2000000))
#define QA_Q2M_MASK_OPAQUE ((double)(1 | 8 | 16))
#define QA_Q2M_MASK_WATER ((double)(8 | 16 | 32))
#define QA_Q2M_FL_NOTARGET 32.0

double qa_q2m_monster_solid_mask(qa_q2f_game *game);
double qa_q2m_attack_trace_mask(qa_q2f_game *game);
double qa_q2m_health(qa_q2f_game *game, const qa_actor_id *actor);
bool qa_q2m_enemy_body(qa_q2m_context *context, qa_body_state *out);
bool qa_q2m_enemy_eye(qa_q2m_context *context, qa_vec3 *out);
double qa_q2m_target_distance(qa_q2m_context *context);
/* visible(context, actor = entity.enemy): actor NULL means default
 * enemy is intended via qa_q2m_visible_enemy; pass explicit here. */
bool qa_q2m_visible(qa_q2m_context *context, const qa_actor_id *actor);
bool qa_q2m_visible_enemy(qa_q2m_context *context);
bool qa_q2m_in_front(qa_q2m_context *context, qa_actor_id actor);
qa_vec3 qa_q2m_project_flash(qa_q2m_context *context, qa_vec3 offset, qa_vec3 angles);
qa_vec3 qa_q2m_project_flash_default(qa_q2m_context *context, qa_vec3 offset);
bool qa_q2m_clear_shot(qa_q2m_context *context, qa_vec3 offset);
bool qa_q2m_trace_ground_actor(qa_trace_result trace, qa_q2f_game *game, qa_actor_id *out);
void qa_q2m_change_yaw(qa_q2m_context *context);
void qa_q2m_face_enemy(qa_q2m_context *context);
bool qa_q2m_check_bottom(qa_q2m_context *context, qa_vec3 origin);
bool qa_q2m_walk_move(qa_q2m_context *context, double yaw, double distance, bool commit, bool relink);
bool qa_q2m_step_direction(qa_q2m_context *context, double yaw, double distance);
bool qa_q2m_chase_direction(qa_q2m_context *context, qa_vec3 goal, double distance);
void qa_q2m_run_ai(qa_q2m_context *context, qa_q2m_ai_kind ai, double distance);
bool qa_q2m_prone_shot(qa_q2m_context *context);
void qa_q2m_set_duck(qa_q2m_context *context, bool down);
void qa_q2m_finish_dodge(qa_q2m_context *context);
void qa_q2m_corpse(qa_q2m_context *context);
void qa_q2m_monster_dead_think(qa_q2m_context *context);
void qa_q2m_flies_on(qa_q2m_context *context);
void qa_q2m_flies_off(qa_q2m_context *context);
void qa_q2m_fly_check(qa_q2m_context *context);
qa_vec3 qa_q2m_body_of(qa_q2f_game *game, qa_q2f_entity *entity);
#endif
