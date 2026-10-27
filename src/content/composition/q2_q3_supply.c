/* Port of src/content/composition/q2-q3-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2_q3_supply.h"

static const qa_pickup_supply_owner OWNERS[] = {
    {"q3:weapon/shotgun", "q2:weapon_shotgun"}, {"q3:weapon/machinegun", "q2:weapon_machinegun"},
    {"q3:weapon/grenadelauncher", "q2:weapon_grenadelauncher"}, {"q3:weapon/rocketlauncher", "q2:weapon_rocketlauncher"},
    {"q3:weapon/plasmagun", "q2:weapon_hyperblaster"}, {"q3:weapon/lightning", "q2:weapon_hyperblaster"},
    {"q3:weapon/railgun", "q2:weapon_railgun"}, {"q3:weapon/bfg", "q2:weapon_bfg"}};
static const qa_item_id A0[] = {"q3:ammo/shotgun"}, A1[] = {"q3:ammo/machinegun"};
static const qa_item_id A2[] = {"q2:ammo_cells", "q3:ammo/plasmagun", "q3:ammo/lightning", "q3:ammo/bfg"};
static const qa_item_id A3[] = {"q3:ammo/rocketlauncher"}, A4[] = {"q3:ammo/railgun"};
static const qa_item_id A5[] = {"q2:ammo_grenades", "q3:ammo/grenadelauncher"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q2:ammo_shells", A0, 1}, {"q2:ammo_bullets", A1, 1}, {"q2:ammo_cells", A2, 4},
    {"q2:ammo_rockets", A3, 1}, {"q2:ammo_slugs", A4, 1}, {"q2:ammo_grenades", A5, 2}};
static const qa_item_id W0[] = {"q3:weapon/shotgun"}, W1[] = {"q3:weapon/machinegun"}, W2[] = {"q3:weapon/grenadelauncher"};
static const qa_item_id W3[] = {"q3:weapon/rocketlauncher"}, W4[] = {"q3:weapon/plasmagun", "q3:weapon/lightning"};
static const qa_item_id W5[] = {"q3:weapon/railgun"}, W6[] = {"q3:weapon/bfg"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q2:weapon_shotgun", W0, 1}, {"q2:weapon_supershotgun", W0, 1}, {"q2:weapon_machinegun", W1, 1},
    {"q2:weapon_chaingun", W1, 1}, {"q2:ammo_grenades", W2, 1}, {"q2:weapon_grenadelauncher", W2, 1},
    {"q2:weapon_rocketlauncher", W3, 1}, {"q2:weapon_hyperblaster", W4, 2}, {"q2:weapon_railgun", W5, 1},
    {"q2:weapon_bfg", W6, 1}};

const qa_pickup_supply_profile QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE = {
    "composition:q2-base-q3-supply", AMMO, 6, OWNERS, 8, NULL, 0, WEAPONS, 10};
