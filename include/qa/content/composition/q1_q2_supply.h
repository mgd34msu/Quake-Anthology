/*
 * Port of src/content/composition/q1-q2-supply.ts: cross-game supply
 * policy from base Q1 pickups to the recipient's base Q2 pools.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_Q2_SUPPLY_H
#define QA_CONTENT_COMPOSITION_Q1_Q2_SUPPLY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/pickups.h"

/* Anonymous TS loadout shape shared by the supply files:
 * { weapon, inventory: { item, count }[] }. */
typedef struct qa_composition_loadout_entry {
  qa_item_id item;
  double count;
} qa_composition_loadout_entry;
typedef struct qa_composition_supply_loadout {
  qa_item_id weapon;
  const qa_composition_loadout_entry *inventory;
  size_t inventory_count;
} qa_composition_supply_loadout;

/** TS q1Q2PickupSelect(); current is NULL for TS null. */
bool qa_composition_q1_q2_pickup_select(qa_item_id current, qa_item_id incoming,
                                        qa_pickup_selection selection);

extern const qa_pickup_supply_profile QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE;

/** TS q1Q2SupplyLoadout(); returned storage is static. */
qa_composition_supply_loadout qa_composition_q1_q2_supply_loadout(void);

#endif
