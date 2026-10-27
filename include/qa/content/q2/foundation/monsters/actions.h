/* Port of src/content/q2/foundation/monsters/actions.ts (MonsterCallback
 * string union; here the exhaustive name table). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MONSTERS_ACTIONS_H
#define QA_CONTENT_Q2_FOUNDATION_MONSTERS_ACTIONS_H
#include <stdbool.h>
#include <stddef.h>
typedef const char *qa_q2m_callback_name;
extern const char *const qa_q2m_callback_names[];
extern const size_t qa_q2m_callback_name_count;
bool qa_q2m_is_callback_name(const char *name);
#endif
