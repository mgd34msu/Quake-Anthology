/*
 * Private helpers shared by the bot library translation units: DataView-style
 * little-endian cells, TS Error throws, owned diagnostic lists, the
 * BotMemory-as-ScriptMemory adapter and checkpoint value builders.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_INTERNAL_H
#define QA_BOTS_BEHAVIOR_LIBRARY_INTERNAL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/bots/behavior/library/memory.h"
#include "qa/contracts/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

static inline uint32_t lib_u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void lib_set_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
static inline int32_t lib_i32(const uint8_t *p) { return (int32_t)lib_u32(p); }
static inline void lib_set_i32(uint8_t *p, int32_t v) { lib_set_u32(p, (uint32_t)v); }
static inline float lib_f32(const uint8_t *p) {
  uint32_t bits = lib_u32(p);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}
static inline void lib_set_f32(uint8_t *p, float v) {
  uint32_t bits;
  memcpy(&bits, &v, 4);
  lib_set_u32(p, bits);
}
static inline qa_vec3 lib_vec3(const uint8_t *p) {
  return (qa_vec3){lib_f32(p), lib_f32(p + 4), lib_f32(p + 8)};
}
static inline void lib_set_vec3(uint8_t *p, qa_vec3 v) {
  lib_set_f32(p, (float)v.x);
  lib_set_f32(p + 4, (float)v.y);
  lib_set_f32(p + 8, (float)v.z);
}

static inline void *lib_alloc(size_t size) {
  void *memory = calloc(1, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("out of memory");
  return memory;
}
static inline void *lib_realloc(void *pointer, size_t size) {
  void *memory = realloc(pointer, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("out of memory");
  return memory;
}
static inline char *lib_strndup(const char *text, size_t length) {
  char *copy = lib_alloc(length + 1);
  memcpy(copy, text, length);
  return copy;
}
static inline char *lib_strdup(const char *text) { return lib_strndup(text, strlen(text)); }
__attribute__((format(printf, 1, 2))) static inline char *lib_format(const char *format, ...) {
  va_list args, copy;
  va_start(args, format);
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *text = lib_alloc((size_t)length + 1);
  vsnprintf(text, (size_t)length + 1, format, args);
  va_end(args);
  return text;
}

/* Grow-only table cell count for direct pointer-ID indexing. */
static inline size_t lib_grow(size_t current, size_t needed) {
  size_t next = current == 0 ? 16 : current;
  while (next < needed) next *= 2;
  return next;
}

/* qa_bot_memory_allocate with OOM treated as fatal. */
static inline qa_bot_memory_allocation lib_mem_alloc(qa_bot_memory *memory, int64_t size,
                                                     qa_bot_memory_kind kind, bool clear) {
  qa_bot_memory_allocation allocation;
  if (!qa_bot_memory_allocate(memory, size, kind, clear, &allocation))
    qa_range_error("out of memory");
  return allocation;
}

/* TS BotMemory used where a ScriptMemory is expected. */
static inline bool lib_script_allocate(void *ctx, size_t size, bool clear,
                                       qa_ui_script_allocation *out) {
  qa_bot_memory_allocation bot;
  if (!qa_bot_memory_allocate(ctx, (int64_t)size, QA_BOT_MEMORY_HEAP, clear, &bot)) return false;
  out->bytes = bot.bytes;
  out->length = bot.length;
  out->record = bot.record;
  return true;
}
static inline void lib_script_free(void *ctx, qa_ui_script_allocation *allocation) {
  qa_bot_memory_allocation bot = {allocation->bytes, allocation->length, allocation->record};
  qa_bot_memory_free(ctx, &bot);
}
static inline qa_ui_script_memory lib_script_memory(qa_bot_memory *memory) {
  return (qa_ui_script_memory){memory, lib_script_allocate, lib_script_free};
}

/* Owned ScriptDiagnostic list (TS ScriptDiagnostic[]). */
typedef struct lib_diagnostics {
  qa_ui_script_diagnostic *items;
  size_t count, capacity;
} lib_diagnostics;

static inline qa_ui_script_location lib_location(const char *path, int64_t line, int64_t column) {
  return (qa_ui_script_location){lib_strdup(path), line, column};
}
static inline void lib_diagnostics_push(lib_diagnostics *list, qa_ui_script_severity severity,
                                        const char *message, const char *path, int64_t line,
                                        int64_t column) {
  char * message2;
  qa_ui_script_location location;
  if (list->count == list->capacity) {
    list->capacity = lib_grow(list->capacity, list->count + 1);
    list->items = lib_realloc(list->items, list->capacity * sizeof *list->items);
  }
  list->items[list->count++] = (message2 = lib_strdup(message), location = lib_location(path, line, column), (qa_ui_script_diagnostic){
      severity, message2, location});
}
static inline void lib_diagnostics_copy(lib_diagnostics *list, const qa_ui_script_diagnostic *d) {
  lib_diagnostics_push(list, d->severity, d->message, d->location.path, d->location.line,
                       d->location.column);
}
static inline void lib_diagnostics_destroy(lib_diagnostics *list) {
  for (size_t i = 0; i < list->count; i++) qa_ui_script_diagnostic_destroy(&list->items[i]);
  free(list->items);
  *list = (lib_diagnostics){NULL, 0, 0};
}

/* Checkpoint builders. OOM has no TS counterpart and is fatal here. */
static inline qa_save_value *lib_sv(qa_save_value *value) {
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static inline void lib_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, lib_sv(child))) qa_range_error("out of memory");
}
static inline void lib_set_num(qa_save_value *object, const char *key, double value) {
  lib_set(object, key, qa_save_value_number(value));
}
static inline void lib_push(qa_save_value *array, qa_save_value *child) {
  if (!qa_save_value_push(array, lib_sv(child))) qa_range_error("out of memory");
}
static inline qa_save_value *lib_vec3_value(qa_vec3 v) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "x", v.x);
  lib_set_num(object, "y", v.y);
  lib_set_num(object, "z", v.z);
  return object;
}
static inline qa_save_value *lib_diagnostics_value(const lib_diagnostics *list) {
  qa_save_value *array = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < list->count; i++)
    lib_push(array, qa_ui_script_diagnostic_capture(&list->items[i]));
  return array;
}

/* SaveReader helpers. */
static inline void lib_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}
static inline void lib_item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out)) qa_range_error("out of memory");
}
static inline double lib_int_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader field;
  lib_field(reader, name, &field);
  double value = qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}
static inline double lib_int(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  lib_field(reader, name, &field);
  double value = qa_save_reader_integer(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static inline bool lib_read_bool(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  lib_field(reader, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static inline char *lib_read_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  lib_field(reader, name, &field);
  char *value = lib_strdup(qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  return value;
}
static inline void lib_read_diagnostics(const qa_save_reader *reader, const char *name,
                                        lib_diagnostics *out) {
  qa_save_reader list;
  lib_field(reader, name, &list);
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    lib_item(&list, i, &entry);
    qa_ui_script_diagnostic diagnostic;
    if (!qa_ui_script_read_diagnostic(&entry, &diagnostic)) qa_range_error("out of memory");
    lib_diagnostics_copy(out, &diagnostic);
    qa_ui_script_diagnostic_destroy(&diagnostic);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
}

/* TS ScriptToken text comparison (token.text === text). */
static inline bool lib_token_is(const qa_ui_script_token *token, const char *text) {
  size_t length = strlen(text);
  return token->text_length == length && memcmp(token->text, text, length) == 0;
}

/* One read through ScriptSourceReader.next with its TS source-failure catch. */
typedef struct lib_read_next {
  qa_ui_script_source_reader *source;
  qa_ui_script_token_record record;
  bool read;
} lib_read_next;
static inline void lib_run_read_next(void *ctx) {
  lib_read_next *next = ctx;
  next->read = qa_ui_script_source_reader_next(next->source, &next->record);
}
/* TS `try { return source.next()?.token } catch { if (!isSourceFailure) throw; return undefined }`.
 * NULL is undefined; otherwise the caller owns one token reference. */
static inline qa_ui_script_token *lib_source_token(qa_ui_script_source_reader *source) {
  lib_read_next next = {source, {NULL, 0, 0, 0}, false};
  bool completed = qa_ui_script_source_reader_catch(source, lib_run_read_next, &next);
  return completed && next.read ? next.record.token : NULL;
}
/* TS `position`/`currentScriptFilename` as a location with the given column; path is malloc'd. */
static inline qa_ui_script_location lib_source_location(qa_ui_script_source_reader *source,
                                                        int64_t column) {
  char filename[1024];
  int64_t line = 0;
  qa_ui_script_source_reader_position(source, filename, sizeof filename, &line);
  return lib_location(filename, line, column);
}
#endif
