/*
 * Port of src/world/gameplay/pickups.ts. Preview receipt arrays are
 * malloc'd (free with qa_free_pickup_supply_preview). The quantity
 * resolver closure becomes fn ptr + ctx; its TS number |
 * SourcePickupQuantity result is qa_pickup_quantity_result.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_PICKUPS_H
#define QA_WORLD_GAMEPLAY_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
#include "qa/world/gameplay/inventory.h"

/** TS PickupGrantPlan. */
typedef struct qa_pickup_grant_plan {
  bool is_weapon;
  const qa_pickup_ammo_grant *weapons; /* weapon kind */
  size_t weapon_count;
  const qa_pickup_ammo_grant *ammo;
  size_t ammo_count;
  const char *acceptance; /* ammo kind: "positive" | "nonzero" */
  bool weapons_shared_ammo; /* ammo kind */
  const qa_item_id *shared_items;
  size_t shared_item_count;
} qa_pickup_grant_plan;

qa_pickup_supply_preview qa_preview_pickup_grants(const qa_inventory_entry *inventory,
                                                  size_t inventory_count,
                                                  qa_pickup_grant_plan plan);
void qa_free_pickup_supply_preview(qa_pickup_supply_preview preview);

/** TS PickupQuantityResolver result. */
typedef struct qa_pickup_quantity_result {
  bool is_source;
  double amount;
  bool accepted; /* source only */
} qa_pickup_quantity_result;
typedef qa_pickup_quantity_result (*qa_pickup_quantity_fn)(void *ctx, qa_inventory_entry entry);

/** TS SharedPickupAdmissionOptions. */
typedef struct qa_shared_pickup_admission_options {
  qa_shared_inventory_table *inventory;
  qa_pickup_supply_profile profile;
  void *ctx;
  void (*ammo_granted)(void *ctx, qa_owned_actor actor,
                       const qa_pickup_ammo_receipt *grants, size_t grant_count,
                       bool auto_switch);
  void (*weapon_granted)(void *ctx, qa_owned_actor actor, const qa_item_id *weapons,
                         size_t weapon_count, qa_pickup_selection selection);
} qa_shared_pickup_admission_options;

typedef struct qa_shared_pickup_admission qa_shared_pickup_admission;
/* NULL only on OOM; duplicate mappings abort (TS Error). */
qa_shared_pickup_admission *qa_shared_pickup_admission_create(
    qa_shared_pickup_admission_options options);
void qa_shared_pickup_admission_destroy(qa_shared_pickup_admission *admission);
/* kind: "ammo" | "weapons". */
bool qa_shared_pickup_admission_maps(qa_shared_pickup_admission *admission, const char *kind,
                                     qa_item_id item);
qa_pickup_supply_preview qa_shared_pickup_admission_preview(
    qa_shared_pickup_admission *admission, qa_actor_id actor, qa_pickup_supply_offer offer);
bool qa_shared_pickup_admission_owns(qa_shared_pickup_admission *admission, qa_actor_id actor,
                                     qa_item_id source_weapon);
/* quantity may be NULL. */
bool qa_shared_pickup_admission_ammo(qa_shared_pickup_admission *admission,
                                     qa_owned_actor actor, qa_pickup_ammo_grant offer,
                                     bool auto_switch, qa_pickup_quantity_fn quantity,
                                     void *quantity_ctx);
bool qa_shared_pickup_admission_ammo_weapon(qa_shared_pickup_admission *admission,
                                            qa_owned_actor actor,
                                            qa_pickup_ammo_weapon_offer offer,
                                            qa_ammo_weapon_selection selection,
                                            qa_pickup_quantity_fn quantity, void *quantity_ctx);
bool qa_shared_pickup_admission_weapon(qa_shared_pickup_admission *admission,
                                       qa_owned_actor actor, qa_pickup_weapon_offer offer,
                                       qa_pickup_selection selection,
                                       qa_pickup_quantity_fn quantity, void *quantity_ctx);
void qa_shared_pickup_admission_select_weapon(qa_shared_pickup_admission *admission,
                                              qa_owned_actor actor, qa_item_id item,
                                              qa_pickup_selection selection);
bool qa_shared_pickup_admission_cargo(qa_shared_pickup_admission *admission,
                                      qa_owned_actor actor, const qa_pickup_cargo_entry *cargo,
                                      size_t cargo_count, qa_pickup_selection selection);
bool qa_shared_pickup_admission_canonical(qa_shared_pickup_admission *admission,
                                          qa_owned_actor actor, qa_pickup_supply_offer offer,
                                          qa_pickup_selection selection,
                                          qa_pickup_quantity_fn quantity, void *quantity_ctx);
qa_pickup_admission qa_shared_pickup_admission_as_contract(qa_shared_pickup_admission *admission);

typedef struct qa_pickup_weapon {
  qa_item_id item;
  qa_item_id ammo; /* nullable */
} qa_pickup_weapon;
/** TS SelectedPickupWeapon. drop: "supply" | "none". */
typedef struct qa_selected_pickup_weapon {
  qa_item_id item;
  qa_item_id ammo; /* nullable */
  const char *drop;
} qa_selected_pickup_weapon;
typedef struct qa_weapon_source_entry {
  qa_item_id item;
  qa_item_id source; /* NULL = TS null */
} qa_weapon_source_entry;
/* Malloc'd result (free()); *out_count set. */
qa_weapon_source_entry *qa_selected_weapon_sources(
    qa_pickup_supply_profile profile, const qa_selected_pickup_weapon *selected,
    size_t selected_count, const qa_pickup_weapon *original, size_t original_count,
    size_t *out_count);

#endif
