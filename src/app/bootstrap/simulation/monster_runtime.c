/* Port of src/app/bootstrap/simulation/monster-runtime.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_selected_monsters { qa_app_sim_selected_monsters_options options; };
qa_app_sim_selected_monsters *qa_app_sim_selected_monsters_create(qa_app_sim_selected_monsters_options options) {
  qa_app_sim_selected_monsters *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_selected_monsters_destroy(qa_app_sim_selected_monsters *value) { free(value); }

