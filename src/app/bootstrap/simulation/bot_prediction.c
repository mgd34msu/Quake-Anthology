/* Port of src/app/bootstrap/simulation/bot-prediction.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque
 * until runtime lands. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_prediction.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_bot_prediction { qa_app_sim_bot_prediction_options options; };
qa_app_sim_bot_prediction *qa_app_sim_bot_prediction_create(qa_app_sim_bot_prediction_options options) {
  qa_app_sim_bot_prediction *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_bot_prediction_destroy(qa_app_sim_bot_prediction *value) { free(value); }

