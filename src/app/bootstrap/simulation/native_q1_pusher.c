/* Port of src/app/bootstrap/simulation/native-q1-pusher.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q1_pusher.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_native_q1_pusher { qa_app_sim_native_q1_pusher_options options; };
qa_app_sim_native_q1_pusher *qa_app_sim_native_q1_pusher_create(qa_app_sim_native_q1_pusher_options options) {
  qa_app_sim_native_q1_pusher *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_native_q1_pusher_destroy(qa_app_sim_native_q1_pusher *value) { free(value); }

