/* Port of src/compat/q3/native/guest-services.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/guest_services.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

void qa_compat_q3_write_guest_string(qa_sparse_guest_memory *memory, qa_guest_core_address address, const char *value,
                                     int64_t capacity) {
  if (capacity < 1 || capacity > QA_MAX_SAFE_INTEGER) qa_range_error("Native string output requires positive source capacity");
  qa_sparse_check(memory, address, (size_t)capacity, QA_ACCESS_WRITE);
  uint8_t *bytes = calloc((size_t)capacity, 1);
  if (bytes == NULL) qa_range_error("Native guest string allocation failed");
  const size_t length = strlen(value);
  const size_t end = length < (size_t)(capacity - 1) ? length : (size_t)(capacity - 1);
  memcpy(bytes, value, end);
  qa_cleanup scope;
  qa_cleanup_push(&scope, free, bytes);
  qa_sparse_write(memory, address, bytes, (size_t)capacity);
  qa_cleanup_pop(&scope);
  free(bytes);
}

void qa_compat_q3_update_guest_cvar(qa_sparse_guest_memory *memory, qa_compat_q3_cvar_read_vm_fn read_vm, void *ctx,
                                    qa_guest_core_address address) {
  double modification_count = 0, numeric_value = 0, integer_value = 0;
  char *value = NULL;
  if (!read_vm(ctx, qa_sparse_read_i32(memory, address), &modification_count, &value, &numeric_value, &integer_value))
    return;
  if ((double)qa_sparse_read_i32(memory, qa_sparse_offset(memory, address, 4)) == modification_count) {
    free(value);
    return;
  }
  qa_cleanup scope;
  qa_cleanup_push(&scope, free, value);
  qa_sparse_check(memory, address, 272, QA_ACCESS_WRITE);
  qa_sparse_write_i32(memory, qa_sparse_offset(memory, address, 4), qa_int32(modification_count));
  if (strlen(value) >= 256) qa_range_error("Cvar_Update: source value exceeds MAX_CVAR_VALUE_STRING");
  qa_compat_q3_write_guest_string(memory, qa_sparse_offset(memory, address, 16), value, 256);
  qa_sparse_write_f32(memory, qa_sparse_offset(memory, address, 8), (double)(float)numeric_value);
  qa_sparse_write_i32(memory, qa_sparse_offset(memory, address, 12), qa_int32(integer_value));
  qa_cleanup_pop(&scope);
  free(value);
}
