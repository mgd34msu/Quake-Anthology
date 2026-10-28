/* Port of src/compat/qvm/guest-memory.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/guest_memory.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static bool is_safe_integer(double v) {
  return isfinite(v) && v == trunc(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0;
}

void qa_qvm_guest_memory_init(qa_qvm_guest_memory *guest, qa_module_identity module,
                              qa_qvmx_memory *memory) {
  guest->module = module;
  guest->memory = memory;
  guest->copy_storage = NULL;
}

void qa_qvm_guest_memory_destroy(qa_qvm_guest_memory *guest) {
  if (guest == NULL) return;
  free(guest->copy_storage);
  guest->copy_storage = NULL;
}

static qa_guest_address make_address(qa_qvm_guest_memory *guest, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > (int64_t)guest->memory->length)
    qa_range_error("QVM address exceeds allocation");
  qa_guest_address address = {(const void *)guest, byte_offset};
  return address;
}

static size_t checked(qa_qvm_guest_memory *guest, qa_guest_address address, double byte_length) {
  if (address.address_space != (const void *)guest)
    qa_range_error("QVM pointer belongs to another module instance");
  if (!is_safe_integer(byte_length) || byte_length < 0 || address.byte_offset < 0 ||
      address.byte_offset > (int64_t)guest->memory->length ||
      (int64_t)byte_length > (int64_t)guest->memory->length - address.byte_offset)
    qa_range_error("QVM address range exceeds allocation");
  return (size_t)address.byte_offset;
}

qa_guest_address_result qa_qvm_guest_memory_pointer(qa_qvm_guest_memory *guest, int64_t raw_value) {
  if (raw_value < -2147483648LL || raw_value > 4294967295LL)
    qa_range_error("QVM pointer is outside its 32-bit representation");
  int32_t word = (int32_t)(uint32_t)raw_value;
  bool is_null = false;
  size_t offset = qa_qvmx_memory_pointer_offset(guest->memory, word, &is_null);
  qa_guest_address_result result = {false, {NULL, 0}};
  if (is_null) return result;
  result.has_value = true;
  result.value = make_address(guest, (int64_t)offset);
  return result;
}

qa_guest_address qa_qvm_guest_memory_offset(qa_qvm_guest_memory *guest, qa_guest_address address,
                                            int64_t displacement) {
  (void)checked(guest, address, 0);
  /* TS adds bigints; an overflowing sum is still outside the allocation. */
  if ((displacement > 0 && address.byte_offset > INT64_MAX - displacement) ||
      (displacement < 0 && address.byte_offset < INT64_MIN - displacement))
    qa_range_error("QVM address exceeds allocation");
  return make_address(guest, address.byte_offset + displacement);
}

qa_qvmx_view qa_qvm_guest_memory_borrow(qa_qvm_guest_memory *guest, qa_guest_address address,
                                        double byte_length) {
  size_t start = checked(guest, address, byte_length);
  return qa_qvmx_memory_data_view(guest->memory, (double)start, byte_length);
}

qa_qvmx_view qa_qvm_guest_memory_copy(qa_qvm_guest_memory *guest, qa_guest_address address,
                                      double byte_length) {
  size_t start = checked(guest, address, byte_length);
  free(guest->copy_storage);
  guest->copy_storage = malloc(byte_length > 0 ? (size_t)byte_length : 1);
  if (guest->copy_storage == NULL) qa_range_error("out of memory");
  memcpy(guest->copy_storage, guest->memory->bytes + start, (size_t)byte_length);
  qa_qvmx_view view = {guest->copy_storage, (size_t)byte_length};
  return view;
}

void qa_qvm_guest_memory_write(qa_qvm_guest_memory *guest, qa_guest_address address,
                               const uint8_t *bytes, size_t length) {
  size_t start = checked(guest, address, (double)length);
  qa_qvmx_memory_write_bytes(guest->memory, (double)start, bytes, length);
}

static qa_guest_address_result guest_pointer(void *ctx, int64_t raw_value) {
  return qa_qvm_guest_memory_pointer(ctx, raw_value);
}
static qa_guest_address guest_offset(void *ctx, qa_guest_address address, int64_t displacement) {
  return qa_qvm_guest_memory_offset(ctx, address, displacement);
}
static qa_byte_view guest_borrow(void *ctx, qa_guest_address address, double byte_length) {
  qa_qvmx_view view = qa_qvm_guest_memory_borrow(ctx, address, byte_length);
  qa_byte_view result = {view.bytes, view.length};
  return result;
}
static qa_byte_view guest_copy(void *ctx, qa_guest_address address, double byte_length) {
  qa_qvmx_view view = qa_qvm_guest_memory_copy(ctx, address, byte_length);
  qa_byte_view result = {view.bytes, view.length};
  return result;
}
static void guest_write(void *ctx, qa_guest_address address, qa_byte_view bytes) {
  qa_qvm_guest_memory_write(ctx, address, bytes.bytes, bytes.length);
}

qa_guest_memory qa_qvm_guest_memory_as_guest_memory(qa_qvm_guest_memory *guest) {
  qa_guest_memory memory;
  memset(&memory, 0, sizeof memory);
  memory.ctx = guest;
  memory.module = guest->module;
  memory.address_space = (const void *)guest;
  memory.pointer_bytes = 4;
  memory.pointer = guest_pointer;
  memory.offset = guest_offset;
  memory.borrow = guest_borrow;
  memory.copy = guest_copy;
  memory.write = guest_write;
  return memory;
}
