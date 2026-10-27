/* Port of src/content/composition/q1-q3-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1_q3_supply.h"

#include <string.h>

#include "qa/foundation/support.h"

static const qa_item_id A0[] = {"q3:ammo/shotgun"}, A1[] = {"q3:ammo/machinegun"};
static const qa_item_id A2[] = {"q3:ammo/rocketlauncher", "q3:ammo/grenadelauncher"}, A3[] = {"q3:ammo/lightning"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q1:ammo/shells", A0, 1}, {"q1:ammo/nails", A1, 1}, {"q1:ammo/rockets", A2, 2}, {"q1:ammo/cells", A3, 1}};
static const qa_item_id W0[] = {"q3:weapon/gauntlet"}, W1[] = {"q3:weapon/shotgun"}, W2[] = {"q3:weapon/machinegun"};
static const qa_item_id W3[] = {"q3:weapon/grenadelauncher"}, W4[] = {"q3:weapon/rocketlauncher"}, W5[] = {"q3:weapon/lightning"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q1:weapon/axe", W0, 1}, {"q1:weapon/shotgun", W1, 1}, {"q1:weapon/supershotgun", W1, 1},
    {"q1:weapon/nailgun", W2, 1}, {"q1:weapon/supernailgun", W2, 1}, {"q1:weapon/grenadelauncher", W3, 1},
    {"q1:weapon/rocketlauncher", W4, 1}, {"q1:weapon/lightning", W5, 1}};

const qa_pickup_supply_profile QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE = {
    "composition:q1-base-q3-supply", AMMO, 4, NULL, 0, NULL, 0, WEAPONS, 8};

/* Weapon.WP_SHOTGUN (TS owner: src/movement/q3/constants.ts). */
#define WP_SHOTGUN 3.0

bool qa_composition_q1_q3_supply_loadout(qa_provider_id provider, const char *product,
                                         qa_composition_q3_spawn_loadout_fn spawn_loadout, void *spawn_ctx,
                                         qa_inventory_entry *ammo_out, size_t ammo_capacity,
                                         qa_arsenal_state *out) {
  qa_arsenal_state loadout = spawn_loadout(spawn_ctx, provider, product != NULL ? product : "baseq3", false);
  if (loadout.state.kind == NULL || strcmp(loadout.state.kind, "q3") != 0)
    qa_range_error("Q3 loadout returned a foreign weapon state");
  if (loadout.ammo_count > ammo_capacity) return false;
  for (size_t i = 0; i < loadout.ammo_count; i++) {
    ammo_out[i] = loadout.ammo[i];
    qa_item_id item = loadout.ammo[i].item;
    ammo_out[i].count = strcmp(item, "q3:weapon/gauntlet") == 0 || strcmp(item, "q3:weapon/shotgun") == 0 ? 1
                        : strcmp(item, "q3:ammo/shotgun") == 0 ? 25 : 0;
  }
  *out = loadout;
  out->active_weapon = "q3:weapon/shotgun";
  out->state.source_weapon = WP_SHOTGUN;
  out->ammo = ammo_out;
  return true;
}
