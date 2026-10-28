/* Port of src/app/bootstrap/simulation/monster-navigation.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_monster_navigation { qa_app_sim_monster_navigation_options options; };
qa_app_sim_monster_navigation *qa_app_sim_monster_navigation_create(qa_app_sim_monster_navigation_options options) {
  qa_app_sim_monster_navigation *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_monster_navigation_destroy(qa_app_sim_monster_navigation *value) { free(value); }

