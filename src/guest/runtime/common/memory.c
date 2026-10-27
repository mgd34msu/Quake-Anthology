/* Port of src/guest/runtime/common/memory.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/memory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void type_error(const char *message) { qa_range_error("%s", message); }
static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}

const qa_call_value *qa_guest_argument(const qa_call_value *args, size_t count, size_t index) {
  if (index >= count) qa_range_error("Missing guest argument %zu", index);
  return &args[index];
}
uint64_t qa_guest_integer_u64(const qa_call_value *args, size_t count, size_t index) {
  const qa_call_value *value = qa_guest_argument(args, count, index);
  if (value->kind != QA_CALL_INT32 && value->kind != QA_CALL_UINT32
      && value->kind != QA_CALL_INT64 && value->kind != QA_CALL_UINT64)
    type_error("Guest integer required");
  return qa_call_value_bigint_u64(value);
}
int64_t qa_guest_integer(const qa_call_value *args, size_t count, size_t index) {
  return (int64_t)qa_guest_integer_u64(args, count, index);
}
size_t qa_guest_count(const qa_call_value *args, size_t count, size_t index) {
  int64_t value = qa_guest_integer(args, count, index);
  if (value < 0 || value > 0x10000000)
    qa_range_error("Guest byte count exceeds runtime allocation limit");
  return (size_t)value;
}
bool qa_guest_pointer_argument(const qa_call_value *args, size_t count, size_t index, qa_guest_address *out) {
  const qa_call_value *value = qa_guest_argument(args, count, index);
  if (value->kind != QA_CALL_POINTER) type_error("Guest pointer required");
  if (!value->pointer_has) return false;
  *out = value->pointer;
  return true;
}
qa_guest_address qa_guest_required_pointer(const qa_call_value *args, size_t count, size_t index) {
  qa_guest_address address = {0};
  if (!qa_guest_pointer_argument(args, count, index, &address))
    type_error("Nonnull guest pointer required");
  return address;
}

static bool managed_sparse(const qa_mapped_guest_memory *memory) { return memory->managed_sparse; }

uint64_t qa_read_unsigned(const qa_mapped_guest_memory *memory, qa_guest_address address, int width) {
  if (managed_sparse(memory)) {
    switch (width) {
      case 1: return qa_memory_read_u8(memory, address);
      case 2: return qa_memory_read_u16(memory, address);
      case 4: return qa_memory_read_u32(memory, address);
      case 8: return qa_memory_read_u64(memory, address);
      default: break;
    }
  }
  uint8_t stack_bytes[16];
  uint8_t *bytes = width <= 16 ? stack_bytes : xmalloc((size_t)width);
  qa_memory_copy(memory, address, bytes, (size_t)width);
  uint64_t value = 0;
  for (int i = width - 1; i >= 0; i--) value = (value << 8) | bytes[i];
  if (bytes != stack_bytes) free(bytes);
  return value;
}
void qa_write_unsigned(const qa_mapped_guest_memory *memory, qa_guest_address address, int width, uint64_t value) {
  if (managed_sparse(memory)) {
    switch (width) {
      case 1: qa_memory_write_u8(memory, address, (uint8_t)value); return;
      case 2: qa_memory_write_u16(memory, address, (uint16_t)value); return;
      case 4: qa_memory_write_u32(memory, address, (uint32_t)value); return;
      case 8: qa_memory_write_u64(memory, address, value); return;
      default: break;
    }
  }
  uint8_t stack_bytes[16] = {0};
  uint8_t *bytes = width <= 16 ? stack_bytes : xmalloc((size_t)width);
  for (int i = 0; i < width; i++) bytes[i] = (uint8_t)(value >> (i * 8));
  qa_memory_write(memory, address, bytes, (size_t)width);
  if (bytes != stack_bytes) free(bytes);
}
bool qa_read_pointer(const qa_mapped_guest_memory *memory, qa_guest_address address, qa_guest_address *out) {
  return qa_memory_pointer(memory, qa_read_unsigned(memory, address, memory->pointer_bytes), out);
}
void qa_write_pointer(const qa_mapped_guest_memory *memory, qa_guest_address address,
                      bool has, qa_guest_address value) {
  if (has) (void)qa_memory_offset(memory, value, 0);
  qa_write_unsigned(memory, address, memory->pointer_bytes, has ? value.byte_offset : 0);
}
void qa_move_bytes(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                   qa_guest_address destination, qa_guest_address source, size_t byte_length) {
  if (managed_sparse(memory) && ext != NULL && ext->move != NULL) {
    ext->move(ext->ctx, destination, source, byte_length);
    return;
  }
  uint8_t *bytes = xmalloc(byte_length);
  qa_memory_copy(memory, source, bytes, byte_length);
  qa_memory_write(memory, destination, bytes, byte_length);
  free(bytes);
}
void qa_fill_bytes(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                   qa_guest_address address, size_t byte_length, int value) {
  if (managed_sparse(memory) && ext != NULL && ext->fill != NULL) {
    ext->fill(ext->ctx, address, byte_length, value);
    return;
  }
  uint8_t *bytes = xmalloc(byte_length);
  memset(bytes, value & 0xff, byte_length);
  qa_memory_write(memory, address, bytes, byte_length);
  free(bytes);
}
char *qa_read_string(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                     qa_guest_address address, size_t maximum) {
  if (managed_sparse(memory)) {
    size_t length = qa_string_length(memory, ext, address, maximum);
    char *text = xmalloc(length + 1);
    qa_memory_copy(memory, address, (uint8_t *)text, length);
    text[length] = '\0';
    return text;
  }
  char *text = xmalloc(maximum + 1);
  size_t length = 0;
  for (size_t i = 0; i < maximum; i++) {
    uint8_t unit = (uint8_t)qa_read_unsigned(memory,
        qa_memory_offset(memory, address, (int64_t)i), 1);
    if (unit == 0) { text[length] = '\0'; return text; }
    text[length++] = (char)unit;
  }
  free(text);
  qa_range_error("Guest string exceeds checked maximum");
  return NULL;
}
size_t qa_string_length(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                        qa_guest_address address, size_t maximum) {
  if (managed_sparse(memory) && ext != NULL && ext->find_zero != NULL) {
    int64_t length = ext->find_zero(ext->ctx, address, maximum);
    if (length < 0) qa_range_error("Guest string exceeds checked maximum");
    return (size_t)length;
  }
  if (managed_sparse(memory)) {
    /* Managed memory without find_zero: same scan SparseGuestMemory does. */
    for (size_t i = 0; i < maximum; i++)
      if (qa_memory_read_u8(memory, qa_memory_offset(memory, address, (int64_t)i)) == 0) return i;
    qa_range_error("Guest string exceeds checked maximum");
  }
  char *text = qa_read_string(memory, ext, address, maximum);
  size_t length = strlen(text);
  free(text);
  return length;
}
uint16_t *qa_read_string_wide(const qa_mapped_guest_memory *memory, qa_guest_address address,
                              size_t maximum, size_t *out_length) {
  uint16_t *text = xmalloc((maximum + 1) * sizeof(uint16_t));
  size_t length = 0;
  for (size_t i = 0; i < maximum; i++) {
    uint16_t unit = (uint16_t)qa_read_unsigned(memory,
        qa_memory_offset(memory, address, (int64_t)(i * 2)), 2);
    if (unit == 0) { text[length] = 0; *out_length = length; return text; }
    text[length++] = unit;
  }
  free(text);
  qa_range_error("Guest string exceeds checked maximum");
  return NULL;
}
uint8_t *qa_string_bytes(const char *value, bool wide, size_t *out_len) {
  size_t length = strlen(value);
  size_t width = wide ? 2 : 1;
  size_t total = (length + 1) * width;
  uint8_t *bytes = (uint8_t *)calloc(total, 1);
  if (bytes == NULL) qa_range_error("Guest runtime out of memory");
  for (size_t i = 0; i < length; i++) {
    unsigned code = (uint8_t)value[i];
    bytes[i * width] = (uint8_t)(code & 255);
    if (wide) bytes[i * width + 1] = (uint8_t)(code >> 8);
  }
  *out_len = total;
  return bytes;
}
uint8_t *qa_string_bytes_wide(const uint16_t *value, size_t length, bool wide, size_t *out_len) {
  size_t width = wide ? 2 : 1;
  size_t total = (length + 1) * width;
  uint8_t *bytes = (uint8_t *)calloc(total, 1);
  if (bytes == NULL) qa_range_error("Guest runtime out of memory");
  for (size_t i = 0; i < length; i++) {
    bytes[i * width] = (uint8_t)(value[i] & 255);
    if (wide) bytes[i * width + 1] = (uint8_t)(value[i] >> 8);
  }
  *out_len = total;
  return bytes;
}
size_t qa_native_allocation_bytes(size_t logical_bytes) {
  if (logical_bytes > (size_t)QA_MAX_SAFE_INTEGER - 4095)
    qa_range_error("Invalid native allocation size");
  size_t rounded = (logical_bytes + 4095) / 4096 * 4096;
  return rounded < 4096 ? 4096 : rounded;
}
qa_guest_address qa_allocate_native_memory(const qa_mapped_memory_ext *ext, size_t logical_bytes,
                                           const char *label) {
  return qa_gmem_allocate(ext, qa_native_allocation_bytes(logical_bytes), true, 4096,
                          false, QA_PERM_READ_WRITE, label);
}
