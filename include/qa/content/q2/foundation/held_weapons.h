/* Port of src/content/q2/foundation/held-weapons.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_HELD_WEAPONS_H
#define QA_CONTENT_Q2_FOUNDATION_HELD_WEAPONS_H
#include <stdbool.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/held_weapon.h"
/* item NULL = TS undefined. out->path is caller scratch buffer path_buf. */
bool qa_q2_held_weapon(const char *view_model, qa_item_id item, qa_held_weapon_model *out, char *path_buf, size_t path_buf_len);
#endif
