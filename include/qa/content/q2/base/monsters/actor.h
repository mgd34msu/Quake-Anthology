/* Port of src/content/q2/base/monsters/actor.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_ACTOR_H
#define QA_CONTENT_Q2_BASE_MONSTERS_ACTOR_H
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/index.h"

const char *qa_q2bm_actor_name(qa_q2f_entity *entity, qa_q2f_game *game);
extern const qa_q2m_definition qa_q2bm_actor_definition;

/* TS createActorTargetModule(monsters) closes over monsters. Creating it
 * also records the monsters used to resolve "actor_use" in the actor
 * definition's initialize (the qa_q2f source-callback mirror carries no
 * use resolver; TS owner: foundation/callbacks.ts). */
typedef struct qa_q2bm_actor_target_module {
  qa_q2m_monsters *monsters;
  qa_q2f_named_use use_defs[1];
  qa_q2f_named_touch touch_defs[1];
} qa_q2bm_actor_target_module;
void qa_q2bm_create_actor_target_module(qa_q2bm_actor_target_module *out, qa_q2m_monsters *monsters);
qa_q2bm_spawn_module qa_q2bm_actor_target_as_spawn(qa_q2bm_actor_target_module *module);
#endif
