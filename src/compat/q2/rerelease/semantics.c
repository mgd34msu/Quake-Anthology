/* Port of src/compat/q2/rerelease/semantics.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/semantics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_rows(const void *a, const void *b) {
  double d = ((const qa_compat_q2_rerelease_inventory_item *)a)->source_index - ((const qa_compat_q2_rerelease_inventory_item *)b)->source_index;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
const qa_compat_q2_rerelease_inventory_item *qa_compat_q2_rerelease_inventory_items(
    qa_compat_q2_rerelease_guest_module *module, void *string_ctx,
    qa_guest_address (*string)(void *, const char *), size_t *out_count) {
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  size_t capacity = (size_t)profile->client.inventory_count;
  bool *indices = calloc(capacity ? capacity : 1, sizeof *indices);
  static qa_compat_q2_rerelease_inventory_item *items = NULL; static size_t items_cap = 0;
  if (capacity > items_cap) { items = realloc(items, capacity * sizeof *items); items_cap = capacity; }
  size_t count = 0;
  const qa_compat_q2_rerelease_inventory_row *remaining = NULL;
  for (size_t i = 0; i < profile->inventory_count; i++) {
    const qa_compat_q2_rerelease_inventory_row *row = &profile->inventory[i];
    if (strcmp(row->source_kind, "remaining") == 0) { remaining = row; continue; }
    double value;
    if (strcmp(row->source_kind, "index") == 0) value = row->source_index;
    else {
      qa_guest_call_value arg = qa_compat_q2_call_pointer(true, string(string_ctx, row->source_name));
      qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_call_game(module, "Bot_GetItemID", &arg, 1, NULL, NULL);
      if (result.kind != QA_GUEST_VALUE_INT32) qa_range_error("Native item roster mismatch: %s", row->item);
      value = result.number_value;
    }
    if (value < 0 || value >= profile->client.inventory_count || indices[(size_t)value])
      qa_range_error("Native item roster mismatch: %s", row->item);
    indices[(size_t)value] = true;
    items[count++] = (qa_compat_q2_rerelease_inventory_item){row->item, value, row->capacity_is_ammo, row->capacity_source_index, row->capacity_count};
  }
  if (remaining) {
    size_t found = 0; double unnamed = -1;
    for (size_t i = 0; i < capacity; i++) if (!indices[i]) { found++; unnamed = (double)i; }
    if (found != 1) qa_range_error("Source inventory declaration requires exactly one remaining slot");
    items[count++] = (qa_compat_q2_rerelease_inventory_item){remaining->item, unnamed, remaining->capacity_is_ammo,
                                                            remaining->capacity_source_index, remaining->capacity_count};
  }
  bool cells_ok = false;
  for (size_t i = 0; i < count; i++) if (strcmp(items[i].item, profile->armor.cells) == 0) cells_ok = items[i].source_index == profile->armor.cells_index;
  if ((double)count != profile->client.inventory_count || !cells_ok)
    qa_range_error("Native inventory roster differs from the declared source storage");
  qsort(items, count, sizeof *items, compare_rows);
  free(indices);
  if (out_count) *out_count = count;
  return items;
}
