/* Port of src/content/q1/foundation/shambler-damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_SHAMBLER_DAMAGE_H
#define QA_CONTENT_Q1_FOUNDATION_SHAMBLER_DAMAGE_H

#include <stdbool.h>

#include "qa/content/q1/foundation/entity.h"
#include "qa/contracts/gameplay.h"

/* False = TS null. species NULL = TS undefined. */
bool qa_q1_foreign_shambler_damage(qa_q1_monster_species species, const qa_damage_request *request,
                                   double *out_amount, double *out_knockback);

#endif
