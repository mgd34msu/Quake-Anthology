/* Port of src/content/q2/rerelease/monsters/base-variants/actor.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_ACTOR_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_ACTOR_H
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/index.h"
/* TS createRereleaseActorModule result. Storage is caller-owned. */
typedef struct qa_q2rm_actor_module {
  qa_q2m_monsters *monsters;
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[16];
  qa_q2f_named_use use_defs[1];
  qa_q2f_named_touch touch_defs[1];
} qa_q2rm_actor_module;
void qa_q2rm_create_rerelease_actor_module(qa_q2rm_actor_module *out, qa_q2m_monsters *monsters);
qa_q2bm_spawn_module qa_q2rm_actor_targets_as_spawn(qa_q2rm_actor_module *module);
#endif
