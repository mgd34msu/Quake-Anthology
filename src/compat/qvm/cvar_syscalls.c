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

static char *read_string(qa_qvm_memory *memory, int32_t word) {
  char *text = qa_qvm_memory_read_string(memory, word);
  if (text == NULL) qa_range_error("out of memory");
  return text;
}

static bool pointer_is_null(qa_qvm_memory *memory, int32_t word) {
  qa_qvm_memory_span span;
  return !qa_qvm_memory_pointer(memory, word, &span);
}

static void update(int32_t pointer, qa_qvm_memory *memory, qa_qvm_cvar_services *cvars) {
  qa_qvm_writable_view record;
  qa_qvm_memory_view(memory, pointer, 272, 0, &record);
  qa_cvar_snapshot source;
  if (!qa_cvar_registry_read_vm(cvars, qa_qvm_writable_view_get_int32(&record, 0, true), &source))
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
  qa_qvm_memory_fill_bytes(memory, record.start + 16, 256, 0);
  for (size_t i = 0; i < value_length; i++)
    qa_qvm_writable_view_set_uint8(&record, 16 + (int64_t)i, (uint8_t)source.value[i]);
  qa_qvm_writable_view_set_float32(&record, 8, source.numeric_value, true);
  qa_qvm_writable_view_set_int32(&record, 12, qa_int32(source.integer_value), true);
  qa_cvar_read_free(&source);
}

static void register_cvar(const uint8_t *words, size_t length, qa_qvm_memory *memory,
                          qa_qvm_cvar_services *cvars) {
  int32_t pointer = words_i32(words, length, 4);
  char *name = read_string(memory, words_i32(words, length, 8));
  char *fallback = read_string(memory, words_i32(words, length, 12));
  int64_t handle = qa_cvar_registry_bind_vm(cvars, name, fallback, words_i32(words, length, 16));
  free(name);
  free(fallback);
  if (pointer_is_null(memory, pointer)) return;
  qa_qvm_writable_view record;
  qa_qvm_memory_view(memory, pointer, 272, 0, &record);
  qa_qvm_writable_view_set_int32(&record, 0, (int32_t)handle, true);
  qa_qvm_writable_view_set_int32(&record, 4, -1, true);
  update(pointer, memory, cvars);
}

static void set_cvar(const uint8_t *words, size_t length, qa_qvm_memory *memory,
                     qa_qvm_cvar_services *cvars) {
  char *name = read_string(memory, words_i32(words, length, 4));
  int32_t pointer = words_i32(words, length, 8);
  qa_cvar_snapshot snapshot;
  if (pointer_is_null(memory, pointer)) {
    if (qa_cvar_registry_reset(cvars, name, true, &snapshot)) qa_cvar_read_free(&snapshot);
  } else {
    char *value = read_string(memory, pointer);
    if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
    free(value);
  }
  free(name);
}

static void variable_string(const uint8_t *words, size_t length, qa_qvm_memory *memory,
                            qa_qvm_cvar_services *cvars) {
  char *name = read_string(memory, words_i32(words, length, 4));
  qa_cvar_snapshot source;
  bool found = qa_cvar_registry_get(cvars, name, &source);
  free(name);
  int32_t pointer = words_i32(words, length, 8);
  if (!found) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(memory, pointer, 1, 0, &view);
    qa_qvm_writable_view_set_uint8(&view, 0, 0);
  } else {
    qa_qvm_memory_write_string(memory, pointer, source.value, words_i32(words, length, 12));
    qa_cvar_read_free(&source);
  }
}

bool qa_qvm_cvar_syscall(qa_qvm_role role, const uint8_t *words, size_t words_length,
                         qa_qvm_memory *memory, qa_qvm_cvar_services *cvars,
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
        bool found = qa_cvar_registry_get(cvars, name, &snapshot);
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
      bool found = qa_cvar_registry_get(cvars, name, &snapshot);
      free(name);
      double value = found ? snapshot.numeric_value : 0;
      if (found) qa_cvar_read_free(&snapshot);
      *out = (int32_t)qa_float32_to_bits(value);
      return true;
    }
    case 5: variable_string(words, words_length, memory, cvars); *out = 0; return true;
    case 6: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      if (qa_cvar_registry_set_value(cvars, name, words_f32(words, words_length, 8), &snapshot))
        qa_cvar_read_free(&snapshot);
      free(name);
      *out = 0;
      return true;
    }
    case 7: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      if (qa_cvar_registry_reset(cvars, name, false, &snapshot)) qa_cvar_read_free(&snapshot);
      free(name);
      *out = 0;
      return true;
    }
    case 8: {
      char *name = read_string(memory, words_i32(words, words_length, 4));
      char *fallback = read_string(memory, words_i32(words, words_length, 8));
      if (qa_cvar_registry_register(cvars, name, fallback, words_i32(words, words_length, 12),
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
      char *info = qa_cvar_registry_info_string(cvars, flags, -1);
      if (info == NULL) qa_range_error("out of memory");
      qa_qvm_memory_write_string(memory, destination, info, capacity);
      free(info);
      *out = 0;
      return true;
    }
    case 50: register_cvar(words, words_length, memory, cvars); *out = 0; return true;
    case 51: update(words_i32(words, words_length, 4), memory, cvars); *out = 0; return true;
    default: return false;
  }
}
