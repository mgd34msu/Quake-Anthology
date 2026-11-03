/* Port of src/content/q2/rerelease/monsters/base-variants/proboscis.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_PROBOSCIS_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_PROBOSCIS_H
#include "qa/content/q2/foundation/monsters/index.h"
/* TS createProboscis(monsters) result. Storage is caller-owned. */
typedef struct qa_q2rm_proboscis {
  qa_q2m_monsters *monsters;
  qa_q2_think reset, think, draw;
  qa_q2_touch touch;
  qa_q2_die die;
  qa_q2_named_think think_defs[3];
  qa_q2_named_touch touch_defs[1];
  qa_q2_named_die die_defs[1];
} qa_q2rm_proboscis;
void qa_q2rm_create_proboscis(qa_q2rm_proboscis *out, qa_q2m_monsters *monsters);
qa_q2_callback_definitions qa_q2rm_proboscis_callbacks(qa_q2rm_proboscis *proboscis);
/* TS retract(tip, game) / fire(context). */
void qa_q2rm_proboscis_retract(qa_q2rm_proboscis *proboscis, qa_q2_entity *tip, qa_q2_game_services *game);
void qa_q2rm_proboscis_fire(qa_q2rm_proboscis *proboscis, qa_q2m_context *context);
#endif
