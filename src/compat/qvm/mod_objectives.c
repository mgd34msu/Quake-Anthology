/* See include/qa/compat/qvm/mod_objectives.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_objectives.h"

#include <math.h>

#include "qa/foundation/support.h"

static int64_t word(int64_t end, double address_value) {
  if (!isfinite(address_value) || address_value != trunc(address_value) ||
      address_value < -9007199254740991.0 || address_value > 9007199254740991.0 ||
      (int64_t)address_value < 0 || (int64_t)address_value % 4 != 0 ||
      (int64_t)address_value + 4 > end)
    qa_range_error("Objective address exceeds original QVM data or is not aligned");
  return (int64_t)address_value;
}

void qa_qvm_validate_objective_address(int64_t end, const qa_qvm_mod_objective_address *address) {
  if (!address->is_global_pointer) {
    (void)word(end, address->address);
    return;
  }
  (void)word(end, address->pointer.address);
  (void)word(end, address->pointer.offset);
  for (size_t i = 0; i < address->pointer.indirections.count; i++)
    (void)word(end, address->pointer.indirections.values[i]);
}

static int32_t read_word(qa_qvm_memory *memory, int64_t offset) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, offset, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

int64_t qa_qvm_resolve_objective_address(qa_qvm_memory *memory, int64_t end,
                                         const qa_qvm_mod_objective_address *address,
                                         qa_qvm_objective_current_fn current, void *current_ctx) {
  current(current_ctx);
  if (!address->is_global_pointer) return word(end, address->address);
  int64_t pointer = read_word(memory, word(end, address->pointer.address));
  for (size_t i = 0; i < address->pointer.indirections.count; i++) {
    current(current_ctx);
    if (pointer == 0) qa_range_error("Objective selector follows a null source pointer");
    int64_t at = word(end, (double)(word(end, (double)pointer) +
                                     (int64_t)address->pointer.indirections.values[i]));
    pointer = read_word(memory, at);
  }
  current(current_ctx);
  if (pointer == 0) qa_range_error("Objective selector follows a null source pointer");
  return word(end, (double)(word(end, (double)pointer) + (int64_t)address->pointer.offset));
}
