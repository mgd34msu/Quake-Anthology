/* Port of src/content/q2/base/monsters/makron.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_MAKRON_H
#define QA_CONTENT_Q2_BASE_MONSTERS_MAKRON_H
#include "qa/content/q2/foundation/monsters/index.h"

extern const qa_q2m_definition qa_q2bm_makron_definition;
/* Copies *definition to *out with MakronSpawn/makron_torso_think think
 * source callbacks appended; think_storage needs room for think_count + 2. */
void qa_q2bm_with_makron_spawn_callbacks(qa_q2m_definition *out, const qa_q2m_definition *definition,
                                         qa_q2m_monsters *monsters, qa_q2f_named_think *think_storage);
void qa_q2bm_makron_toss(qa_q2m_context *context, qa_q2m_monsters *monsters);
#endif
