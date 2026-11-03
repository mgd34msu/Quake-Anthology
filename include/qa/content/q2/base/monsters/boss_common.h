/* Port of src/content/q2/base/monsters/boss-common.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_BOSS_COMMON_H
#define QA_CONTENT_Q2_BASE_MONSTERS_BOSS_COMMON_H
#include "qa/content/q2/foundation/monsters/index.h"

void qa_q2bm_stop_loop(qa_q2m_context *context);
bool qa_q2bm_boss_check_attack(qa_q2m_context *context, bool hover, bool allow_non_solid);
bool qa_q2bm_boss_check_attack_default(qa_q2m_context *context);
/* TS WeakMap-cached think per monsters: ctx is the monsters pointer. */
qa_q2_think qa_q2bm_boss_explosion_think(qa_q2m_monsters *monsters);
void qa_q2bm_boss_explosion_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
/* Copies *definition to *out with "q2:BossExplode" appended to its think
 * source callbacks; think_storage needs room for think_count + 1. */
void qa_q2bm_with_boss_explosion_callbacks(qa_q2m_definition *out, const qa_q2m_definition *definition,
                                           qa_q2m_monsters *monsters, qa_q2_named_think *think_storage);
void qa_q2bm_boss_explode(qa_q2m_context *context);
void qa_q2bm_boss_explode_handler(void *ctx, qa_q2m_context *context);
#endif
