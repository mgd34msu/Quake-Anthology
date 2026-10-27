/* Port of src/content/q2/foundation/weapons/definitions.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_DEFINITIONS_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_DEFINITIONS_H
#include "qa/content/q2/foundation/weapons/types.h"
extern const qa_q2w_base_definition qa_q2w_base_weapons[];
extern const size_t qa_q2w_base_weapon_count;
/* TS q2WeaponDefinition: name must be a base name (unchecked in TS;
 * unknown aborts here rather than yielding undefined). */
const qa_q2w_base_definition *qa_q2w_weapon_definition(qa_q2w_name name);
const qa_q2w_base_definition *qa_q2w_weapon_from_classname(const char *classname); /* NULL = null */
#endif
