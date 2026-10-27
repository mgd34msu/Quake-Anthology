/* Port of src/content/q2/rerelease/monsters/boss.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BOSS_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BOSS_H
#include "qa/content/q2/foundation/monsters/types.h"

qa_vec3 qa_q2rm_random_body_point(qa_q2f_entity *entity, qa_q2f_game *game);
/* TS bossExplodeThink (Q2Think). */
void qa_q2rm_boss_explode_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
qa_q2f_think qa_q2rm_boss_explode_think(void);
void qa_q2rm_boss_explode(qa_q2m_context *context);
/* qa_q2m_handler form of bossExplode. */
void qa_q2rm_boss_explode_handler(void *ctx, qa_q2m_context *context);
#endif
