/*
 * Port of src/compat/qvm/mod-items.ts. The TS class name collides
 * with the landed contract type qa_qvm_mod_items (the declaration),
 * so the runtime class is qa_qvm_mod_items_runtime. Storage
 * read/write/capacity mirror src/compat/qvm/item-storage.ts
 * (parallel slice). Mirrored services surface (TS owner
 * src/world/session/mods.ts): qa_qvmy_item_services.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MOD_ITEMS_H
#define QA_COMPAT_QVM_MOD_ITEMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/mod_weapon_stage.h"
#include "qa/compat/qvm/regions.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mod_clients.h"

void qa_qvm_validate_mod_items(const qa_qvm_mod_items *items, const qa_qvm_mod_callback_declaration *declaration,
                               const qa_qvm_instruction *instructions, size_t instruction_count,
                               int64_t initialized_data_length, int64_t bss_length);

/* Admissions are the landed qa_source_item_admission and leases
 * the landed qa_source_item_lease (contracts/source_items.h). */

/* Lease bundle mirrored onto SourceItemLease call sites. */
typedef struct qa_qvmy_item_lease_binding {
  qa_provider_id owner;
  const qa_source_item_admission *items;
  size_t item_count;
  void *ctx;
  void (*invoke)(void *ctx, qa_item_id item, qa_source_item_action action);
  size_t (*read)(void *ctx, qa_inventory_entry *out, size_t capacity);
  bool (*entry)(void *ctx, qa_item_id item, qa_inventory_entry *out);
  void (*write)(void *ctx, qa_inventory_entry entry);
  bool (*mutable_capacity)(void *ctx, qa_item_id item);
} qa_qvmy_item_lease_binding;

typedef struct qa_qvmy_item_services {
  void *ctx;
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  qa_actor_id (*reference_saved)(void *ctx, qa_saved_actor_id saved);
  qa_source_item_lease (*bind_items)(void *ctx, qa_owned_actor owner, const qa_qvmy_item_lease_binding *binding);
  double (*inventory_count)(void *ctx, qa_actor_id actor, qa_item_id item);
  bool (*has_weapons)(void *ctx);
  bool (*weapon_selected)(void *ctx, qa_actor_id actor, qa_provider_id provider);
  bool (*weapon_bind)(void *ctx, qa_owned_actor owner, qa_source_weapon_binding binding,
                      qa_source_weapon_unbind *out);
  bool (*body_read)(void *ctx, qa_actor_id actor, qa_body_snapshot *out);
  bool (*player_view)(void *ctx, qa_actor_id actor, qa_mod_client_player_view *out);
} qa_qvmy_item_services;

typedef struct qa_qvm_mod_item_operations {
  void *ctx;
  int64_t (*pointer)(void *ctx, qa_actor_id actor, const char *record);
  bool (*live)(void *ctx, qa_actor_id actor);
  int32_t (*invoke)(void *ctx, qa_actor_id actor, const qa_qvm_mod_source_call *call);
  int32_t (*ground)(void *ctx, qa_actor_id actor);
  bool (*pickup)(void *ctx, qa_original_pickup_execution *out);
} qa_qvm_mod_item_operations;

typedef struct qa_qvm_mod_items_runtime {
  const qa_qvm_mod_items *definition;
  qa_qvm_memory *memory;
  const qa_qvmy_artifact *image;
  const qa_qvmy_item_services *services;
  qa_provider_id provider;
  qa_content_id content;
  const qa_qvm_mod_item_operations *operations;
  qa_source_item_admission *admissions; /* owned */
  size_t admission_count;
  qa_source_item_action *admission_actions; /* owned */
  qa_weapon_hud_icon *admission_icons;      /* owned */
  qa_qvm_mod_weapon_stage *weapons;         /* owned, nullable */
  void *entries;                            /* opaque */
  size_t entry_count;
  size_t entry_capacity;
  void *retired_entries; /* opaque; released entries kept for capability identity until destroy */
  size_t retired_entry_count;
  size_t retired_entry_capacity;
  int64_t next_request;
  qa_mod_client_application **applied;
  size_t applied_count;
  size_t applied_capacity;
} qa_qvm_mod_items_runtime;

void qa_qvm_mod_items_runtime_init(qa_qvm_mod_items_runtime *items, const qa_qvm_mod_items *definition,
                                   qa_qvm_module *module, const qa_qvmy_artifact *image,
                                   const qa_qvmy_item_services *services, qa_provider_id provider,
                                   qa_content_id content, const qa_qvm_mod_item_operations *operations);
void qa_qvm_mod_items_runtime_destroy(qa_qvm_mod_items_runtime *items);
void qa_qvm_mod_items_admit(qa_qvm_mod_items_runtime *items, qa_actor_id actor);
bool qa_qvm_mod_items_requested(qa_qvm_mod_items_runtime *items, qa_actor_id actor, int64_t *out);
void qa_qvm_mod_items_open(qa_qvm_mod_items_runtime *items, qa_mod_client_application *application,
                           void (**remove)(void *ctx), void **remove_ctx);
bool qa_qvm_mod_items_applies(qa_qvm_mod_items_runtime *items, const qa_qvm_mod_source_call *call);
int32_t qa_qvm_mod_items_apply(qa_qvm_mod_items_runtime *items, qa_mod_client_application *application,
                               int32_t (*run)(void *ctx), void *run_ctx);
qa_save_value *qa_qvm_mod_items_checkpoint(qa_qvm_mod_items_runtime *items);
void qa_qvm_mod_items_restore(qa_qvm_mod_items_runtime *items, const qa_save_value *value);
void qa_qvm_mod_items_clear(qa_qvm_mod_items_runtime *items);
void qa_qvm_mod_items_release(qa_qvm_mod_items_runtime *items, qa_actor_id actor);
void qa_qvm_mod_items_close(qa_qvm_mod_items_runtime *items);

#endif
