/* Port of src/app/bootstrap/simulation/bot-q1-knowledge.ts. Wiring over the opaque
 * SharedSimulation (runtime slice); collaborators stay fn+ctx/opaque
 * until runtime lands. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_q1_knowledge.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_q1_bot_knowledge { qa_app_sim_q1_bot_knowledge_options options; };
qa_app_sim_q1_bot_knowledge *qa_app_sim_q1_bot_knowledge_create(qa_app_sim_q1_bot_knowledge_options options) {
  qa_app_sim_q1_bot_knowledge *v = calloc(1, sizeof *v); if (v == NULL) qa_range_error("Out of memory");
  v->options = options; return v;
}
void qa_app_sim_q1_bot_knowledge_destroy(qa_app_sim_q1_bot_knowledge *value) { free(value); }

