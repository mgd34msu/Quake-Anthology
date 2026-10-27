/* Port of src/content/monsters/target.ts. GPL-2.0-or-later. */
#include "qa/content/monsters/target.h"

#include <stddef.h>

bool qa_monster_target_eligible(double health,
                                const qa_monster_target_observation *observation) {
  return health > 0 && observation != NULL && !observation->notarget;
}
