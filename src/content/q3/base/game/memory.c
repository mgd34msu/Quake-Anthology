/* Port of src/content/q3/base/game/memory.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/memory.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
void qa_q3_game_memory_init(qa_q3_game_memory *mem, void *ctx, double (*dbg)(void *), void (*print)(void *, const char *)) {
  memset(mem->pool, 0, sizeof mem->pool); mem->alloc_point = 0; mem->ctx = ctx; mem->debug_integer = dbg; mem->print = print;
}
double qa_q3_game_memory_allocated_bytes(const qa_q3_game_memory *mem) { return mem->alloc_point; }
char *qa_q3_game_memory_allocation_read_string(qa_q3_game_memory_allocation alloc) {
  size_t start = alloc.offset, cap = QA_Q3_GAME_MEMORY_BYTES; long end = -1;
  for (size_t i = start; i < cap; i++) if (alloc.pool->pool[i] == 0) { end = (long)i; break; }
  if (end < 0) qa_range_error("Game string reads beyond the source memory pool");
  size_t n = (size_t)end - start; char *text = malloc(n + 1); if (!text) return NULL;
  memcpy(text, alloc.pool->pool + start, n); text[n] = 0; return text;
}
void qa_q3_game_memory_allocation_write_string(qa_q3_game_memory_allocation alloc, const char *value) {
  size_t n = strlen(value);
  if (n + 1 > alloc.length) qa_range_error("Game string exceeds its source allocation");
  for (size_t i = 0; i < n; i++) { unsigned char b = (unsigned char)value[i]; if (b == 0) qa_range_error("Game strings require non-NUL source bytes"); alloc.bytes[i] = b; }
  alloc.bytes[n] = 0;
}
qa_q3_game_memory_pointer qa_q3_game_memory_capture_allocation(qa_q3_game_memory *mem, qa_q3_game_memory_allocation alloc) {
  if (alloc.pool != mem) qa_range_error("Game allocation belongs to another pool");
  qa_q3_game_memory_pointer p = {(double)alloc.offset, (double)alloc.length}; return p;
}
static double field_integer0(const qa_save_value *value, const char *path, const char *name) {
  qa_save_reader reader, field; if (!qa_save_reader_init(&reader, value, path)) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, name, &field)) qa_range_error("out of memory");
  double v = qa_save_reader_integer_min(&field, 0); qa_save_reader_destroy(&field); qa_save_reader_destroy(&reader); return v;
}
qa_q3_game_memory_allocation qa_q3_game_memory_restore_allocation(qa_q3_game_memory *mem, const qa_save_value *value) {
  double offset = field_integer0(value, "q3.memory.pointer", "offset"), length = field_integer0(value, "q3.memory.pointer", "length");
  if (offset > QA_Q3_GAME_MEMORY_BYTES || length > QA_Q3_GAME_MEMORY_BYTES - offset) {
    qa_save_reader r; qa_save_reader_init(&r, value, "q3.memory.pointer"); qa_save_reader_fail(&r, "allocation outside game pool");
  }
  qa_q3_game_memory_allocation a = {mem->pool + (size_t)offset, (size_t)length, mem, (size_t)offset}; return a;
}
qa_q3_game_memory_save_state qa_q3_game_memory_capture_save_state(qa_q3_game_memory *mem) {
  qa_q3_game_memory_save_state s; s.pool = malloc(QA_Q3_GAME_MEMORY_BYTES); s.pool_length = QA_Q3_GAME_MEMORY_BYTES; s.alloc_point = mem->alloc_point;
  if (s.pool) memcpy(s.pool, mem->pool, QA_Q3_GAME_MEMORY_BYTES);
  return s;
}
void qa_q3_game_memory_save_state_destroy(qa_q3_game_memory_save_state *s) { free(s->pool); s->pool = NULL; }
void qa_q3_game_memory_restore_save_state(qa_q3_game_memory *mem, const qa_save_value *value) {
  qa_save_reader reader, field; qa_save_reader_init(&reader, value, "q3.memory");
  qa_save_reader_field(&reader, "pool", &field); size_t blen = 0; const uint8_t *bytes = qa_save_reader_bytes(&field, &blen); qa_save_reader_destroy(&field);
  qa_save_reader_field(&reader, "allocPoint", &field); double point = qa_save_reader_integer_min(&field, 0); qa_save_reader_destroy(&field);
  if (blen != QA_Q3_GAME_MEMORY_BYTES || point > QA_Q3_GAME_MEMORY_BYTES || fmod(point, 32) != 0) qa_save_reader_fail(&reader, "invalid module memory extent");
  memcpy(mem->pool, bytes, QA_Q3_GAME_MEMORY_BYTES); mem->alloc_point = point; qa_save_reader_destroy(&reader);
}
qa_q3_game_memory_allocation qa_q3_game_memory_allocate(qa_q3_game_memory *mem, double size) {
  if (size != trunc(size) || size < 0 || size > 0x7fffffff) qa_range_error("G_Alloc requires a nonnegative source int size");
  double aligned = (double)(qa_js_to_int32((size + 31)) & ~31);
  if (mem->debug_integer(mem->ctx) != 0) { char buf[256]; snprintf(buf, sizeof buf, "G_Alloc of %lld bytes (%lld left)\n", (long long)size, (long long)qa_int32(QA_Q3_GAME_MEMORY_BYTES - mem->alloc_point - aligned)); mem->print(mem->ctx, buf); }
  if (mem->alloc_point + size > QA_Q3_GAME_MEMORY_BYTES) { char buf[256]; snprintf(buf, sizeof buf, "G_Alloc: failed on allocation of %lld bytes\n", (long long)size); qa_common_error(QA_COMMON_ERROR_DROP, buf); }
  qa_q3_game_memory_allocation a = {mem->pool + (size_t)mem->alloc_point, (size_t)size, mem, (size_t)mem->alloc_point};
  mem->alloc_point = qa_int32(mem->alloc_point + aligned); return a;
}
void qa_q3_game_memory_initialize(qa_q3_game_memory *mem) { mem->alloc_point = 0; }
void qa_q3_game_memory_status(qa_q3_game_memory *mem) { char buf[256]; snprintf(buf, sizeof buf, "Game memory status: %lld out of %d bytes allocated\n", (long long)mem->alloc_point, QA_Q3_GAME_MEMORY_BYTES); mem->print(mem->ctx, buf); }
