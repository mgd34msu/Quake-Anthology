/* Port of src/compat/q2/native-primary-inventory.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_INVENTORY_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_INVENTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/ui.h"

/* TS NativeInventoryPresentation. kind: "weapon"|"ammunition" (weapon) | "item" (icon, nullable). */
typedef struct qa_compat_q2_native_inventory_presentation { qa_provider_reference source; const char *kind; qa_item_id weapon; bool has_icon; const qa_weapon_hud_icon *icon; } qa_compat_q2_native_inventory_presentation;
typedef struct qa_compat_q2_native_inventory_row { bool has_presentation; qa_compat_q2_native_inventory_presentation presentation; qa_item_id item; const char *label; double count, source_index; bool selected, presence_only; } qa_compat_q2_native_inventory_row;
typedef struct qa_compat_q2_native_inventory_readout_item { qa_item_id item; const char *label; double count; } qa_compat_q2_native_inventory_readout_item;
/* items is owned by the inventory, valid until its next read. selected NULL = TS null. */
typedef struct qa_compat_q2_native_inventory_readout { bool has_presentation; qa_compat_q2_native_inventory_presentation presentation; const qa_compat_q2_native_inventory_readout_item *items; size_t item_count; qa_item_id selected; } qa_compat_q2_native_inventory_readout;
typedef struct qa_compat_q2_selection_write { double offset, bytes; } qa_compat_q2_selection_write;
typedef struct qa_compat_q2_native_primary_inventory_profile {
  qa_content_digest digest; qa_native_abi abi; double client, inventory, count, cursor, empty;
  qa_item_id proto_weapon, proto_ammunition, proto_usable, proto_passive, proto_droppable, proto_undroppable;
  const qa_compat_q2_selection_write *selection_writes; size_t selection_write_count;
  double next_entry, next_scan, next_join; bool next_menu_argument;
  double prev_entry, prev_scan, prev_join; double validate_entry; bool has_validate_scan; double validate_scan_entry, validate_scan_join;
  double use_entry, use_call, use_join; double named_entry, named_lookup_call, named_lookup_return, named_call, named_join;
} qa_compat_q2_native_primary_inventory_profile;
/** TS constructor hooks. rows returns false for the TS null; rows stay valid while the guest call that requested them runs. item_at returns NULL for the TS null. */
typedef struct qa_compat_q2_native_primary_inventory_hooks {
  void *ctx;
  bool (*rows)(void *ctx, qa_actor_id actor, const qa_compat_q2_native_inventory_row **out, size_t *out_count);
  void (*use)(void *ctx, qa_actor_id actor, qa_item_id item);
  qa_guest_address (*descriptor)(void *ctx, double index);
  qa_item_id (*item_at)(void *ctx, qa_guest_address address);
  void (*print)(void *ctx, qa_actor_id actor, const char *text);
} qa_compat_q2_native_primary_inventory_hooks;
typedef struct qa_compat_q2_native_primary_inventory qa_compat_q2_native_primary_inventory;
qa_compat_q2_native_primary_inventory *qa_compat_q2_native_primary_inventory_create(qa_compat_q2_native_primary_weapon_host host, const qa_compat_q2_native_primary_inventory_profile *profile, qa_compat_q2_native_primary_inventory_hooks hooks);
/* False is the TS null. */
bool qa_compat_q2_native_primary_inventory_read(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, qa_compat_q2_native_inventory_readout *out);
/* item NULL = TS null. */
void qa_compat_q2_native_primary_inventory_restore(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor, qa_item_id item);
void qa_compat_q2_native_primary_inventory_release(qa_compat_q2_native_primary_inventory *s, qa_actor_id actor);
void qa_compat_q2_native_primary_inventory_close(qa_compat_q2_native_primary_inventory *s);
void qa_compat_q2_native_primary_inventory_destroy(qa_compat_q2_native_primary_inventory *s);
#endif
