/* Port of src/content/q2/base/monsters/medic.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_MEDIC_H
#define QA_CONTENT_Q2_BASE_MONSTERS_MEDIC_H
#include "qa/content/q2/foundation/monsters/index.h"

/* TS createMedicDefinition(monsters) closes over monsters. */
typedef struct qa_q2bm_medic_definition {
  qa_q2m_definition definition;
  qa_q2m_named_handler callbacks[11];
} qa_q2bm_medic_definition;
void qa_q2bm_create_medic_definition(qa_q2bm_medic_definition *out, qa_q2m_monsters *monsters);
#endif
