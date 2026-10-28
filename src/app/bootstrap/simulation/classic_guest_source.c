/* Port of src/app/bootstrap/simulation/classic-guest-source.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_classic_guest_source { qa_app_sim_classic_guest_source_options options; };
qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_source_create(qa_app_sim_classic_guest_source_options options) {
  qa_app_sim_classic_guest_source *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_classic_guest_source_destroy(qa_app_sim_classic_guest_source *value) { free(value); }

