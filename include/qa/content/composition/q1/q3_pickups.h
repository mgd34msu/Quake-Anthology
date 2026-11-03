/* Port of src/content/composition/q1/q3-pickups.ts: Q3 item pickups on a Q1
 * primary, reached by the Q1 foundation through its foreignPickup hook.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_COMPOSITION_Q1_Q3_PICKUPS_H
#define QA_CONTENT_COMPOSITION_Q1_Q3_PICKUPS_H

#include "qa/content/q1/foundation/entity_services.h"

/* TS registerQ3PickupHook(game): game.foreignPickup = foreignQ3Pickup. */
void qa_comp_q1_register_q3_pickup_hook(qa_q1_services *game);

#endif
