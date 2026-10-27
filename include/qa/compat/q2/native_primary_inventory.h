/* Port of src/compat/q2/native-primary-inventory.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_INVENTORY_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_INVENTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"

typedef struct qa_compat_q2_native_inventory_presentation { qa_provider_reference source; const char *kind; qa_item_id weapon; bool has_icon; } qa_compat_q2_native_inventory_presentation;
typedef struct qa_compat_q2_native_inventory_row { bool has_presentation; qa_compat_q2_native_inventory_presentation presentation; qa_item_id item; const char *label; double count, source_index; bool selected, presence_only; } qa_compat_q2_native_inventory_row;
typedef struct qa_compat_q2_native_inventory_readout { const qa_compat_q2_native_inventory_row *items; size_t item_count; qa_item_id selected; } qa_compat_q2_native_inventory_readout;
typedef struct qa_compat_q2_selection_write { double offset, bytes; } qa_compat_q2_selection_write;
typedef struct qa_compat_q2_native_primary_inventory_profile {
  qa_content_digest digest; qa_native_abi abi; double client, inventory, count, cursor, empty;
  qa_item_id proto_weapon, proto_ammunition, proto_usable, proto_passive, proto_droppable, proto_undroppable;
  const qa_compat_q2_selection_write *selection_writes; size_t selection_write_count;
  double next_entry, next_scan, next_join; bool next_menu_argument;
  double prev_entry, prev_scan, prev_join; double validate_entry; bool has_validate_scan; double validate_scan_entry, validate_scan_join;
  double use_entry, use_call, use_join; double named_entry, named_lookup_call, named_lookup_return, named_call, named_join;
} qa_compat_q2_native_primary_inventory_profile;
typedef struct qa_compat_q2_native_primary_inventory { qa_compat_q2_native_primary_weapon_host host; const qa_compat_q2_native_primary_inventory_profile *profile; bool closed; } qa_compat_q2_native_primary_inventory;
void qa_compat_q2_native_primary_inventory_init(qa_compat_q2_native_primary_inventory *s, qa_compat_q2_native_primary_weapon_host h, const qa_compat_q2_native_primary_inventory_profile *p);
void qa_compat_q2_native_primary_inventory_close(qa_compat_q2_native_primary_inventory *s);
#endif
