/*
 * Port of src/content/composition/q1-hipnotic-supply.ts. TS derives this
 * profile from Q1_Q3_SUPPLY_PROFILE at module load (identity ammo rows,
 * identity weapons plus hipnotic extras on lightning/grenadelauncher);
 * the derived value is spelled out statically here. GPL-2.0-or-later.
 */
#include "qa/content/composition/q1_hipnotic_supply.h"

static const qa_item_id A0[] = {"q1:ammo/shells"}, A1[] = {"q1:ammo/nails"}, A2[] = {"q1:ammo/rockets"}, A3[] = {"q1:ammo/cells"};
static const qa_pickup_supply_mapping AMMO[] = {
    {"q1:ammo/shells", A0, 1}, {"q1:ammo/nails", A1, 1}, {"q1:ammo/rockets", A2, 1}, {"q1:ammo/cells", A3, 1}};
static const qa_item_id W0[] = {"q1:weapon/axe"}, W1[] = {"q1:weapon/shotgun"}, W2[] = {"q1:weapon/supershotgun"};
static const qa_item_id W3[] = {"q1:weapon/nailgun"}, W4[] = {"q1:weapon/supernailgun"};
static const qa_item_id W5[] = {"q1:weapon/grenadelauncher", "q1:weapon/hipnotic:proximity"};
static const qa_item_id W6[] = {"q1:weapon/rocketlauncher"};
static const qa_item_id W7[] = {"q1:weapon/lightning", "q1:weapon/hipnotic:laser", "q1:weapon/hipnotic:mjolnir"};
static const qa_pickup_supply_mapping WEAPONS[] = {
    {"q1:weapon/axe", W0, 1}, {"q1:weapon/shotgun", W1, 1}, {"q1:weapon/supershotgun", W2, 1},
    {"q1:weapon/nailgun", W3, 1}, {"q1:weapon/supernailgun", W4, 1}, {"q1:weapon/grenadelauncher", W5, 2},
    {"q1:weapon/rocketlauncher", W6, 1}, {"q1:weapon/lightning", W7, 3}};

const qa_pickup_supply_profile QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE = {
    "composition:q1-hipnotic-supply", AMMO, 4, NULL, 0, NULL, 0, WEAPONS, 8};
