/* Port of src/content/q2/foundation/monsters/gibs.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_GIBS_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_GIBS_H
#include "qa/content/q2/foundation/monsters/types.h"
typedef struct qa_q2m_gib_options { bool head, metallic, skinned, upright; } qa_q2m_gib_options;
qa_q2f_callback_definitions qa_q2m_gib_callbacks(void);
qa_q2f_entity *qa_q2m_throw_gib(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage, qa_q2m_gib_options options);
qa_q2f_entity *qa_q2m_throw_head(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double damage);
#endif
