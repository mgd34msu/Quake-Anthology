/* Port of src/content/q2/rerelease/monsters/base-variants/mutant.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_MUTANT_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_MUTANT_H
#include "qa/content/q2/base/monsters/mutant.h"
/* TS createRereleaseMutantDefinition. Storage for the definition,
 * merged callbacks, ai table and source touch is caller-owned;
 * base holds the embedded createMutantDefinition(monsters). */
typedef struct qa_q2rm_mutant_definition {
  qa_q2bm_mutant_definition base;
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[24];
  qa_q2m_named_ai ai[2];
  qa_q2f_named_touch touch_defs[1];
} qa_q2rm_mutant_definition;
void qa_q2rm_create_rerelease_mutant_definition(qa_q2rm_mutant_definition *out, qa_q2m_monsters *monsters);
#endif
