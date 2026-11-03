/* Port of src/app/bootstrap/network/unified-finite.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_finite.h"
#include <math.h>
bool qa_app_unified_has_non_finite(const qa_save_value *value, int depth) {
  if (value == NULL) return false;
  if (value->kind == QA_SAVE_NUMBER) return !isfinite(value->number);
  if (depth > 32) return false;
  if (value->kind == QA_SAVE_ARRAY) {
    for (size_t i = 0; i < value->item_count; i++)
      if (qa_app_unified_has_non_finite(value->items[i], depth + 1)) return true;
    return false;
  }
  if (value->kind == QA_SAVE_OBJECT) {
    for (size_t i = 0; i < value->entry_count; i++)
      if (qa_app_unified_has_non_finite(value->entries[i].value, depth + 1)) return true;
  }
  return false;
}
/* TS copies only the paths holding non-finite numbers; on an owned tree that is an in-place
 * rewrite of exactly those numbers, with the same depth bound. */
void qa_app_unified_zero_non_finite(qa_save_value *value, int depth) {
  if (value == NULL) return;
  if (value->kind == QA_SAVE_NUMBER) { if (!isfinite(value->number)) value->number = 0; return; }
  if (depth > 32 || !qa_app_unified_has_non_finite(value, depth)) return;
  if (value->kind == QA_SAVE_ARRAY)
    for (size_t i = 0; i < value->item_count; i++) qa_app_unified_zero_non_finite(value->items[i], depth + 1);
  else if (value->kind == QA_SAVE_OBJECT)
    for (size_t i = 0; i < value->entry_count; i++) qa_app_unified_zero_non_finite(value->entries[i].value, depth + 1);
}
