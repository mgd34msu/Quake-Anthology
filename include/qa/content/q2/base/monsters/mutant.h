/* Port of src/content/q2/base/monsters/mutant.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_MUTANT_H
#define QA_CONTENT_Q2_BASE_MONSTERS_MUTANT_H
#include "qa/content/q2/foundation/monsters/index.h"

/* TS createMutantDefinition(monsters, source) closes over monsters; the
 * jump touch is registered as "<source>_mutant_jump_touch". */
typedef struct qa_q2bm_mutant_definition {
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[12];
  qa_q2f_named_touch touch_defs[1];
} qa_q2bm_mutant_definition;
void qa_q2bm_create_mutant_definition(qa_q2bm_mutant_definition *out, qa_q2m_monsters *monsters);
/* source is "classic" or "rogue" (NULL = "classic", the TS default). */
void qa_q2bm_create_mutant_definition_source(qa_q2bm_mutant_definition *out, qa_q2m_monsters *monsters,
                                             const char *source);
#endif
