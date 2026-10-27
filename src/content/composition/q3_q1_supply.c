/*
 * Port of src/content/composition/q3-q1-supply.ts. TS derives the hipnotic
 * profile by mapping over the base weapons; the derived value is spelled
 * out statically here. GPL-2.0-or-later.
 */
#include "qa/content/composition/q3_q1_supply.h"

static const qa_pickup_supply_owner AMMO_OWNERS[] = {
    {"q1:ammo/shells", "q3:ammo/shotgun"}, {"q1:ammo/nails", "q3:ammo/machinegun"},
    {"q1:ammo/rockets", "q3:ammo/rocketlauncher"}, {"q1:ammo/cells", "q3:ammo/lightning"}};
static const qa_item_id A0[] = {"q1:ammo/shells"}, A1[] = {"q1:ammo/nails"}, A2[] = {"q1:ammo/rockets"}, A3[] = {"q1:ammo/cells"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q3:ammo/shotgun", A0, 1}, {"q3:ammo/machinegun", A1, 1}, {"q3:ammo/grenadelauncher", A2, 1},
    {"q3:ammo/rocketlauncher", A2, 1}, {"q3:ammo/lightning", A3, 1}, {"q3:ammo/railgun", A1, 1},
    {"q3:ammo/plasmagun", A3, 1}, {"q3:ammo/bfg", A3, 1}};
static const qa_item_id W0[] = {"q1:weapon/axe"}, W1[] = {"q1:weapon/supershotgun"}, W2[] = {"q1:weapon/nailgun"};
static const qa_item_id W3[] = {"q1:weapon/grenadelauncher"}, W4[] = {"q1:weapon/rocketlauncher"}, W5[] = {"q1:weapon/lightning"};
static const qa_item_id W6[] = {"q1:weapon/supernailgun"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q3:weapon/gauntlet", W0, 1}, {"q3:weapon/shotgun", W1, 1}, {"q3:weapon/machinegun", W2, 1},
    {"q3:weapon/grenadelauncher", W3, 1}, {"q3:weapon/rocketlauncher", W4, 1}, {"q3:weapon/lightning", W5, 1},
    {"q3:weapon/railgun", W6, 1}, {"q3:weapon/plasmagun", W5, 1}, {"q3:weapon/bfg", W5, 1}};
static const qa_item_id H3[] = {"q1:weapon/grenadelauncher", "q1:weapon/hipnotic:proximity"};
static const qa_item_id H7[] = {"q1:weapon/lightning", "q1:weapon/hipnotic:laser"};
static const qa_item_id H8[] = {"q1:weapon/lightning", "q1:weapon/hipnotic:mjolnir"};
static const qa_pickup_supply_mapping HIPNOTIC_WEAPONS[] = {
    {"q3:weapon/gauntlet", W0, 1}, {"q3:weapon/shotgun", W1, 1}, {"q3:weapon/machinegun", W2, 1},
    {"q3:weapon/grenadelauncher", H3, 2}, {"q3:weapon/rocketlauncher", W4, 1}, {"q3:weapon/lightning", W5, 1},
    {"q3:weapon/railgun", W6, 1}, {"q3:weapon/plasmagun", H7, 2}, {"q3:weapon/bfg", H8, 2}};

const qa_pickup_supply_profile QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE = {
    "composition:q3-base-q1-supply", AMMO, 8, NULL, 0, AMMO_OWNERS, 4, WEAPONS, 9};
const qa_pickup_supply_profile QA_COMPOSITION_Q3_HIPNOTIC_SUPPLY_PROFILE = {
    "composition:q3-hipnotic-supply", AMMO, 8, NULL, 0, AMMO_OWNERS, 4, HIPNOTIC_WEAPONS, 9};
