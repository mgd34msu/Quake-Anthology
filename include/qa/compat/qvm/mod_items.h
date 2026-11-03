/*
 * Port of src/compat/qvm/mod-items.ts. The TS class name collides
 * with the landed contract type qa_qvm_mod_items (the declaration),
 * so the runtime class is qa_qvm_mod_items_runtime. Storage
 * read/write/capacity mirror src/compat/qvm/item-storage.ts
 * (parallel slice). Services are the landed qa_mod_host_services
 * (TS owner src/world/session/mods.ts; mods.h); the lease binding
 * is the landed qa_inventory_item_binding (inventory.h).
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
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/mod_clients.h"
#include "qa/world/session/mods.h"

void qa_qvm_validate_mod_items(const qa_qvm_mod_items *items, const qa_qvm_mod_callback_declaration *declaration,
                               const qa_qvm_instruction *instructions, size_t instruction_count,
                               int64_t initialized_data_length, int64_t bss_length);

/* Admissions are the landed qa_source_item_admission and leases
 * the landed qa_source_item_lease (contracts/source_items.h). */

/* Lease bundles are the landed qa_inventory_item_binding
 * (world/gameplay/inventory.h): owner/items, an
 * qa_inventory_state_binding state and an invoke fn + ctx. */

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
  const qa_resolved_qvm_artifact *image;
  qa_mod_host_services *services;
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
  uint64_t request_identity;              /* last request object identity */
  int64_t *applied_invocations;           /* owned: open applications already applied */
  size_t applied_count;
  size_t applied_capacity;
  qa_source_item_definition *presentation_items; /* owned: TS admissions.map(definition) */
  void **retained;                        /* owned: capability and binding contexts */
  size_t retained_count;
  size_t retained_capacity;
} qa_qvm_mod_items_runtime;

void qa_qvm_mod_items_runtime_init(qa_qvm_mod_items_runtime *items, const qa_qvm_mod_items *definition,
                                   qa_qvm_module *module, const qa_resolved_qvm_artifact *image,
                                   qa_mod_host_services *services, qa_provider_id provider,
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
