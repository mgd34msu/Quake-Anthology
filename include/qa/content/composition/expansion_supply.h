/*
 * Port of src/content/composition/expansion-supply.ts: mixed-game supply
 * policy that retains each base grant and adds the selected expansions'
 * related equipment. The returned PickupSupplyProfile is heap-owned
 * (qa_composition_owned_profile); item strings stay borrowed from the
 * input profile and the static catalogs. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_EXPANSION_SUPPLY_H
#define QA_CONTENT_COMPOSITION_EXPANSION_SUPPLY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/pickups.h"

/* TS ExpansionSupply union, in declaration order. */
typedef enum qa_composition_expansion_supply {
  QA_COMPOSITION_EXPANSION_Q1_ROGUE,
  QA_COMPOSITION_EXPANSION_Q1_MG3,
  QA_COMPOSITION_EXPANSION_Q2_XATRIX,
  QA_COMPOSITION_EXPANSION_Q2_ROGUE,
  QA_COMPOSITION_EXPANSION_Q3_MISSIONPACK,
  QA_COMPOSITION_EXPANSION_COUNT
} qa_composition_expansion_supply_kind;

/** Exact TS string for one expansion ("q1-rogue", ...). */
const char *qa_composition_expansion_supply_name(qa_composition_expansion_supply_kind expansion);

/* Heap-owned PickupSupplyProfile produced by the composition transforms.
 * The owner arrays carry has_ flags because TS distinguishes an absent
 * list from an empty one; the view points at a static dummy for an
 * empty-but-present list so NULL still means absent. */
typedef struct qa_composition_owned_profile {
  char *id;
  qa_pickup_supply_mapping *ammo;
  size_t ammo_count;
  qa_pickup_supply_mapping *weapons;
  size_t weapon_count;
  qa_pickup_supply_owner *weapon_owners;
  size_t weapon_owner_count;
  bool has_weapon_owners;
  qa_pickup_supply_owner *ammo_owners;
  size_t ammo_owner_count;
  bool has_ammo_owners;
} qa_composition_owned_profile;

qa_pickup_supply_profile qa_composition_owned_profile_view(const qa_composition_owned_profile *owned);
void qa_composition_owned_profile_destroy(qa_composition_owned_profile *owned);

/** TS expansionSupply(): false is OOM only; *out is zeroed on entry. */
bool qa_composition_expansion_supply(const qa_pickup_supply_profile *profile,
                                     const qa_composition_expansion_supply_kind *expansions,
                                     size_t expansion_count,
                                     qa_composition_owned_profile *out);

#endif
