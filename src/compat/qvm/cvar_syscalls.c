/* Port of src/compat/qvm/cvar-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/cvar_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static int32_t words_i32(const uint8_t *words, size_t length, int64_t offset) {
  if (offset < 0 || offset > (int64_t)length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
                  ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
  return (int32_t)bits;
}

static float words_f32(const uint8_t *words, size_t length, int64_t offset) {
  int32_t bits = words_i32(words, length, offset);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

static char *read_string(const qa_qvm_game_memory *memory, int32_t word) {
  char *text = qa_qvm_game_memory_read_string(memory, word);
  if (text == NULL) qa_range_error("out of memory");
  return text;
}

static bool pointer_is_null(const qa_qvm_game_memory *memory, int32_t word) {
  qa_qvm_game_region region;
  return !qa_qvm_game_memory_pointer(memory, word, &region);
}

static void update(int32_t pointer, const qa_qvm_game_memory *memory, const qa_qvm_cvar_table *cvars) {
  qa_qvm_writable_view record;
  qa_qvm_game_memory_view(memory, pointer, 272, 0, &record);
  qa_cvar_snapshot source;
  if (!cvars->read_vm(cvars->ctx, qa_qvm_writable_view_get_int32(&record, 0, true), &source))
    return;
  if (source.modification_count == qa_qvm_writable_view_get_int32(&record, 4, true)) {
    qa_cvar_read_free(&source);
    return;
  }
  qa_qvm_writable_view_set_int32(&record, 4, qa_int32(source.modification_count), true);
  size_t value_length = strlen(source.value);
  if (value_length > 255) {
    qa_cvar_read_free(&source);
    qa_range_error("Cvar_Update: value exceeds MAX_CVAR_VALUE_STRING");
  }
  const qa_qvm_game_region bytes = qa_qvm_game_memory_span(memory, pointer, 272, 0);
  qa_qvm_game_memory_region_fill(memory, &bytes, 16, 256, 0);
  for (size_t i = 0; i < value_length; i++)
    qa_qvm_writable_view_set_uint8(&record, 16 + (int64_t)i, (uint8_t)source.value[i]);
  qa_qvm_writable_view_set_float32(&record, 8, source.numeric_value, true);
  qa_qvm_writable_view_set_int32(&record, 12, qa_int32(source.integer_value), true);
  qa_cvar_read_free(&source);
}

static void register_cvar(const uint8_t *words, size_t length, const qa_qvm_game_memory *memory,
                          const qa_qvm_cvar_table *cvars) {
  int32_t pointer = words_i32(words, length, 4);
  char *name = read_string(memory, words_i32(words, length, 8));
  char *fallback = read_string(memory, words_i32(words, length, 12));
  double handle = cvars->bind_vm(cvars->ctx, name, fallback, words_i32(words, length, 16));
  free(name);
  free(fallback);
  if (pointer_is_null(memory, pointer)) return;
  qa_qvm_writable_view record;
  qa_qvm_game_memory_view(memory, pointer, 272, 0, &record);
  qa_qvm_writable_view_set_int32(&record, 0, qa_int32(handle), true);
  qa_qvm_writable_view_set_int32(&record, 4, -1, true);
  update(pointer, memory, cvars);
}

static void set_cvar(const uint8_t *words, size_t length, const qa_qvm_game_memory *memory,
                     const qa_qvm_cvar_table *cvars) {
  char *name = read_string(memory, words_i32(words, length, 4));
  int32_t pointer = words_i32(words, length, 8);
  qa_cvar_snapshot snapshot;
  if (pointer_is_null(memory, pointer)) {
    if (cvars->reset(cvars->ctx, name, true, &snapshot)) qa_cvar_read_free(&snapshot);
  } else {
    char *value = read_string(memory, pointer);
    if (cvars->set(cvars->ctx, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
    free(value);
  }
  free(name);
}

static void variable_string(const uint8_t *words, size_t length, const qa_qvm_game_memory *memory,
                            const qa_qvm_cvar_table *cvars) {
  char *name = read_string(memory, words_i32(words, length, 4));
  qa_cvar_snapshot source;
  bool found = cvars->get(cvars->ctx, name, &source);
  free(name);
  int32_t pointer = words_i32(words, length, 8);
  if (!found) {
    qa_qvm_writable_view view;
    qa_qvm_game_memory_view(memory, pointer, 1, 0, &view);
    qa_qvm_writable_view_set_uint8(&view, 0, 0);
  } else {
    qa_qvm_game_memory_write_string(memory, pointer, source.value, words_i32(words, length, 12));
    qa_cvar_read_free(&source);
  }
}

static bool registry_get(void *ctx, const char *name, qa_cvar_snapshot *out) {
  return qa_cvar_registry_get(ctx, name, out);
}
static bool registry_set(void *ctx, const char *name, const char *value, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set(ctx, name, value, force, out);
}
static bool registry_set_value(void *ctx, const char *name, double value, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set_value(ctx, name, value, out);
}
static bool registry_reset(void *ctx, const char *name, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_reset(ctx, name, force, out);
}
static bool registry_register(void *ctx, const char *name, const char *value, bool has_flags, double flags,
                              qa_cvar_snapshot *out) {
  return qa_cvar_registry_register(ctx, name, value, has_flags ? flags : 0, out);
}
static double registry_bind_vm(void *ctx, const char *name, const char *value, double flags) {
  return (double)qa_cvar_registry_bind_vm(ctx, name, value, flags);
}
static bool registry_read_vm(void *ctx, double handle, qa_cvar_snapshot *out) {
  return qa_cvar_registry_read_vm(ctx, handle, out);
}
static char *registry_info_string(void *ctx, double flags, double maximum_length) {
  return qa_cvar_registry_info_string(ctx, flags, maximum_length < 0 ? -1 : (int64_t)maximum_length);
}

qa_qvm_cvar_table qa_qvm_registry_cvar_table(qa_cvar_registry *cvars) {
  qa_qvm_cvar_table table = {cvars, registry_get, registry_set, registry_set_value, registry_reset,
                             registry_register, registry_bind_vm, registry_read_vm, registry_info_string};
  return table;
}

bool qa_qvm_cvar_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                         const qa_qvm_game_memory *memory, qa_qvm_cvar_services *registry,
                         qa_qvm_host_result *out) {
  qa_qvm_cvar_table table = qa_qvm_registry_cvar_table(registry);
  return qa_qvm_cvar_table_syscall(role, words, words_length, memory, &table, out);
}

bool qa_qvm_cvar_table_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                               const qa_qvm_game_memory *memory, const qa_qvm_cvar_table *cvars,
                               qa_qvm_host_result *out) {
  int32_t trap = words_i32(words, words_length, 0);
  qa_cvar_snapshot snapshot;
  if (strcmp(role, "ui") != 0) {
    switch (trap) {
      case 3: register_cvar(words, words_length, memory, cvars); *out = 0; return true;
      case 4: update(words_i32(words, words_length, 4), memory, cvars); *out = 0; return true;
      case 5: set_cvar(words, words_length, memory, cvars); *out = 0; return true;
      case 6: {
        if (strcmp(role, "cgame") == 0) {
          variable_string(words, words_length, memory, cvars);
          *out = 0;
          return true;
        }
        char *name = read_string(memory, words_i32(words, words_length, 4));
        bool found = cvars->get(cvars->ctx, name, &snapshot);
        free(name);
        if (!found) {
          *out = 0;
          return true;
        }
        *out = qa_int32(snapshot.integer_value);
        qa_cvar_read_free(&snapshot);
        return true;
      }
      case 7:
        if (strcmp(role, "cgame") == 0) return false;
        variable_string(words, words_length, memory, cvars);
        *out = 0;
        return true;
      default: return false;
    }
  }
  switch (trap) {
    case 3: set_cvar(words, words_length, memory, cvars); *out = 0; return true;
    case 4: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      bool found = cvars->get(cvars->ctx, name, &snapshot);
      free(name);
      double value = found ? snapshot.numeric_value : 0;
      if (found) qa_cvar_read_free(&snapshot);
      *out = (int32_t)qa_float32_to_bits(value);
      return true;
    }
    case 5: variable_string(words, words_length, memory, cvars); *out = 0; return true;
    case 6: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      if (cvars->set_value(cvars->ctx, name, words_f32(words, words_length, 8), &snapshot))
        qa_cvar_read_free(&snapshot);
      free(name);
      *out = 0;
      return true;
    }
    case 7: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      if (cvars->reset(cvars->ctx, name, false, &snapshot)) qa_cvar_read_free(&snapshot);
      free(name);
      *out = 0;
      return true;
    }
    case 8: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      char *fallback = read_string(memory, words_i32(words, words_length, 8));
      if (cvars->register_cvar(cvars->ctx, name, fallback, true, words_i32(words, words_length, 12),
                               &snapshot))
        qa_cvar_read_free(&snapshot);
      free(name);
      free(fallback);
      *out = 0;
      return true;
    }
    case 9: {
      int32_t flags = words_i32(words, words_length, 4);
      int32_t destination = words_i32(words, words_length, 8);
      int32_t capacity = words_i32(words, words_length, 12);
      char *info = cvars->info_string(cvars->ctx, flags, -1);
      if (info == NULL) qa_range_error("out of memory");
      qa_qvm_game_memory_write_string(memory, destination, info, capacity);
      free(info);
      *out = 0;
      return true;
    }
    case 50: register_cvar(words, words_length, memory, cvars); *out = 0; return true;
    case 51: update(words_i32(words, words_length, 4), memory, cvars); *out = 0; return true;
    default: return false;
  }
}
