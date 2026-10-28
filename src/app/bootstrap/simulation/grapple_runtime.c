/* Port of src/app/bootstrap/simulation/grapple-runtime.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_grapple_runtime { qa_app_sim_grapple_runtime_options options; };
qa_app_sim_grapple_runtime *qa_app_sim_grapple_runtime_create(qa_app_sim_grapple_runtime_options options) {
  qa_app_sim_grapple_runtime *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_grapple_runtime_destroy(qa_app_sim_grapple_runtime *value) { free(value); }

