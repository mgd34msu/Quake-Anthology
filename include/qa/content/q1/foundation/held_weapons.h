/* Port of src/content/q1/foundation/held-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_HELD_WEAPONS_H
#define QA_CONTENT_Q1_FOUNDATION_HELD_WEAPONS_H

#include "qa/contracts/held_weapon.h"

/* NULL = TS null. Returned model is static storage. */
const qa_held_weapon_model *qa_q1_held_weapon(const char *view_model);

#endif
