/* Port of src/compat/q3/native/memory.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/memory.h"

#include <math.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

void qa_native_game_memory_init(qa_native_game_memory *native, qa_sparse_guest_memory *memory) {
  if (memory->base.pointer_bytes != 4)
    qa_range_error("Q3 game pointer arguments are 32-bit words; later intptr_t profiles need their own memory");
  memset(native, 0, sizeof *native);
  native->memory = memory;
}
void qa_native_game_memory_destroy(qa_native_game_memory *native) {
  for (size_t i = 0; i < native->copy_count; i++) free(native->copies[i]);
  free(native->copies);
  memset(native, 0, sizeof *native);
}

/* TS at(word): the guest address of a 32-bit pointer word; false for null. */
static bool at(qa_native_game_memory *native, int64_t word, qa_guest_core_address *out) {
  if (word < -0x80000000LL || word > 0xffffffffLL) qa_range_error("Native game pointer must be a 32-bit word");
  return qa_sparse_pointer(native->memory, (uint64_t)(uint32_t)word, out);
}
static qa_guest_core_address required(qa_native_game_memory *native, int64_t word) {
  qa_guest_core_address address;
  if (!at(native, word, &address)) qa_range_error("Native game memory span requires a nonnull pointer");
  return address;
}

/* TS bytes(address, length): a live borrow, or a read-only copy when the
 * borrow faults on permission; any other fault propagates. */
static qa_qvm_game_region bytes_at(qa_native_game_memory *native, qa_guest_core_address address, int64_t length) {
  if (length < 0 || length > QA_MAX_SAFE_INTEGER) qa_range_error("Native game memory length is invalid");
  qa_sparse_guest_memory *memory = native->memory;
  qa_guest_throw *outer = memory->throw_ctx;
  jmp_buf buf;
  qa_guest_throw caught;
  qa_guest_throw_init(&caught, &buf);
  memory->throw_ctx = &caught;
  qa_qvm_game_region region = {NULL, (size_t)length, false};
  if (setjmp(buf) == 0) {
    region.bytes = qa_sparse_borrow(memory, address, (size_t)length);
    memory->throw_ctx = outer;
    return region;
  }
  memory->throw_ctx = outer;
  if (caught.kind != QA_GUEST_THROW_MEMORY || caught.memory.reason != QA_FAULT_PERMISSION) {
    if (outer != NULL && outer->buf != NULL) {
      jmp_buf *target = outer->buf;
      *outer = caught;
      outer->buf = target;
      longjmp(*target, 1);
    }
    qa_range_error("%s", caught.memory.message);
  }
  uint8_t *copy = malloc(length == 0 ? 1 : (size_t)length);
  if (copy == NULL) qa_range_error("Native game memory out of memory");
  qa_sparse_copy(memory, address, copy, (size_t)length);
  if (native->copy_count == native->copy_cap) {
    native->copy_cap = native->copy_cap == 0 ? 8 : native->copy_cap * 2;
    uint8_t **grown = realloc(native->copies, native->copy_cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Native game memory out of memory");
    native->copies = grown;
  }
  native->copies[native->copy_count++] = copy;
  region.bytes = copy;
  region.read_only = true;
  return region;
}
static void writable(const qa_qvm_game_region *region) {
  if (region->read_only) qa_range_error("Native module memory is read-only");
}
static void region_view_of(const qa_qvm_game_region *region, int64_t offset, int64_t length, qa_qvm_writable_view *out) {
  qa_qvm_writable_view_init(out, region->bytes, region->length, offset, length, NULL);
  out->read_only = region->read_only;
}

static bool native_address(void *ctx, int64_t word, int64_t *out) {
  qa_guest_core_address address;
  if (!at(ctx, word, &address)) return false;
  *out = (int64_t)address.byte_offset;
  return true;
}
static int64_t native_pointer_word(void *ctx, bool has_address, int64_t address) {
  (void)ctx;
  return has_address ? (int64_t)(int32_t)(uint32_t)address : 0;
}
static void native_view_at(void *ctx, int64_t address, int64_t length, qa_qvm_writable_view *out) {
  /* address | 0 */
  const qa_qvm_game_region bytes = bytes_at(ctx, required(ctx, (int64_t)(int32_t)(uint32_t)address), length);
  region_view_of(&bytes, 0, length, out);
}
static bool native_pointer(void *ctx, int64_t word, qa_qvm_game_region *out) {
  qa_native_game_memory *native = ctx;
  qa_guest_core_address address;
  if (!at(native, word, &address)) return false;
  size_t count = qa_sparse_mappings(native->memory, NULL, 0);
  qa_guest_mapping *mappings = malloc((count == 0 ? 1 : count) * sizeof *mappings);
  if (mappings == NULL) qa_range_error("Native game memory out of memory");
  count = qa_sparse_mappings(native->memory, mappings, count);
  bool found = false;
  uint64_t end = 0;
  for (size_t i = 0; i < count && !found; i++)
    if (mappings[i].base <= address.byte_offset && address.byte_offset < mappings[i].base + mappings[i].byte_length) {
      found = true;
      end = mappings[i].base + mappings[i].byte_length;
    }
  free(mappings);
  if (!found) qa_range_error("Native game pointer is not mapped");
  *out = bytes_at(native, address, (int64_t)(end - address.byte_offset));
  return true;
}
static qa_qvm_game_region native_span(void *ctx, int64_t word, int64_t length, int64_t relative_offset) {
  qa_native_game_memory *native = ctx;
  const qa_guest_core_address base = required(native, word);
  return bytes_at(native, relative_offset == 0 ? base : qa_sparse_offset(native->memory, base, relative_offset), length);
}
static void native_view(void *ctx, int64_t word, int64_t length, int64_t relative_offset, qa_qvm_writable_view *out) {
  qa_native_game_memory *native = ctx;
  const qa_guest_core_address base = required(native, word);
  const qa_guest_core_address address = relative_offset == 0 ? base : qa_sparse_offset(native->memory, base, relative_offset);
  native_view_at(native, (int64_t)address.byte_offset, length, out);
}
static char *native_read_string(void *ctx, int64_t word) {
  qa_native_game_memory *native = ctx;
  qa_guest_core_address address;
  if (!at(native, word, &address)) qa_range_error("Native game string requires a nonnull pointer");
  return qa_read_string(&native->memory->base, NULL, address, 1048576);
}
/* Q_strncpyz into guest memory: at most capacity - 1 characters and a terminator, the rest of the capacity zeroed. */
static void native_write_string(void *ctx, int64_t word, const char *text, int64_t capacity) {
  qa_native_game_memory *native = ctx;
  qa_guest_core_address address;
  if (!at(native, word, &address)) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: NULL dest");
  if (capacity < 1) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: destsize < 1");
  const qa_qvm_game_region bytes = bytes_at(native, address, capacity);
  writable(&bytes);
  const size_t length = strlen(text);
  const size_t end = length < (size_t)(capacity - 1) ? length : (size_t)(capacity - 1);
  memset(bytes.bytes, 0, bytes.length);
  memcpy(bytes.bytes, text, end);
}
static void native_region_view(void *ctx, const qa_qvm_game_region *region, int64_t offset, int64_t length,
                               qa_qvm_writable_view *out) {
  (void)ctx;
  if (length < 0) length = (int64_t)region->length - offset;
  if (offset < 0 || length < 0 || offset + length > (int64_t)region->length)
    qa_range_error("Native game region view exceeds its region");
  region_view_of(region, offset, length, out);
}
static void native_region_write(void *ctx, const qa_qvm_game_region *region, const uint8_t *bytes, size_t length) {
  (void)ctx;
  writable(region);
  /* Uint8Array.set rejects a source longer than the region. */
  if (length > region->length) qa_range_error("Source is too large");
  memmove(region->bytes, bytes, length);
}
static void native_region_fill(void *ctx, const qa_qvm_game_region *region, int64_t start, int64_t length, int32_t value) {
  (void)ctx;
  if (start < 0 || length < 0 || start + length > (int64_t)region->length)
    qa_range_error("Native game region fill exceeds its region");
  writable(region);
  memset(region->bytes + start, value & 255, (size_t)length);
}

static const qa_qvm_game_memory_ops native_ops = {
    native_pointer, native_span, native_view, native_read_string, native_write_string, native_address,
    native_pointer_word, native_view_at, native_region_view, native_region_write, native_region_fill,
};
qa_qvm_game_memory qa_native_game_memory_game_memory(qa_native_game_memory *native) {
  qa_qvm_game_memory memory = {&native_ops, native};
  return memory;
}
