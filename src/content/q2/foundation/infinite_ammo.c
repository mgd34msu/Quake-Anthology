/* Port of src/content/q2/foundation/infinite-ammo.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/infinite_ammo.h"
#include <string.h>

bool qa_q2_no_infinite_ammo(const qa_q2_game_options *options, qa_item_id ammo) {
  return options->edition == QA_Q2_EDITION_RERELEASE && ammo != NULL &&
         (strcmp(ammo, "q2:ammo_trap") == 0 || strcmp(ammo, "q2:ammo_tesla") == 0);
}
