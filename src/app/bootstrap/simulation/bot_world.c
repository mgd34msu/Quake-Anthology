/* Port of src/app/bootstrap/simulation/bot-world.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque
 * until runtime lands. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_world.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_shared_bot_world { qa_app_sim_shared_bot_world_options options; };
qa_app_sim_shared_bot_world *qa_app_sim_shared_bot_world_create(qa_app_sim_shared_bot_world_options options) {
  qa_app_sim_shared_bot_world *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_shared_bot_world_destroy(qa_app_sim_shared_bot_world *value) { free(value); }

