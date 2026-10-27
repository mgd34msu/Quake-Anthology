/*
 * Port of src/content/composition/q2-q1-supply.ts. TS derives the hipnotic
 * profile by mapping over the base one; the derived value is spelled out
 * statically here (extra destinations on hyperblaster/grenadelauncher/bfg
 * plus matching weaponOwners). GPL-2.0-or-later.
 */
#include "qa/content/composition/q2_q1_supply.h"

static const qa_pickup_supply_owner OWNERS[] = {
    {"q1:weapon/axe", "q2:weapon_blaster"}, {"q1:weapon/shotgun", "q2:weapon_shotgun"},
    {"q1:weapon/supershotgun", "q2:weapon_supershotgun"}, {"q1:weapon/nailgun", "q2:weapon_machinegun"},
    {"q1:weapon/supernailgun", "q2:weapon_chaingun"}, {"q1:weapon/grenadelauncher", "q2:weapon_grenadelauncher"},
    {"q1:weapon/rocketlauncher", "q2:weapon_rocketlauncher"}, {"q1:weapon/lightning", "q2:weapon_hyperblaster"}};
static const qa_pickup_supply_owner HIPNOTIC_OWNERS[] = {
    {"q1:weapon/axe", "q2:weapon_blaster"}, {"q1:weapon/shotgun", "q2:weapon_shotgun"},
    {"q1:weapon/supershotgun", "q2:weapon_supershotgun"}, {"q1:weapon/nailgun", "q2:weapon_machinegun"},
    {"q1:weapon/supernailgun", "q2:weapon_chaingun"}, {"q1:weapon/grenadelauncher", "q2:weapon_grenadelauncher"},
    {"q1:weapon/rocketlauncher", "q2:weapon_rocketlauncher"}, {"q1:weapon/lightning", "q2:weapon_hyperblaster"},
    {"q1:weapon/hipnotic:laser", "q2:weapon_hyperblaster"}, {"q1:weapon/hipnotic:proximity", "q2:weapon_grenadelauncher"},
    {"q1:weapon/hipnotic:mjolnir", "q2:weapon_bfg"}};

static const qa_item_id A0[] = {"q1:ammo/shells"}, A1[] = {"q1:ammo/nails"};
static const qa_item_id A2[] = {"q2:ammo_cells", "q1:ammo/cells"}, A3[] = {"q1:ammo/rockets"};
static const qa_item_id A5[] = {"q2:ammo_grenades", "q1:ammo/rockets"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q2:ammo_shells", A0, 1}, {"q2:ammo_bullets", A1, 1}, {"q2:ammo_cells", A2, 2},
    {"q2:ammo_rockets", A3, 1}, {"q2:ammo_slugs", A1, 1}, {"q2:ammo_grenades", A5, 2}};
static const qa_item_id W0[] = {"q1:weapon/axe"}, W1[] = {"q1:weapon/shotgun"}, W2[] = {"q1:weapon/supershotgun"};
static const qa_item_id W3[] = {"q1:weapon/nailgun"}, W4[] = {"q1:weapon/supernailgun"}, W5[] = {"q1:weapon/grenadelauncher"};
static const qa_item_id W6[] = {"q1:weapon/rocketlauncher"}, W7[] = {"q1:weapon/lightning"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q2:weapon_blaster", W0, 1}, {"q2:weapon_shotgun", W1, 1}, {"q2:weapon_supershotgun", W2, 1},
    {"q2:weapon_machinegun", W3, 1}, {"q2:weapon_chaingun", W4, 1}, {"q2:ammo_grenades", W5, 1},
    {"q2:weapon_grenadelauncher", W5, 1}, {"q2:weapon_rocketlauncher", W6, 1}, {"q2:weapon_hyperblaster", W7, 1},
    {"q2:weapon_railgun", W4, 1}, {"q2:weapon_bfg", W7, 1}};
static const qa_item_id H5[] = {"q1:weapon/grenadelauncher", "q1:weapon/hipnotic:proximity"};
static const qa_item_id H7[] = {"q1:weapon/lightning", "q1:weapon/hipnotic:laser"};
static const qa_item_id H10[] = {"q1:weapon/lightning", "q1:weapon/hipnotic:mjolnir"};
static const qa_pickup_supply_mapping HIPNOTIC_WEAPONS[] = {
    {"q2:weapon_blaster", W0, 1}, {"q2:weapon_shotgun", W1, 1}, {"q2:weapon_supershotgun", W2, 1},
    {"q2:weapon_machinegun", W3, 1}, {"q2:weapon_chaingun", W4, 1}, {"q2:ammo_grenades", W5, 1},
    {"q2:weapon_grenadelauncher", H5, 2}, {"q2:weapon_rocketlauncher", W6, 1}, {"q2:weapon_hyperblaster", H7, 2},
    {"q2:weapon_railgun", W4, 1}, {"q2:weapon_bfg", H10, 2}};

const qa_pickup_supply_profile QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE = {
    "composition:q2-base-q1-supply", AMMO, 6, OWNERS, 8, NULL, 0, WEAPONS, 11};
const qa_pickup_supply_profile QA_COMPOSITION_Q2_HIPNOTIC_SUPPLY_PROFILE = {
    "composition:q2-hipnotic-supply", AMMO, 6, HIPNOTIC_OWNERS, 11, NULL, 0, HIPNOTIC_WEAPONS, 11};
