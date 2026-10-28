/* Port of src/app/bootstrap/simulation/native-mod-presentation.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_presentation.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_native_mod_presentation { qa_app_sim_native_mod_presentation_options options; };
qa_app_sim_native_mod_presentation *qa_app_sim_native_mod_presentation_create(qa_app_sim_native_mod_presentation_options options) {
  qa_app_sim_native_mod_presentation *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_native_mod_presentation_destroy(qa_app_sim_native_mod_presentation *value) { free(value); }

