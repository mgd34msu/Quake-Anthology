/* Port of src/app/bootstrap/simulation/bots.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque
 * until runtime lands. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bots.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_application_bots { qa_app_sim_application_bots_options options; };
qa_app_sim_application_bots *qa_app_sim_application_bots_create(qa_app_sim_application_bots_options options) {
  qa_app_sim_application_bots *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_application_bots_destroy(qa_app_sim_application_bots *value) { free(value); }

