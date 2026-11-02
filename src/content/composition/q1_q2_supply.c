/* Port of src/content/composition/q1-q2-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1_q2_supply.h"

#include <string.h>

#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/foundation/support.h"

/* TS Q2_BASE_WEAPONS.findIndex(weapon => weapon.item === item)
 * (owner: src/content/q2/foundation/weapons/definitions.ts). */
static long weapon_index(qa_item_id item) {
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
    if (strcmp(qa_q2w_base_weapons[i].item, item) == 0) return (long)i;
  return -1;
}

bool qa_composition_q1_q2_pickup_select(qa_item_id current, qa_item_id incoming,
                                        qa_pickup_selection selection) {
  if (selection != QA_PICKUP_SELECTION_BETTER) return selection == QA_PICKUP_SELECTION_ALWAYS;
  long next = weapon_index(incoming);
  long previous = current == NULL ? -1 : weapon_index(current);
  if (next < 0 || (current != NULL && previous < 0))
    qa_range_error("Cross-game Q2 pickup ranking requires base weapons");
  return next > previous;
}

static const qa_item_id A0[] = {"q2:ammo_shells"}, A1[] = {"q2:ammo_bullets"};
static const qa_item_id A2[] = {"q2:ammo_rockets", "q2:ammo_grenades"}, A3[] = {"q2:ammo_cells"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q1:ammo/shells", A0, 1}, {"q1:ammo/nails", A1, 1}, {"q1:ammo/rockets", A2, 2}, {"q1:ammo/cells", A3, 1}};
static const qa_item_id W0[] = {"q2:weapon_blaster"}, W1[] = {"q2:weapon_shotgun"};
static const qa_item_id W2[] = {"q2:weapon_supershotgun"}, W3[] = {"q2:weapon_machinegun"};
static const qa_item_id W4[] = {"q2:weapon_chaingun"}, W5[] = {"q2:weapon_grenadelauncher"};
static const qa_item_id W6[] = {"q2:weapon_rocketlauncher"}, W7[] = {"q2:weapon_hyperblaster"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q1:weapon/axe", W0, 1}, {"q1:weapon/shotgun", W1, 1}, {"q1:weapon/supershotgun", W2, 1},
    {"q1:weapon/nailgun", W3, 1}, {"q1:weapon/supernailgun", W4, 1}, {"q1:weapon/grenadelauncher", W5, 1},
    {"q1:weapon/rocketlauncher", W6, 1}, {"q1:weapon/lightning", W7, 1}};

const qa_pickup_supply_profile QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE = {
    "composition:q1-base-q2-supply", AMMO, 4, NULL, 0, NULL, 0, WEAPONS, 8};

static const qa_composition_loadout_entry LOADOUT[] = {
    {"q2:weapon_blaster", 1}, {"q2:weapon_shotgun", 1}, {"q2:ammo_shells", 25}};

qa_composition_supply_loadout qa_composition_q1_q2_supply_loadout(void) {
  return (qa_composition_supply_loadout){"q2:weapon_shotgun", LOADOUT, 3};
}
