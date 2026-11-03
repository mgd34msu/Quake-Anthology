/* Port of src/content/q2/foundation/rerelease-ammo.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/rerelease_ammo.h"
#include <stddef.h>
#include <string.h>

static const qa_q2_ammo_limits LIMITS[] = {
  {"ammo_bullets", 0, 200, true, 250, true, 300},
  {"ammo_shells", 1, 100, true, 150, true, 200},
  {"ammo_rockets", 2, 50, false, 0, true, 100},
  {"ammo_grenades", 3, 50, false, 0, true, 100},
  {"ammo_cells", 4, 200, true, 250, true, 300},
  {"ammo_slugs", 5, 50, true, 75, true, 100},
  {"ammo_magslug", 6, 50, true, 75, true, 100},
  {"ammo_trap", 7, 5, false, 0, false, 0},
  {"ammo_flechettes", 8, 200, true, 250, true, 300},
  {"ammo_tesla", 9, 5, false, 0, false, 0},
  {"ammo_disruptor", 10, 12, true, 21, true, 30},
  {"ammo_prox", 11, 50, false, 0, false, 0},
};
const qa_q2_ammo_limits *qa_q2_rerelease_ammo_limits(const char *classname) {
  if (classname == NULL) return NULL;
  for (size_t i = 0; i < sizeof LIMITS / sizeof *LIMITS; i++) if (strcmp(LIMITS[i].classname, classname) == 0) return &LIMITS[i];
  return NULL;
}
