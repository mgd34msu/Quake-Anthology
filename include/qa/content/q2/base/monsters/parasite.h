/* Port of src/content/q2/base/monsters/parasite.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_PARASITE_H
#define QA_CONTENT_Q2_BASE_MONSTERS_PARASITE_H
#include "qa/content/q2/foundation/monsters/types.h"
bool qa_q2bm_parasite_drain_reachable(qa_vec3 start, qa_vec3 end);
extern const qa_q2m_definition qa_q2bm_parasite_definition;
#endif
