/* See include/qa/compat/qvm/memory.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/memory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

/* TS qStrncpyz over destination bytes (length = allocation remainder). */
static void q_strncpyz(uint8_t *destination, int64_t destination_length, bool has_destination,
                       const char *source, int64_t size) {
  if (!has_destination) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: NULL dest");
  if (size < 1) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: destsize < 1");
  if (size > destination_length) qa_range_error("Q_strncpyz destination exceeds its allocation");
  size_t source_length = strlen(source);
  int64_t index = 0;
  for (; index < size - 1; index++) {
    uint8_t byte = index < (int64_t)source_length ? (uint8_t)source[index] : 0;
    destination[index] = byte;
    if (byte == 0) break;
  }
  if (index < size) memset(destination + index, 0, (size_t)(size - index));
}

void qa_qvm_memory_init(qa_qvm_memory *memory, uint8_t *bytes, size_t length,
                        qa_qvm_effect_fn effect, void *effect_ctx) {
  if (length == 0 || length > 0x40000000u || (length & (length - 1)) != 0)
    qa_range_error("QVM memory allocation must be a nonzero power of two at most 2^30 bytes");
  memory->bytes = bytes;
  memory->length = length;
  qa_qvm_memory_writes_init(&memory->writes, bytes, length, effect, effect_ctx);
}

void qa_qvm_memory_destroy(qa_qvm_memory *memory) {
  qa_qvm_memory_writes_destroy(&memory->writes);
  memory->bytes = NULL;
  memory->length = 0;
}

size_t qa_qvm_memory_observe_writes(qa_qvm_memory *memory,
                                    const qa_qvm_write_range *ranges, size_t range_count,
                                    qa_qvm_publish_fn publish, void *publish_ctx,
                                    qa_qvm_publish_fn after_publication, void *after_ctx) {
  return qa_qvm_memory_writes_observe(&memory->writes, ranges, range_count, publish, publish_ctx,
                                      after_publication, after_ctx);
}

bool qa_qvm_memory_get_observes_writes(const qa_qvm_memory *memory) {
  return qa_qvm_memory_writes_get_intercepts(&memory->writes);
}

void qa_qvm_memory_assert_live(qa_qvm_memory *memory) { qa_qvm_memory_writes_assert_live(&memory->writes); }
void qa_qvm_memory_assert_not_publishing(qa_qvm_memory *memory) { qa_qvm_memory_writes_assert_not_publishing(&memory->writes); }
void qa_qvm_memory_clear_write_observers(qa_qvm_memory *memory) { qa_qvm_memory_writes_clear(&memory->writes); }
void qa_qvm_memory_close(qa_qvm_memory *memory) { qa_qvm_memory_writes_close(&memory->writes); }

void qa_qvm_memory_data_view(qa_qvm_memory *memory, int64_t byte_offset, int64_t byte_length,
                             qa_qvm_writable_view *out) {
  qa_qvm_memory_writes_range(&memory->writes, byte_offset, byte_length);
  qa_qvm_writable_view_init(out, memory->bytes, memory->length, byte_offset, byte_length, &memory->writes);
}

void qa_qvm_memory_write_bytes(qa_qvm_memory *memory, int64_t byte_offset,
                               const uint8_t *bytes, size_t length) {
  qa_qvm_memory_writes_assert_writable(&memory->writes);
  qa_qvm_memory_writes_range(&memory->writes, byte_offset, (int64_t)length);
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(&memory->writes)
      ? qa_qvm_memory_writes_before(&memory->writes, byte_offset, (int64_t)length) : NULL;
  /* memmove: TS Uint8Array.set copies as if through a snapshot when
   * the source is a subarray of this same allocation. */
  memmove(memory->bytes + byte_offset, bytes, length);
  qa_qvm_memory_writes_after(&memory->writes, capture);
}

void qa_qvm_memory_fill_bytes(qa_qvm_memory *memory, int64_t byte_offset, int64_t byte_length, int32_t value) {
  qa_qvm_memory_writes_assert_writable(&memory->writes);
  qa_qvm_memory_writes_range(&memory->writes, byte_offset, byte_length);
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(&memory->writes)
      ? qa_qvm_memory_writes_before(&memory->writes, byte_offset, byte_length) : NULL;
  memset(memory->bytes + byte_offset, value & 255, (size_t)byte_length);
  qa_qvm_memory_writes_after(&memory->writes, capture);
}

void qa_qvm_memory_copy_bytes(qa_qvm_memory *memory, int64_t destination_offset,
                              int64_t source_offset, int64_t byte_length) {
  qa_qvm_memory_writes_assert_writable(&memory->writes);
  qa_qvm_memory_writes_range(&memory->writes, destination_offset, byte_length);
  qa_qvm_memory_writes_range(&memory->writes, source_offset, byte_length);
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(&memory->writes)
      ? qa_qvm_memory_writes_before(&memory->writes, destination_offset, byte_length) : NULL;
  /* Uint8Array.copyWithin semantics == memmove. */
  memmove(memory->bytes + destination_offset, memory->bytes + source_offset, (size_t)byte_length);
  qa_qvm_memory_writes_after(&memory->writes, capture);
}

bool qa_qvm_memory_pointer(const qa_qvm_memory *memory, int64_t word, qa_qvm_memory_span *out) {
  if (word < -0x80000000LL || word > 0x7fffffffLL)
    qa_range_error("QVM pointer must be a signed 32-bit word");
  if (word == 0) return false;
  int64_t offset = word & (int64_t)(memory->length - 1);
  out->byte_offset = offset;
  out->byte_length = (int64_t)memory->length - offset;
  return true;
}

qa_qvm_memory_span qa_qvm_memory_get_span(const qa_qvm_memory *memory, int64_t word, int64_t length, int64_t relative_offset) {
  qa_qvm_memory_span pointer;
  if (!qa_qvm_memory_pointer(memory, word, &pointer))
    qa_range_error("QVM memory span requires a nonnull pointer");
  /* A start inside [0, length] needs relative_offset within the same
   * span of the masked offset; reject wild values before the addition
   * can overflow (TS rejects them via its safe-integer check). */
  if (relative_offset > (int64_t)memory->length || relative_offset < -(int64_t)memory->length)
    qa_range_error("QVM memory span exceeds allocation or has an invalid length");
  int64_t start = pointer.byte_offset + relative_offset;
  if (length < 0 || start < 0 || start > (int64_t)memory->length || length > (int64_t)memory->length - start)
    qa_range_error("QVM memory span exceeds allocation or has an invalid length");
  qa_qvm_memory_span result = {start, length};
  return result;
}

void qa_qvm_memory_view(qa_qvm_memory *memory, int64_t word, int64_t length, int64_t relative_offset,
                        qa_qvm_writable_view *out) {
  qa_qvm_memory_span span = qa_qvm_memory_get_span(memory, word, length, relative_offset);
  qa_qvm_memory_data_view(memory, span.byte_offset, span.byte_length, out);
}

char *qa_qvm_memory_read_string(const qa_qvm_memory *memory, int64_t word) {
  qa_qvm_memory_span pointer;
  if (!qa_qvm_memory_pointer(memory, word, &pointer))
    qa_range_error("QVM string requires a nonnull pointer");
  const uint8_t *start = memory->bytes + pointer.byte_offset;
  const void *terminator = memchr(start, 0, (size_t)pointer.byte_length);
  if (terminator == NULL) qa_range_error("QVM string has no terminator before the allocation ends");
  size_t count = (size_t)((const uint8_t *)terminator - start);
  char *text = malloc(count + 1);
  if (text == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(text, start, count);
  text[count] = '\0';
  return text;
}

void qa_qvm_memory_write_string(qa_qvm_memory *memory, int64_t word, const char *text, int64_t capacity) {
  qa_qvm_memory_span pointer;
  bool has = qa_qvm_memory_pointer(memory, word, &pointer);
  if (has && capacity > pointer.byte_length)
    qa_range_error("Q_strncpyz destination exceeds QVM allocation");
  if (!has) {
    q_strncpyz(NULL, 0, false, text, capacity);
    return;
  }
  int64_t offset = pointer.byte_offset;
  qa_qvm_memory_writes_assert_writable(&memory->writes);
  if (capacity < 1) {
    q_strncpyz(memory->bytes + offset, pointer.byte_length, true, text, capacity);
    return;
  }
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(&memory->writes)
      ? qa_qvm_memory_writes_before(&memory->writes, offset, capacity) : NULL;
  q_strncpyz(memory->bytes + offset, pointer.byte_length, true, text, capacity);
  qa_qvm_memory_writes_after(&memory->writes, capture);
}

void qa_qvm_memory_write_bounded_string(qa_qvm_memory *memory, int64_t word, const char *text, int64_t capacity) {
  qa_qvm_memory_span pointer;
  bool has = qa_qvm_memory_pointer(memory, word, &pointer);
  if (!has || capacity < 1 || capacity > pointer.byte_length)
    qa_range_error("Bounded string copy exceeds QVM allocation");
  qa_qvm_memory_writes_assert_writable(&memory->writes);
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(&memory->writes)
      ? qa_qvm_memory_writes_before(&memory->writes, pointer.byte_offset, capacity) : NULL;
  q_strncpyz(memory->bytes + pointer.byte_offset, pointer.byte_length, true, text, capacity);
  qa_qvm_memory_writes_after(&memory->writes, capture);
}

/* --- Q3GameMemory (TS `QvmMemory implements Q3GameMemory`) --- */
bool qa_qvm_memory_address(const qa_qvm_memory *memory, int64_t word, int64_t *out) {
  qa_qvm_memory_span pointer;
  if (!qa_qvm_memory_pointer(memory, word, &pointer)) return false;
  *out = pointer.byte_offset;
  return true;
}
int64_t qa_qvm_memory_pointer_word(const qa_qvm_memory *memory, bool has_address, int64_t address) {
  return !has_address ? 0 : address == 0 ? (int64_t)memory->length : address;
}
static int64_t region_offset(const qa_qvm_memory *memory, const qa_qvm_game_region *region) {
  return (int64_t)(region->bytes - memory->bytes);
}
static bool game_pointer(void *ctx, int64_t word, qa_qvm_game_region *out) {
  qa_qvm_memory *memory = ctx;
  qa_qvm_memory_span pointer;
  if (!qa_qvm_memory_pointer(memory, word, &pointer)) return false;
  out->bytes = memory->bytes + pointer.byte_offset;
  out->length = (size_t)pointer.byte_length;
  out->read_only = false;
  return true;
}
static qa_qvm_game_region game_span(void *ctx, int64_t word, int64_t length, int64_t relative_offset) {
  qa_qvm_memory *memory = ctx;
  const qa_qvm_memory_span span = qa_qvm_memory_get_span(memory, word, length, relative_offset);
  qa_qvm_game_region region = {memory->bytes + span.byte_offset, (size_t)span.byte_length, false};
  return region;
}
static void game_view(void *ctx, int64_t word, int64_t length, int64_t relative_offset, qa_qvm_writable_view *out) {
  qa_qvm_memory_view(ctx, word, length, relative_offset, out);
}
static char *game_read_string(void *ctx, int64_t word) { return qa_qvm_memory_read_string(ctx, word); }
static void game_write_string(void *ctx, int64_t word, const char *text, int64_t capacity) {
  qa_qvm_memory_write_string(ctx, word, text, capacity);
}
static bool game_address(void *ctx, int64_t word, int64_t *out) { return qa_qvm_memory_address(ctx, word, out); }
static int64_t game_pointer_word(void *ctx, bool has_address, int64_t address) {
  return qa_qvm_memory_pointer_word(ctx, has_address, address);
}
static void game_view_at(void *ctx, int64_t address, int64_t length, qa_qvm_writable_view *out) {
  qa_qvm_memory_data_view(ctx, address, length, out);
}
static void game_region_view(void *ctx, const qa_qvm_game_region *region, int64_t offset, int64_t length,
                             qa_qvm_writable_view *out) {
  qa_qvm_memory *memory = ctx;
  if (length < 0) length = (int64_t)region->length - offset;
  qa_qvm_memory_data_view(memory, region_offset(memory, region) + offset, length, out);
}
static void game_region_write(void *ctx, const qa_qvm_game_region *region, const uint8_t *bytes, size_t length) {
  qa_qvm_memory *memory = ctx;
  qa_qvm_memory_write_bytes(memory, region_offset(memory, region), bytes, length);
}
static void game_region_fill(void *ctx, const qa_qvm_game_region *region, int64_t start, int64_t length, int32_t value) {
  qa_qvm_memory *memory = ctx;
  qa_qvm_memory_fill_bytes(memory, region_offset(memory, region) + start, length, value);
}
static const qa_qvm_game_memory_ops qvm_game_memory_ops = {
    game_pointer, game_span, game_view, game_read_string, game_write_string, game_address,
    game_pointer_word, game_view_at, game_region_view, game_region_write, game_region_fill,
};
qa_qvm_game_memory qa_qvm_memory_game_memory(qa_qvm_memory *memory) {
  qa_qvm_game_memory result = {&qvm_game_memory_ops, memory};
  return result;
}
