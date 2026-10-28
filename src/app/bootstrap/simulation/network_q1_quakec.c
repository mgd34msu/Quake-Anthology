/* Port of src/app/bootstrap/simulation/network-q1-quakec.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q1_quakec.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_network_q1_quakec { qa_app_sim_network_q1_quakec_options options; };
qa_app_sim_network_q1_quakec *qa_app_sim_network_q1_quakec_create(qa_app_sim_network_q1_quakec_options options) {
  qa_app_sim_network_q1_quakec *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_network_q1_quakec_destroy(qa_app_sim_network_q1_quakec *value) { free(value); }

