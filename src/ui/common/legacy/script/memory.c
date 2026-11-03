/* See include/qa/ui/common/legacy/script/memory.h. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/memory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/token_memory.h"

enum {
  OFF_FILENAME = 0,
  OFF_BUFFER = 1024,
  OFF_SCRIPT_POINTER = 1028,
  OFF_END_POINTER = 1032,
  OFF_LAST_SCRIPT_POINTER = 1036,
  OFF_WHITESPACE_POINTER = 1040,
  OFF_END_WHITESPACE_POINTER = 1044,
  OFF_LENGTH = 1048,
  OFF_LINE = 1052,
  OFF_LAST_LINE = 1056,
  OFF_TOKEN_AVAILABLE = 1060,
  OFF_FLAGS = 1064,
  OFF_PUNCTUATIONS = 1068,
  OFF_PUNCTUATION_TABLE = 1072,
  OFF_TOKEN = 1076,
  OFF_NEXT_SCRIPT = 2144,
  FILENAME_BYTES = 1024
};

struct qa_ui_source_script_storage {
  const qa_ui_script_memory *memory; /* borrowed vtable; may be NULL */
  qa_ui_script_allocation allocation;
  qa_ui_script_allocation punctuation_table;
  bool has_punctuation_table;
  bool disposed;
  qa_ui_source_token_memory *token;
};

bool qa_ui_script_memory_allocate(const qa_ui_script_memory *memory,
                                  size_t size, bool clear,
                                  qa_ui_script_allocation *out) {
  memset(out, 0, sizeof *out);
  if (memory != NULL) return memory->allocate(memory->ctx, size, clear, out);
  out->bytes = clear ? calloc(size == 0 ? 1 : size, 1)
                     : malloc(size == 0 ? 1 : size);
  if (out->bytes == NULL) return false;
  out->length = size;
  return true;
}

void qa_ui_script_memory_free(const qa_ui_script_memory *memory,
                              qa_ui_script_allocation *allocation) {
  if (memory != NULL) {
    memory->free_allocation(memory->ctx, allocation);
    return;
  }
  free(allocation->bytes);
  allocation->bytes = NULL;
  allocation->length = 0;
  allocation->record = NULL;
}

static uint32_t rd_u32(const uint8_t *bytes, size_t offset) {
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof value);
  return value;
}
static int32_t rd_i32(const uint8_t *bytes, size_t offset) {
  return (int32_t)rd_u32(bytes, offset);
}
static void wr_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  memcpy(bytes + offset, &value, sizeof value);
}
static void wr_i32(uint8_t *bytes, size_t offset, int32_t value) {
  wr_u32(bytes, offset, (uint32_t)value);
}

static uint8_t *bytes_of(qa_ui_source_script_storage *script) {
  if (script->disposed) qa_ui_script_plain_error("script storage has been freed");
  return script->allocation.bytes;
}

static uint8_t *token_borrow(void *ctx, size_t *out_length) {
  qa_ui_source_script_storage *script = ctx;
  uint8_t *bytes = bytes_of(script);
  *out_length = QA_UI_SOURCE_TOKEN_BYTES;
  return bytes + OFF_TOKEN;
}

static qa_ui_source_script_storage *storage_new(
    const qa_ui_script_memory *memory,
    const qa_ui_script_allocation *allocation) {
  qa_ui_source_script_storage *script = calloc(1, sizeof *script);
  if (script == NULL) return NULL;
  script->memory = memory;
  script->allocation = *allocation;
  script->token = qa_ui_source_token_memory_create(token_borrow, script);
  if (script->token == NULL) {
    free(script);
    return NULL;
  }
  return script;
}

qa_ui_source_script_storage *qa_ui_source_script_allocate(
    int64_t length, const char *path, const qa_ui_script_memory *memory) {
  if (length < 0 || length > 0x7fffffff - QA_UI_SOURCE_SCRIPT_BYTES - 1)
    qa_range_error("script allocation must fit its nonnegative source signed size");
  qa_ui_script_allocation allocation;
  size_t total = (size_t)QA_UI_SOURCE_SCRIPT_BYTES + (size_t)length + 1;
  if (!qa_ui_script_memory_allocate(memory, total, true, &allocation))
    return NULL;
  qa_ui_source_script_storage *script = storage_new(memory, &allocation);
  if (script == NULL) {
    qa_ui_script_memory_free(memory, &allocation);
    return NULL;
  }
  uint8_t *bytes = allocation.bytes;
  memset(bytes, 0, QA_UI_SOURCE_SCRIPT_BYTES);
  size_t path_length = strlen(path);
  for (size_t index = 0;; index++) {
    unsigned int character =
        index < path_length ? (uint8_t)path[index] : 0;
    if (index >= FILENAME_BYTES)
      qa_range_error("LoadScript filename exceeds its 1024-byte source allocation");
    bytes[index] = (uint8_t)character;
    if (character == 0) break;
  }
  wr_u32(bytes, OFF_BUFFER, QA_UI_SOURCE_SCRIPT_BYTES);
  bytes[QA_UI_SOURCE_SCRIPT_BYTES + (size_t)length] = 0;
  wr_i32(bytes, OFF_LENGTH, (int32_t)length);
  wr_u32(bytes, OFF_SCRIPT_POINTER, QA_UI_SOURCE_SCRIPT_BYTES);
  wr_u32(bytes, OFF_LAST_SCRIPT_POINTER, QA_UI_SOURCE_SCRIPT_BYTES);
  wr_u32(bytes, OFF_END_POINTER,
         (uint32_t)(QA_UI_SOURCE_SCRIPT_BYTES + length));
  wr_i32(bytes, OFF_TOKEN_AVAILABLE, 0);
  wr_i32(bytes, OFF_LINE, 1);
  wr_i32(bytes, OFF_LAST_LINE, 1);
  return script;
}

qa_save_value *qa_ui_source_script_capture_save_state(
    qa_ui_source_script_storage *script,
    const qa_ui_script_memory_capture *capture) {
  if (script->disposed)
    qa_ui_script_plain_error("Cannot checkpoint disposed script storage");
  qa_save_value *object = qa_save_value_object();
  qa_save_value *token = qa_ui_source_token_capture_save_state(script->token);
  if (object == NULL || token == NULL) goto oom;
  if (!qa_save_value_set(
          object, "allocation",
          qa_save_value_number((double)capture->reference(
              capture->ctx, &script->allocation))))
    goto oom;
  if (script->has_punctuation_table) {
    if (!qa_save_value_set(
            object, "punctuationTable",
            qa_save_value_number((double)capture->reference(
                capture->ctx, &script->punctuation_table))))
      goto oom;
  } else if (!qa_save_value_set(object, "punctuationTable",
                                qa_save_value_null())) {
    goto oom;
  }
  if (!qa_save_value_set(object, "token", token)) {
    qa_save_value_free(token);
    goto oom;
  }
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(token);
  return NULL;
}

qa_ui_source_script_storage *qa_ui_source_script_restore_save_state(
    const qa_save_value *value, const qa_ui_script_memory *memory,
    const qa_ui_script_memory_restore *restore) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "script.storage")) return NULL;
  qa_save_reader field;
  if (!qa_save_reader_field(&reader, "allocation", &field)) {
    qa_save_reader_destroy(&reader);
    return NULL;
  }
  qa_ui_script_allocation allocation =
      restore->allocation(restore->ctx,
                          (int64_t)qa_save_reader_integer_min(&field, 0));
  qa_save_reader_destroy(&field);
  if (allocation.length < (size_t)QA_UI_SOURCE_SCRIPT_BYTES + 1)
    qa_save_reader_fail(&reader, "invalid script storage extent");
  qa_ui_source_script_storage *script = storage_new(memory, &allocation);
  if (script == NULL) {
    qa_save_reader_destroy(&reader);
    return NULL;
  }
  if (!qa_save_reader_field(&reader, "punctuationTable", &field)) goto oom;
  if (!qa_save_reader_is_null(&field)) {
    script->punctuation_table = restore->allocation(
        restore->ctx, (int64_t)qa_save_reader_integer_min(&field, 0));
    script->has_punctuation_table = true;
  }
  qa_save_reader_destroy(&field);
  if (script->has_punctuation_table
      && script->punctuation_table.length
             != QA_UI_SOURCE_PUNCTUATION_TABLE_BYTES)
    qa_save_reader_fail(&reader, "invalid punctuation table extent");
  if (!qa_save_reader_field(&reader, "token", &field)) goto oom;
  qa_ui_source_token_restore_save_state(script->token, field.value, true);
  qa_save_reader_destroy(&field);
  /* TS touches buffer and path getters for validation. */
  (void)qa_ui_source_script_buffer(script, &(size_t){0});
  {
    char *path = qa_ui_source_script_path(script);
    if (path == NULL) goto oom;
    free(path);
  }
  qa_save_reader_destroy(&reader);
  return script;
oom:
  qa_save_reader_destroy(&reader);
  qa_ui_source_script_destroy(script);
  return NULL;
}

void qa_ui_source_script_set_default_punctuations(
    qa_ui_source_script_storage *script, const uint32_t *heads,
    size_t count) {
  if (count != 256)
    qa_range_error("script punctuation table requires 256 heads");
  uint8_t *bytes = bytes_of(script);
  if (rd_u32(bytes, OFF_PUNCTUATION_TABLE) == 0) {
    if (!qa_ui_script_memory_allocate(
            script->memory, QA_UI_SOURCE_PUNCTUATION_TABLE_BYTES, false,
            &script->punctuation_table))
      qa_range_error("script punctuation table allocation failed");
    script->has_punctuation_table = true;
    wr_u32(bytes, OFF_PUNCTUATION_TABLE, 1);
  }
  if (rd_u32(bytes, OFF_PUNCTUATION_TABLE) != 1
      || !script->has_punctuation_table)
    qa_range_error("script punctuation pointer does not identify its table allocation");
  uint8_t *table = script->punctuation_table.bytes;
  memset(table, 0, QA_UI_SOURCE_PUNCTUATION_TABLE_BYTES);
  for (size_t index = 0; index < count; index++)
    wr_u32(table, index * 4, heads[index]);
  wr_u32(bytes, OFF_PUNCTUATIONS, 1);
}

char *qa_ui_source_script_path(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  for (size_t index = 0; index < FILENAME_BYTES; index++) {
    if (index >= script->allocation.length)
      qa_range_error("script filename is outside its allocation");
    if (bytes[index] == 0) {
      char *result = malloc(index + 1);
      if (result == NULL) return NULL;
      memcpy(result, bytes, index);
      result[index] = '\0';
      return result;
    }
  }
  qa_range_error("script filename lacks its source terminator");
}

uint8_t *qa_ui_source_script_buffer(qa_ui_source_script_storage *script,
                                    size_t *out_length) {
  uint8_t *bytes = bytes_of(script);
  uint32_t start = rd_u32(bytes, OFF_BUFFER);
  uint32_t end = rd_u32(bytes, OFF_END_POINTER);
  if (start != QA_UI_SOURCE_SCRIPT_BYTES || end < start
      || end >= script->allocation.length)
    qa_range_error("script buffer pointers are outside their allocation");
  if (out_length != NULL) *out_length = end - start;
  return bytes + start;
}

char *qa_ui_source_script_text(qa_ui_source_script_storage *script) {
  size_t length = 0;
  uint8_t *buffer = qa_ui_source_script_buffer(script, &length);
  size_t count = 0;
  while (count < length && buffer[count] != 0) count++;
  char *result = malloc(count + 1);
  if (result == NULL) return NULL;
  memcpy(result, buffer, count);
  result[count] = '\0';
  return result;
}

const qa_ui_script_memory *qa_ui_source_script_memory(
    const qa_ui_source_script_storage *script) {
  return script->memory;
}
qa_ui_source_token_memory *qa_ui_source_script_token(
    qa_ui_source_script_storage *script) {
  return script->token;
}
uint8_t *qa_ui_source_script_bytes(qa_ui_source_script_storage *script,
                                   size_t *out_length) {
  uint8_t *bytes = bytes_of(script);
  if (out_length != NULL) *out_length = script->allocation.length;
  return bytes;
}

int32_t qa_ui_source_script_length(qa_ui_source_script_storage *script) {
  return rd_i32(bytes_of(script), OFF_LENGTH);
}
uint32_t qa_ui_source_script_next_script(qa_ui_source_script_storage *script) {
  return rd_u32(bytes_of(script), OFF_NEXT_SCRIPT);
}
void qa_ui_source_script_set_next_script(qa_ui_source_script_storage *script,
                                         uint32_t value) {
  wr_u32(bytes_of(script), OFF_NEXT_SCRIPT, value);
}
int64_t qa_ui_source_script_offset(qa_ui_source_script_storage *script) {
  return (int64_t)rd_u32(bytes_of(script), OFF_SCRIPT_POINTER)
         - QA_UI_SOURCE_SCRIPT_BYTES;
}
void qa_ui_source_script_set_offset(qa_ui_source_script_storage *script,
                                    int64_t value) {
  wr_u32(bytes_of(script), OFF_SCRIPT_POINTER,
         (uint32_t)(QA_UI_SOURCE_SCRIPT_BYTES + value));
}
int64_t qa_ui_source_script_last_offset(qa_ui_source_script_storage *script) {
  return (int64_t)rd_u32(bytes_of(script), OFF_LAST_SCRIPT_POINTER)
         - QA_UI_SOURCE_SCRIPT_BYTES;
}
int64_t qa_ui_source_script_lines_crossed(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  return rd_i32(bytes, OFF_LINE) - rd_i32(bytes, OFF_LAST_LINE);
}
int64_t qa_ui_source_script_next_whitespace_char(
    qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  uint32_t pointer = rd_u32(bytes, OFF_WHITESPACE_POINTER);
  if (pointer == rd_u32(bytes, OFF_END_WHITESPACE_POINTER)) return 0;
  if (pointer >= script->allocation.length
      || pointer < QA_UI_SOURCE_SCRIPT_BYTES)
    qa_range_error("script whitespace pointer exceeds its source allocation");
  uint8_t byte = bytes[pointer];
  wr_u32(bytes, OFF_WHITESPACE_POINTER, pointer + 1);
  return byte < 128 ? byte : byte - 256;
}
int32_t qa_ui_source_script_line(qa_ui_source_script_storage *script) {
  return rd_i32(bytes_of(script), OFF_LINE);
}
void qa_ui_source_script_set_line(qa_ui_source_script_storage *script,
                                  int32_t value) {
  wr_i32(bytes_of(script), OFF_LINE, value);
}
int32_t qa_ui_source_script_flags(qa_ui_source_script_storage *script) {
  return rd_i32(bytes_of(script), OFF_FLAGS);
}
void qa_ui_source_script_set_flags(qa_ui_source_script_storage *script,
                                   int32_t value) {
  wr_i32(bytes_of(script), OFF_FLAGS, value);
}
bool qa_ui_source_script_token_available(qa_ui_source_script_storage *script) {
  return rd_i32(bytes_of(script), OFF_TOKEN_AVAILABLE) != 0;
}
void qa_ui_source_script_set_token_available(
    qa_ui_source_script_storage *script, bool value) {
  wr_i32(bytes_of(script), OFF_TOKEN_AVAILABLE, value ? 1 : 0);
}

void qa_ui_source_script_begin_token(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  uint32_t pointer = rd_u32(bytes, OFF_SCRIPT_POINTER);
  wr_u32(bytes, OFF_LAST_SCRIPT_POINTER, pointer);
  wr_i32(bytes, OFF_LAST_LINE, rd_i32(bytes, OFF_LINE));
  wr_u32(bytes, OFF_WHITESPACE_POINTER, pointer);
}
void qa_ui_source_script_end_whitespace(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  wr_u32(bytes, OFF_END_WHITESPACE_POINTER,
         rd_u32(bytes, OFF_SCRIPT_POINTER));
}

uint32_t qa_ui_source_script_punctuation_head(
    qa_ui_source_script_storage *script, int64_t character) {
  uint8_t *bytes = bytes_of(script);
  if (rd_u32(bytes, OFF_PUNCTUATIONS) != 1
      || rd_u32(bytes, OFF_PUNCTUATION_TABLE) != 1
      || !script->has_punctuation_table)
    qa_range_error("script punctuation pointer does not identify its default table");
  if (character < 0 || character > 255)
    qa_range_error("script punctuation lookup requires a source byte");
  return rd_u32(script->punctuation_table.bytes, (size_t)character * 4);
}

void qa_ui_source_script_copy_text(qa_ui_source_script_storage *script,
                                   const uint8_t *text, size_t length) {
  size_t buffer_length = 0;
  uint8_t *buffer = qa_ui_source_script_buffer(script, &buffer_length);
  if (length != buffer_length)
    qa_range_error("script text does not match its allocated length");
  memcpy(buffer, text, length);
}

void qa_ui_source_script_compress(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  size_t start = rd_u32(bytes, OFF_BUFFER);
  size_t input = start, output = start;
  bool newline = false, whitespace = false;
  size_t total = script->allocation.length;
  for (;;) {
    if (input >= total)
      qa_range_error("COM_Compress read outside its script allocation");
    uint8_t byte = bytes[input];
    if (byte == 0) break;
    if (byte == 47) {
      if (input + 1 >= total)
        qa_range_error("COM_Compress read outside its script allocation");
      if (bytes[input + 1] == 47) {
        while (input < total && bytes[input] != 0 && bytes[input] != 10)
          input++;
        continue;
      }
      if (bytes[input + 1] == 42) {
        while (input < total && bytes[input] != 0
               && !(bytes[input] == 42 && input + 1 < total
                    && bytes[input + 1] == 47))
          input++;
        if (input < total && bytes[input] != 0) input += 2;
        continue;
      }
    }
    if (byte == 10 || byte == 13) {
      newline = true;
      input++;
    } else if (byte == 32 || byte == 9) {
      whitespace = true;
      input++;
    } else {
      if (newline) {
        bytes[output++] = 10;
        newline = false;
        whitespace = false;
      }
      if (whitespace) {
        bytes[output++] = 32;
        whitespace = false;
      }
      bytes[output++] = bytes[input++];
      if (byte == 34) {
        while (input < total && bytes[input] != 0 && bytes[input] != 34)
          bytes[output++] = bytes[input++];
        if (input < total && bytes[input] == 34)
          bytes[output++] = bytes[input++];
      }
    }
  }
  if (output >= total)
    qa_range_error("COM_Compress read outside its script allocation");
  bytes[output] = 0;
  wr_i32(bytes, OFF_LENGTH, (int32_t)(output - start));
}

void qa_ui_source_script_reset(qa_ui_source_script_storage *script) {
  uint8_t *bytes = bytes_of(script);
  uint32_t pointer = rd_u32(bytes, OFF_BUFFER);
  wr_u32(bytes, OFF_SCRIPT_POINTER, pointer);
  wr_u32(bytes, OFF_LAST_SCRIPT_POINTER, pointer);
  wr_u32(bytes, OFF_WHITESPACE_POINTER, 0);
  wr_u32(bytes, OFF_END_WHITESPACE_POINTER, 0);
  wr_i32(bytes, OFF_TOKEN_AVAILABLE, 0);
  wr_i32(bytes, OFF_LINE, 1);
  wr_i32(bytes, OFF_LAST_LINE, 1);
  qa_ui_source_token_clear(script->token);
}

void qa_ui_source_script_dispose(qa_ui_source_script_storage *script) {
  if (script->disposed) return;
  uint32_t pointer = rd_u32(script->allocation.bytes, OFF_PUNCTUATION_TABLE);
  if (pointer != 0) {
    if (pointer != 1 || !script->has_punctuation_table)
      qa_range_error("script punctuation pointer does not identify its table allocation");
    qa_ui_script_memory_free(script->memory, &script->punctuation_table);
    script->has_punctuation_table = false;
  }
  qa_ui_script_memory_free(script->memory, &script->allocation);
  script->disposed = true;
}

void qa_ui_source_script_destroy(qa_ui_source_script_storage *script) {
  if (script == NULL) return;
  if (!script->disposed) qa_ui_source_script_dispose(script);
  qa_ui_source_token_memory_destroy(script->token);
  free(script);
}

uint8_t *qa_ui_compress_script_text(const uint8_t *text, size_t length,
                                    size_t *out_length) {
  /* Output never exceeds the input: each emitted separator replaces at least
     one consumed byte. */
  uint8_t *output = malloc(length + 1);
  if (output == NULL) return NULL;
  size_t o = 0, index = 0;
  bool newline = false, whitespace = false;
#define AT(offset) ((offset) < length ? text[offset] : 0)
  uint8_t byte;
  while ((byte = AT(index)) != 0) {
    if (byte == 47 && AT(index + 1) == 47) {
      while ((byte = AT(index)) != 0 && byte != 10) index++;
    } else if (byte == 47 && AT(index + 1) == 42) {
      while (AT(index) != 0 && (AT(index) != 42 || AT(index + 1) != 47))
        index++;
      if (AT(index) != 0) index += 2;
    } else if (byte == 10 || byte == 13) {
      newline = true;
      index++;
    } else if (byte == 32 || byte == 9) {
      whitespace = true;
      index++;
    } else {
      if (newline) {
        output[o++] = '\n';
        newline = false;
        whitespace = false;
      }
      if (whitespace) {
        output[o++] = ' ';
        whitespace = false;
      }
      output[o++] = text[index++];
      if (byte == 34) {
        while ((byte = AT(index)) != 0 && byte != 34)
          output[o++] = text[index++];
        if (byte == 34) output[o++] = text[index++];
      }
    }
  }
#undef AT
  output[o] = 0;
  *out_length = o;
  return output;
}
