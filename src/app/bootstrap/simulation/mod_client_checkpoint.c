/* Port of src/app/bootstrap/simulation/mod-client-checkpoint.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/mod_client_checkpoint.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_mod_client_checkpoint { qa_app_sim_mod_client_checkpoint_options options; };
qa_app_sim_mod_client_checkpoint *qa_app_sim_mod_client_checkpoint_create(qa_app_sim_mod_client_checkpoint_options options) {
  qa_app_sim_mod_client_checkpoint *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_mod_client_checkpoint_destroy(qa_app_sim_mod_client_checkpoint *value) { free(value); }

