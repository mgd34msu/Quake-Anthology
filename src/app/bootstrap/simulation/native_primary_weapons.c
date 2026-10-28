/* Port of src/app/bootstrap/simulation/native-primary-weapons.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_primary_weapons.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_native_primary_weapons { qa_app_sim_native_primary_weapons_options options; };
qa_app_sim_native_primary_weapons *qa_app_sim_native_primary_weapons_create(qa_app_sim_native_primary_weapons_options options) {
  qa_app_sim_native_primary_weapons *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_native_primary_weapons_destroy(qa_app_sim_native_primary_weapons *value) { free(value); }

