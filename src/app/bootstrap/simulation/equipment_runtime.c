/* Port of src/app/bootstrap/simulation/equipment-runtime.ts: checkpoint
 * shape + runtime holder; Q2 hand-grenade stepping rides the landed
 * equipment controller at the runtime call site. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
void qa_app_sim_hand_grenade_runtime_checkpoint_destroy(qa_app_sim_hand_grenade_runtime_checkpoint *c) {
  free(c->controls); c->controls = NULL; c->control_count = 0;
}
struct qa_app_sim_hand_grenade_runtime { const void *host; };
qa_app_sim_hand_grenade_runtime *qa_app_sim_hand_grenade_runtime_create(const void *host) {
  qa_app_sim_hand_grenade_runtime *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("Out of memory");
  r->host = host; return r;
}
void qa_app_sim_hand_grenade_runtime_destroy(qa_app_sim_hand_grenade_runtime *r) { free(r); }
