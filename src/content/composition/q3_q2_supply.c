/* Port of src/content/composition/q3-q2-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q3_q2_supply.h"

static const qa_pickup_supply_owner AMMO_OWNERS[] = {
    {"q2:ammo_bullets", "q3:ammo/machinegun"}, {"q2:ammo_shells", "q3:ammo/shotgun"},
    {"q2:ammo_grenades", "q3:ammo/grenadelauncher"}, {"q2:ammo_rockets", "q3:ammo/rocketlauncher"},
    {"q2:ammo_cells", "q3:ammo/lightning"}, {"q2:ammo_slugs", "q3:ammo/railgun"}};
static const qa_item_id A0[] = {"q2:ammo_bullets"}, A1[] = {"q2:ammo_shells"}, A2[] = {"q2:ammo_grenades"};
static const qa_item_id A3[] = {"q2:ammo_rockets"}, A4[] = {"q2:ammo_cells"}, A5[] = {"q2:ammo_slugs"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q3:ammo/machinegun", A0, 1}, {"q3:ammo/shotgun", A1, 1}, {"q3:ammo/grenadelauncher", A2, 1},
    {"q3:ammo/rocketlauncher", A3, 1}, {"q3:ammo/lightning", A4, 1}, {"q3:ammo/plasmagun", A4, 1},
    {"q3:ammo/bfg", A4, 1}, {"q3:ammo/railgun", A5, 1}};
static const qa_item_id W0[] = {"q2:weapon_blaster"}, W1[] = {"q2:weapon_machinegun", "q2:weapon_chaingun"};
static const qa_item_id W2[] = {"q2:weapon_shotgun", "q2:weapon_supershotgun"}, W3[] = {"q2:weapon_grenadelauncher"};
static const qa_item_id W4[] = {"q2:weapon_rocketlauncher"}, W5[] = {"q2:weapon_hyperblaster"};
static const qa_item_id W6[] = {"q2:weapon_railgun"}, W7[] = {"q2:weapon_bfg"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q3:weapon/gauntlet", W0, 1}, {"q3:weapon/machinegun", W1, 2}, {"q3:weapon/shotgun", W2, 2},
    {"q3:weapon/grenadelauncher", W3, 1}, {"q3:weapon/rocketlauncher", W4, 1}, {"q3:weapon/lightning", W5, 1},
    {"q3:weapon/plasmagun", W5, 1}, {"q3:weapon/railgun", W6, 1}, {"q3:weapon/bfg", W7, 1}};

const qa_pickup_supply_profile QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE = {
    "composition:q3-base-q2-supply", AMMO, 8, NULL, 0, AMMO_OWNERS, 6, WEAPONS, 9};

static const qa_composition_loadout_entry LOADOUT[] = {
    {"q2:weapon_blaster", 1}, {"q2:weapon_machinegun", 1}, {"q2:weapon_chaingun", 1}, {"q2:ammo_bullets", 100}};

qa_composition_supply_loadout qa_composition_q3_q2_supply_loadout(void) {
  return (qa_composition_supply_loadout){"q2:weapon_machinegun", LOADOUT, 4};
}
