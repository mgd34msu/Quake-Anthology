/* Port of src/app/bootstrap/simulation/native-mod-host.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_native_mod_host { qa_app_sim_native_mod_host_options options; };
qa_app_sim_native_mod_host *qa_app_sim_native_mod_host_create(qa_app_sim_native_mod_host_options options) {
  qa_app_sim_native_mod_host *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_native_mod_host_destroy(qa_app_sim_native_mod_host *value) { free(value); }

