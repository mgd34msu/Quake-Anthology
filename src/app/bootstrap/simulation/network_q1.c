/* Port of src/app/bootstrap/simulation/network-q1.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q1.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_network_q1_binding { qa_app_sim_network_q1_binding_options options; };
qa_app_sim_network_q1_binding *qa_app_sim_network_q1_binding_create(qa_app_sim_network_q1_binding_options options) {
  qa_app_sim_network_q1_binding *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_network_q1_binding_destroy(qa_app_sim_network_q1_binding *value) { free(value); }

