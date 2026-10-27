/* Port of src/content/q2/base/monsters/jorg.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_JORG_H
#define QA_CONTENT_Q2_BASE_MONSTERS_JORG_H
#include "qa/content/q2/foundation/monsters/index.h"

/* TS createJorgDefinition(monsters) closes over monsters (MakronToss);
 * the callback table carries it as handler ctx. */
typedef struct qa_q2bm_jorg_definition {
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[12];
} qa_q2bm_jorg_definition;
void qa_q2bm_create_jorg_definition(qa_q2bm_jorg_definition *out, qa_q2m_monsters *monsters);
#endif
