/* Port of src/app/bootstrap/simulation/native-mod.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_native_mod_preparation { qa_app_sim_native_mod_preparation_options options; };
qa_app_sim_native_mod_preparation *qa_app_sim_native_mod_preparation_create(qa_app_sim_native_mod_preparation_options options) {
  qa_app_sim_native_mod_preparation *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_native_mod_preparation_destroy(qa_app_sim_native_mod_preparation *value) { free(value); }

