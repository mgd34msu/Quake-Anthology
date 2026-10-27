/*
 * Port of src/world/gameplay/inventory.ts: the shared inventory
 * table. Returned entry arrays are table-owned scratch. The TS
 * SourceItemRetired throw aborts with QA_SOURCE_ITEM_RETIRED_MESSAGE.
 * isDeepStrictEqual entry comparisons use Object.is number equality
 * (NaN matches NaN, -0 differs from 0). GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_INVENTORY_H
#define QA_WORLD_GAMEPLAY_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/source_items.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/mod_composition.h"
#include "qa/world/gameplay/original_pickups.h"

/** TS InventoryStateBinding. entry / mutable_capacity may be NULL. */
typedef struct qa_inventory_state_binding {
  void *ctx;
  const qa_inventory_entry *(*read)(void *ctx, size_t *out_count);
  bool (*entry)(void *ctx, qa_item_id item, qa_inventory_entry *out);
  void (*write)(void *ctx, qa_inventory_entry entry);
  bool (*mutable_capacity)(void *ctx, qa_item_id item);
} qa_inventory_state_binding;

/** TS InventoryCommittedChange. */
typedef struct qa_inventory_committed_change {
  qa_owned_actor actor;
  bool has_before;
  qa_inventory_entry before;
  qa_inventory_entry after;
} qa_inventory_committed_change;

/** TS InventoryPickupBinding. */
typedef struct qa_inventory_pickup_binding {
  qa_provider_id owner;
  const qa_original_pickup_rule *rules;
  size_t rule_count;
} qa_inventory_pickup_binding;

/** TS InventoryItemBinding. invoke may be NULL. */
typedef struct qa_inventory_item_binding {
  qa_provider_id owner;
  const qa_source_item_admission *items;
  size_t item_count;
  qa_inventory_state_binding state;
  void *invoke_ctx;
  void (*invoke)(void *ctx, qa_item_id item, qa_source_item_action action);
} qa_inventory_item_binding;

/* TS InventoryGiveTransition on kind. */
typedef struct qa_inventory_give_transition {
  bool is_write;
  qa_inventory_entry entry;
  double given;
} qa_inventory_give_transition;
qa_inventory_give_transition qa_inventory_give(qa_inventory_entry entry, double count);

/* ModOperation request payloads. */
typedef struct qa_inventory_give_request { qa_owned_actor actor; qa_item_id item; double count; } qa_inventory_give_request;
typedef qa_inventory_give_request qa_inventory_consume_request;
typedef struct qa_inventory_configure_request { qa_owned_actor actor; qa_inventory_entry entry; } qa_inventory_configure_request;
typedef struct qa_inventory_adjust_request { qa_owned_actor actor; qa_item_id item; double delta; } qa_inventory_adjust_request;

/** TS itemAction's returned closure. */
typedef struct qa_source_item_action_invocation {
  bool has_value;
  void *ctx; /* table-owned */
  void (*call)(void *ctx);
} qa_source_item_action_invocation;

typedef struct qa_inventory_source_group {
  qa_provider_id owner;
  const qa_source_item_admission *items;
  size_t item_count;
} qa_inventory_source_group;
typedef struct qa_inventory_source_items_result {
  bool has_value;
  const qa_inventory_entry *primary;
  size_t primary_count;
  const qa_inventory_source_group *groups;
  size_t group_count;
} qa_inventory_source_items_result;

typedef struct qa_shared_inventory_table qa_shared_inventory_table;
/* NULL only on OOM. */
qa_shared_inventory_table *qa_shared_inventory_table_create(qa_session_actor_registry *actors);
void qa_shared_inventory_table_destroy(qa_shared_inventory_table *table);
qa_mod_operation *qa_shared_inventory_table_give_operation(qa_shared_inventory_table *table);
qa_mod_operation *qa_shared_inventory_table_consume_operation(qa_shared_inventory_table *table);
qa_mod_operation *qa_shared_inventory_table_configure_operation(qa_shared_inventory_table *table);
qa_mod_operation *qa_shared_inventory_table_adjust_operation(qa_shared_inventory_table *table);

void qa_shared_inventory_table_bind(qa_shared_inventory_table *table, qa_owned_actor actor,
                                    qa_inventory_state_binding binding);
qa_source_item_lease qa_shared_inventory_table_bind_items(qa_shared_inventory_table *table,
                                                          qa_owned_actor actor,
                                                          qa_inventory_item_binding requested);
qa_source_item_action_invocation qa_shared_inventory_table_item_action(
    qa_shared_inventory_table *table, qa_actor_id actor, qa_item_id item,
    qa_source_item_action action);
const qa_source_item_definition *qa_shared_inventory_table_item_definitions(
    qa_shared_inventory_table *table, qa_actor_id actor, size_t *out_count);
qa_inventory_source_items_result qa_shared_inventory_table_source_items(
    qa_shared_inventory_table *table, qa_owned_actor actor);
bool qa_shared_inventory_table_item_owner(qa_shared_inventory_table *table, qa_actor_id actor,
                                          qa_item_id item, qa_provider_id *out);
/* Returns an unbind id for qa_shared_inventory_table_unbind_pickup. */
int64_t qa_shared_inventory_table_bind_pickup(qa_shared_inventory_table *table,
                                              qa_owned_actor actor,
                                              qa_inventory_pickup_binding requested);
void qa_shared_inventory_table_unbind_pickup(qa_shared_inventory_table *table,
                                             qa_owned_actor actor, int64_t binding_id);
qa_original_pickup_resolution qa_shared_inventory_table_resolve_pickup(
    qa_shared_inventory_table *table, qa_owned_actor actor, qa_original_pickup_offer offer);
void qa_shared_inventory_table_create_entries(qa_shared_inventory_table *table,
                                              qa_owned_actor actor,
                                              const qa_inventory_entry *entries,
                                              size_t entry_count);
const qa_inventory_entry *qa_shared_inventory_table_entries(qa_shared_inventory_table *table,
                                                            qa_actor_id actor,
                                                            size_t *out_count);
bool qa_shared_inventory_table_has(qa_shared_inventory_table *table, qa_actor_id actor);
bool qa_shared_inventory_table_mutable_capacity(qa_shared_inventory_table *table,
                                                qa_actor_id actor, qa_item_id item);
double qa_shared_inventory_table_count(qa_shared_inventory_table *table, qa_actor_id actor,
                                       qa_item_id item);
bool qa_shared_inventory_table_consume(qa_shared_inventory_table *table, qa_owned_actor actor,
                                       qa_item_id item, double count);
double qa_shared_inventory_table_give(qa_shared_inventory_table *table, qa_owned_actor actor,
                                      qa_item_id item, double count);
/* committed may be NULL. */
void qa_shared_inventory_table_configure(
    qa_shared_inventory_table *table, qa_owned_actor actor, qa_inventory_entry entry,
    void (*committed)(void *ctx, qa_inventory_committed_change change), void *committed_ctx);
double qa_shared_inventory_table_adjust_source_counter(qa_shared_inventory_table *table,
                                                       qa_owned_actor actor, qa_item_id item,
                                                       double delta);
qa_inventory_table qa_shared_inventory_table_as_contract(qa_shared_inventory_table *table);

#endif
