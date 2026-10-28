/* Port of src/app/bootstrap/simulation/foreign-q2-world.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/foreign_q2_world.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_foreign_q2_world_modules { qa_app_sim_foreign_q2_world_modules_options options; };
qa_app_sim_foreign_q2_world_modules *qa_app_sim_foreign_q2_world_modules_create(qa_app_sim_foreign_q2_world_modules_options options) {
  qa_app_sim_foreign_q2_world_modules *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_foreign_q2_world_modules_destroy(qa_app_sim_foreign_q2_world_modules *value) { free(value); }

