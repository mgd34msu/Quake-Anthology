/* Port of src/compat/qvm/game-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GAME_PICKUPS_H
#define QA_COMPAT_QVM_GAME_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/game_inventory.h"
#include "qa/compat/qvm/item_catalog.h"
#include "qa/compat/qvm/module.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"

typedef struct qa_qvm_pickup_eligibility {
  qa_qvmy_function_call *call;
  qa_qvm_writable_view item;
  qa_qvm_writable_view player;
} qa_qvm_pickup_eligibility;

/* TS QvmPickupGrant. operation kind "return" is !is_region; the weapon
 * storage union is weapon_storage_inventory vs bits/ammo offsets. */
typedef struct qa_qvm_pickup_grant {
  double item_type, entry;
  const double *calls;
  size_t call_count;
  bool is_region;
  double accepted_return;               /* return kind */
  double region_entry, region_join, region_quantity; /* region kind */
  bool has_weapon;
  qa_qvm_region_evaluation weapon_quantity;
  bool weapon_storage_inventory;
  double weapon_bits_offset, weapon_ammo_offset;
  void *eligible_ctx;
  bool (*eligible)(void *ctx, qa_qvm_pickup_eligibility context);
} qa_qvm_pickup_grant;

typedef struct qa_qvm_pickup_profile {
  qa_module_identity module;
  qa_qvm_abi_profile abi_profile;
  double entity_stride, client_stride;
  double field_inuse, field_client, field_health, field_item, field_count, field_flags;
  double dropped_flag;
  qa_qvm_item_layout items;
  double touch;
  double gate_entry;
  const double *gate_calls;
  size_t gate_call_count;
  double gate_item_argument, gate_player_argument;
  double targets_entry;
  const double *targets_calls;
  size_t targets_call_count;
  double free_entry;
  const double *objective_types;
  size_t objective_type_count;
  const qa_qvm_pickup_grant *grants;
  size_t grant_count;
} qa_qvm_pickup_profile;

typedef struct qa_qvm_pickup_resolved_item {
  qa_item_id item;
  const qa_pickup_resource *resource; /* nullable */
} qa_qvm_pickup_resolved_item;

typedef int32_t (*qa_qvm_pickup_execute_fn)(void *ctx, qa_source_pickup_selection selection);

/* TS Options (module-private in TS). project may be NULL (TS optional);
 * it fills owned words freed with qa_qvm_inventory_words_destroy. */
typedef struct qa_qvm_primary_pickups_options {
  qa_qvm_game *game;
  const qa_resolved_qvm_artifact *artifact;
  qa_qvm_pickup_profile profile;
  bool has_catalog;
  void *catalog_ctx;
  const qa_qvm_catalog_records *(*catalog_records)(void *ctx);
  void *ctx;
  bool (*actor)(void *ctx, double slot, qa_owned_actor *out); /* false is TS null */
  bool (*current)(void *ctx, qa_owned_actor actor, double slot);
  qa_qvm_pickup_resolved_item (*resolve_item)(void *ctx, const qa_qvm_catalog_record *record);
  qa_source_time (*time)(void *ctx);
  bool has_supply;
  void *supply_ctx;
  bool (*supply_ammo)(void *ctx, const qa_qvm_catalog_record *record, qa_item_id *out);
  bool (*supply_owns)(void *ctx, qa_owned_actor actor, qa_item_id item);
  void (*supply_project)(void *ctx, qa_owned_actor actor, qa_item_id item, double count,
                         qa_qvm_inventory_words *out);
  int32_t (*run_source)(void *ctx, qa_original_pickup_offer offer, qa_qvm_pickup_execute_fn execute,
                        void *execute_ctx);
  bool own_free_hook; /* TS lifetime kind "own-free-hook" */
  void (*retire)(void *ctx, qa_owned_actor actor);
} qa_qvm_primary_pickups_options;

typedef struct qa_qvm_primary_pickups qa_qvm_primary_pickups;

/* NULL only on OOM. `options` is copied; game/artifact are borrowed. */
qa_qvm_primary_pickups *qa_qvm_primary_pickups_create(qa_qvm_primary_pickups_options options);
void qa_qvm_primary_pickups_destroy(qa_qvm_primary_pickups *pickups);
void qa_qvm_primary_pickups_close(qa_qvm_primary_pickups *pickups);
void qa_qvm_primary_pickups_assert_idle(const qa_qvm_primary_pickups *pickups);
/* The cancel target is a function call (qa_qvmy_function_call). */
void qa_qvm_primary_pickups_after_free(qa_qvm_primary_pickups *pickups, double pointer,
                                       qa_qvmy_function_call *control);

/* TS supply() result; quantity is absent when has_quantity is false. */
typedef struct qa_qvm_pickup_supply {
  qa_pickup_supply_offer offer;
  qa_pickup_ammo_grant ammo_grant; /* backing for offer.ammo */
  bool has_quantity;
  qa_qvm_primary_pickups *pickups; /* internal */
  uint64_t frame_sequence;         /* internal */
} qa_qvm_pickup_supply;

qa_qvm_pickup_supply qa_qvm_primary_pickups_supply(qa_qvm_primary_pickups *pickups,
                                                   qa_original_pickup_offer offer);
double qa_qvm_pickup_supply_quantity(qa_qvm_pickup_supply *supply, double count);

#endif
