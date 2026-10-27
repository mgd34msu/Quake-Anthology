/*
 * Port of src/contracts/pickups.ts (types only): pickup supply
 * offers, previews, admission, and supply profiles. The
 * PickupAdmission interface becomes a function table + ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_PICKUPS_H
#define QA_CONTRACTS_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"

typedef struct qa_pickup_ammo_grant {
  qa_item_id item;
  double amount;
} qa_pickup_ammo_grant;

/* TS PickupSelection = "never" | "always" | "better". */
typedef enum qa_pickup_selection {
  QA_PICKUP_SELECTION_NEVER, QA_PICKUP_SELECTION_ALWAYS, QA_PICKUP_SELECTION_BETTER
} qa_pickup_selection;

/* when: "always" | "empty-ammo" (exact strings). */
typedef struct qa_ammo_weapon_selection {
  qa_pickup_selection mode;
  const char *when;
} qa_ammo_weapon_selection;

/* TS PickupSupplyOffer union on kind: "ammo"(grant) |
 * "ammoWeapon"(grant & weapon) | "weapon"(item + ammo grants). */
typedef enum qa_pickup_supply_offer_kind {
  QA_PICKUP_SUPPLY_AMMO, QA_PICKUP_SUPPLY_AMMO_WEAPON, QA_PICKUP_SUPPLY_WEAPON
} qa_pickup_supply_offer_kind;
typedef struct qa_pickup_supply_offer {
  qa_pickup_supply_offer_kind kind;
  qa_pickup_ammo_grant grant;   /* ammo / ammoWeapon */
  qa_item_id weapon;            /* ammoWeapon */
  qa_item_id item;              /* weapon */
  const qa_pickup_ammo_grant *ammo; /* weapon */
  size_t ammo_count;
} qa_pickup_supply_offer;

/* availability union on kind: "ready"(eligible) |
 * "respawning"(atSeconds) | "inactive". */
typedef struct qa_pickup_supply_observation {
  qa_actor_id actor;
  qa_pickup_supply_offer offer;
  const char *availability_kind;
  bool eligible;
  double at_seconds;
} qa_pickup_supply_observation;

typedef struct qa_pickup_ammo_receipt {
  qa_item_id item;
  double before;
  double given;
} qa_pickup_ammo_receipt;

typedef struct qa_pickup_supply_preview {
  bool accepted;
  const qa_pickup_ammo_receipt *ammo;
  size_t ammo_count;
  const qa_pickup_ammo_receipt *weapons;
  size_t weapon_count;
} qa_pickup_supply_preview;

/* ammoWeapon / weapon offer argument shapes. */
typedef struct qa_pickup_ammo_weapon_offer {
  qa_pickup_ammo_grant grant;
  qa_item_id weapon;
} qa_pickup_ammo_weapon_offer;
typedef struct qa_pickup_weapon_offer {
  qa_item_id item;
  const qa_pickup_ammo_grant *ammo;
  size_t ammo_count;
} qa_pickup_weapon_offer;

/** TS PickupAdmission. maps()'s kind: "ammo" | "weapons". ammo()'s
 * autoSwitch is optional in TS (has_ flag). */
typedef struct qa_pickup_admission {
  void *ctx;
  bool (*maps)(void *ctx, const char *kind, qa_item_id item);
  qa_pickup_supply_preview (*preview)(void *ctx, qa_actor_id actor,
                                      qa_pickup_supply_offer offer);
  bool (*owns)(void *ctx, qa_actor_id actor, qa_item_id source_weapon);
  bool (*ammo)(void *ctx, qa_owned_actor actor, qa_pickup_ammo_grant offer,
               bool has_auto_switch, bool auto_switch);
  bool (*ammo_weapon)(void *ctx, qa_owned_actor actor,
                      qa_pickup_ammo_weapon_offer offer,
                      qa_ammo_weapon_selection selection);
  bool (*weapon)(void *ctx, qa_owned_actor actor, qa_pickup_weapon_offer offer,
                 qa_pickup_selection selection);
  bool (*cargo)(void *ctx, qa_owned_actor actor,
                const qa_pickup_cargo_entry *cargo, size_t cargo_count,
                qa_pickup_selection selection);
} qa_pickup_admission;

/* Supply-profile mapping rows. destinations is a nonempty TS tuple
 * (pointer + count). */
typedef struct qa_pickup_supply_mapping {
  qa_item_id source;
  const qa_item_id *destinations;
  size_t destination_count;
} qa_pickup_supply_mapping;
typedef struct qa_pickup_supply_owner {
  qa_item_id item;
  qa_item_id source;
} qa_pickup_supply_owner;

/* weaponOwnership is the TS literal "all-destinations". The owner
 * lists are optional (NULL = absent). */
typedef struct qa_pickup_supply_profile {
  qa_item_id id;
  const qa_pickup_supply_mapping *ammo;
  size_t ammo_count;
  const qa_pickup_supply_owner *weapon_owners;
  size_t weapon_owner_count;
  const qa_pickup_supply_owner *ammo_owners;
  size_t ammo_owner_count;
  const qa_pickup_supply_mapping *weapons;
  size_t weapon_count;
} qa_pickup_supply_profile;

typedef struct qa_source_pickup_quantity {
  double amount;
  bool accepted;
} qa_source_pickup_quantity;

#endif
