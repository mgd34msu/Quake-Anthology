/* Port of src/app/bootstrap/simulation/events.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/events.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_simulation_events { qa_app_sim_simulation_events_options options; };
qa_app_sim_simulation_events *qa_app_sim_simulation_events_create(qa_app_sim_simulation_events_options options) {
  qa_app_sim_simulation_events *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_simulation_events_destroy(qa_app_sim_simulation_events *value) { free(value); }

