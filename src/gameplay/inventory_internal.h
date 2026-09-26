/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_INVENTORY_INTERNAL_H
#define QA_INVENTORY_INTERNAL_H
#include "qa/inventory.h"

/* Storage identity includes the primary binding, even when no component owns
 * the item. Pickup leases use it to reject storage replaced during callbacks. */
bool qa_inventory_storage_token(qa_inventory *, qa_actor_id, qa_item_id,
                                uint64_t *, qa_error *);
bool qa_inventory_pickup_claim(qa_inventory *, qa_actor_id, qa_item_id, const void *, qa_error *);
bool qa_inventory_pickup_current(qa_inventory *, qa_actor_id, qa_item_id, const void *);
void qa_inventory_pickup_release(qa_inventory *, qa_actor_id, const void *);
void qa_inventory_hold(qa_inventory *);
void qa_inventory_unhold(qa_inventory *);
#endif
