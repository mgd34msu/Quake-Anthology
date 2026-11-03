/* Port of src/content/composition/q1/q3-pickups.ts. Q3-authored items on a Q1
 * primary are an arsenal concern, so the Q1 foundation reaches them through
 * this hook. foreignQ3Pickup itself is qa_q1_foreign_q3_pickup in
 * src/content/q1/foundation/pickups.c, whose private pickup definition and take
 * dispatch carry its grant kinds. GPL-2.0-or-later. */
#include "qa/content/composition/q1/q3_pickups.h"

#include "qa/content/q1/foundation/pickups.h"

void qa_comp_q1_register_q3_pickup_hook(qa_q1_services *game) {
  game->foreign_pickup.ctx = NULL;
  game->foreign_pickup.fn = qa_q1_foreign_q3_pickup;
}
