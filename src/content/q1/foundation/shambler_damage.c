/* Port of src/content/q1/foundation/shambler-damage.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/shambler_damage.h"

#include <string.h>

/* Q1's rocket impact and radius routines already halve their own shambler damage. */
bool qa_q1_foreign_shambler_damage(qa_q1_monster_species species, const qa_damage_request *request,
                                   double *out_amount, double *out_knockback) {
  const qa_attack_cause cause = request->attack.cause;
  if (species == NULL || strcmp(species, "shambler") != 0 ||
      cause.kind == QA_ATTACK_CAUSE_Q1 || cause.kind == QA_ATTACK_CAUSE_ENVIRONMENT)
    return false;
  const bool rocket = cause.kind == QA_ATTACK_CAUSE_Q2 ? cause.means_of_death == 8
                                                       : cause.means_of_death == 6;
  if ((request->delivery == NULL || strcmp(request->delivery, "radius") != 0) && !rocket)
    return false;
  *out_amount = (float)(request->amount * 0.5);
  *out_knockback = (float)(request->knockback * 0.5);
  return true;
}
