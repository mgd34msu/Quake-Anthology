/* Port of src/content/q2/rerelease/monsters/base-variants/jorg.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_JORG_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_JORG_H
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/base/monsters/jorg.h"
/* TS transferHealthbarTarget optional argument (fn NULL = absent). */
typedef struct qa_q2rm_healthbar_transfer {
  void *ctx;
  void (*fn)(void *ctx, qa_actor_id old_actor, qa_actor_id new_actor, qa_q2_game_services *game);
} qa_q2rm_healthbar_transfer;
/* TS createRereleaseJorgDefinition. Storage is caller-owned; base
 * holds the embedded createJorgDefinition(monsters). */
typedef struct qa_q2rm_jorg_definition {
  qa_q2bm_jorg_definition base;
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[24];
  qa_q2_named_think think_defs[1];
} qa_q2rm_jorg_definition;
void qa_q2rm_create_rerelease_jorg_definition(qa_q2mpm_arena *arena, qa_q2rm_jorg_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2rm_healthbar_transfer transfer);
#endif
