/* See include/qa/compat/qvm/symbols.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/symbols.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

typedef struct record {
  char *name;   /* owned */
  uint8_t *data; /* allocator storage, 16 + strlen(name) bytes */
  size_t data_length;
} record;

static void assert_live(qa_qvm_symbols *symbols) {
  if (symbols->assert_live != NULL) symbols->assert_live(symbols->assert_live_ctx);
}

static int32_t data_i32(const uint8_t *data, size_t offset) {
  uint32_t bits = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) |
                  ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
  return (int32_t)bits;
}

static void data_set_i32(uint8_t *data, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  data[offset] = (uint8_t)bits;
  data[offset + 1] = (uint8_t)(bits >> 8);
  data[offset + 2] = (uint8_t)(bits >> 16);
  data[offset + 3] = (uint8_t)(bits >> 24);
}

static int32_t parse_hex(const char *text) {
  int32_t value = 0;
  for (const char *p = text; *p != '\0'; p++) {
    unsigned char code = (unsigned char)*p;
    if (code >= 48 && code <= 57) value = (int32_t)((uint32_t)value * 16u + (uint32_t)(code - 48));
    else if (code >= 97 && code <= 102) value = (int32_t)((uint32_t)value * 16u + (uint32_t)(code - 87));
    else if (code >= 65 && code <= 70) value = (int32_t)((uint32_t)value * 16u + (uint32_t)(code - 55));
  }
  return value;
}

void qa_qvm_symbols_init(qa_qvm_symbols *symbols, const int32_t *instruction_pointers,
                         size_t instruction_pointer_count, qa_qvm_symbol_allocate_fn allocate,
                         void *allocate_ctx, qa_qvm_assert_live_fn assert_live_fn, void *assert_live_ctx) {
  memset(symbols, 0, sizeof *symbols);
  symbols->instruction_pointers = instruction_pointers;
  symbols->instruction_pointer_count = instruction_pointer_count;
  symbols->allocate = allocate;
  symbols->allocate_ctx = allocate_ctx;
  symbols->assert_live = assert_live_fn;
  symbols->assert_live_ctx = assert_live_ctx;
}

void qa_qvm_symbols_destroy(qa_qvm_symbols *symbols) {
  record *records = symbols->records;
  for (size_t i = 0; i < symbols->record_count; i++) free(records[i].name);
  free(records);
  memset(symbols, 0, sizeof *symbols);
}

int64_t qa_qvm_symbols_get_count(qa_qvm_symbols *symbols) {
  assert_live(symbols);
  return symbols->parsed_count;
}

size_t qa_qvm_symbols_entries(qa_qvm_symbols *symbols, qa_qvm_symbol *out, size_t capacity) {
  assert_live(symbols);
  record *records = symbols->records;
  for (size_t i = 0; i < symbols->record_count && i < capacity; i++) {
    out[i].value = data_i32(records[i].data, 4);
    out[i].name = records[i].name;
    out[i].profile_count = data_i32(records[i].data, 8);
  }
  return symbols->record_count;
}

qa_qvm_function_symbol qa_qvm_symbols_value_to_function_symbol(qa_qvm_symbols *symbols, int32_t value) {
  assert_live(symbols);
  record *records = symbols->records;
  qa_qvm_function_symbol result = {0, "", NULL, 0};
  if (symbols->record_count == 0) return result;
  size_t best = 0;
  for (size_t i = 1; i < symbols->record_count; i++) {
    if (data_i32(records[i].data, 4) > value) break;
    best = i;
  }
  result.value = data_i32(records[best].data, 4);
  result.name = records[best].name;
  result.owner = symbols;
  result.index = best;
  return result;
}

int32_t qa_qvm_function_symbol_get_profile_count(qa_qvm_function_symbol *symbol) {
  if (symbol->owner == NULL) return 0;
  assert_live(symbol->owner);
  /* A reload may retire the record a running frame sampled (TS keeps it alive). */
  if (symbol->index >= symbol->owner->record_count) return 0;
  record *records = symbol->owner->records;
  return data_i32(records[symbol->index].data, 8);
}

void qa_qvm_function_symbol_set_profile_count(qa_qvm_function_symbol *symbol, int32_t count) {
  if (symbol->owner == NULL) return; /* null symbol is a plain object in TS */
  assert_live(symbol->owner);
  if (symbol->index >= symbol->owner->record_count) return;
  record *records = symbol->owner->records;
  data_set_i32(records[symbol->index].data, 8, count);
}

void qa_qvm_symbols_value_to_symbol(qa_qvm_symbols *symbols, int32_t value, char *out, size_t out_size) {
  assert_live(symbols);
  if (symbols->record_count == 0) {
    snprintf(out, out_size, "NO SYMBOLS");
    return;
  }
  qa_qvm_function_symbol symbol = qa_qvm_symbols_value_to_function_symbol(symbols, value);
  if (value == symbol.value) {
    snprintf(out, out_size, "%s", symbol.name);
    return;
  }
  char buffer[1200];
  int written = snprintf(buffer, sizeof buffer, "%s+%d", symbol.name,
                         (int)(int32_t)((uint32_t)value - (uint32_t)symbol.value));
  size_t length = written < 0 ? 0 : (size_t)written;
  if (length >= 1024 && symbols->print != NULL) {
    char message[128];
    snprintf(message, sizeof message, "Com_sprintf: overflow of %zu in 1024\n", length);
    symbols->print(symbols->print_ctx, message);
  }
  size_t keep = length < 1023 ? length : 1023;
  if (keep > out_size - 1) keep = out_size - 1;
  memcpy(out, buffer, keep);
  out[keep] = '\0';
}

int32_t qa_qvm_symbols_symbol_to_value(qa_qvm_symbols *symbols, const char *name) {
  assert_live(symbols);
  record *records = symbols->records;
  for (size_t i = 0; i < symbols->record_count; i++)
    if (strcmp(records[i].name, name) == 0) return data_i32(records[i].data, 4);
  return 0;
}

void qa_qvm_symbols_load(qa_qvm_symbols *symbols, const qa_qvm_symbol_load_options *options) {
  if (options->developer == 0) return;
  assert_live(symbols);
  symbols->print = options->print;
  symbols->print_ctx = options->print_ctx;
  char requested[1200];
  const char *dot = strchr(options->name, '.');
  size_t stem = dot == NULL ? strlen(options->name) : (size_t)(dot - options->name);
  int written = snprintf(requested, sizeof requested, "vm/%.*s.map", (int)stem, options->name);
  size_t requested_length = written < 0 ? 0 : (size_t)written;
  if (requested_length >= 64 && options->print != NULL) {
    char message[128];
    snprintf(message, sizeof message, "Com_sprintf: overflow of %zu in 64\n", requested_length);
    options->print(options->print_ctx, message);
  }
  char path[64];
  size_t path_length = requested_length < 63 ? requested_length : 63;
  memcpy(path, requested, path_length);
  path[path_length] = '\0';
  qa_qvm_symbol_file file;
  if (!options->files->read_file_retained_sync(options->files->ctx, path, &file)) {
    if (options->print != NULL) {
      char message[256];
      snprintf(message, sizeof message, "Couldn't load symbol file: %s\n", path);
      options->print(options->print_ctx, message);
    }
    return;
  }
  /* byteText stops at the first NUL. */
  size_t text_length = 0;
  while (text_length < file.terminated_length && file.terminated_bytes[text_length] != 0) text_length++;
  char *text = malloc(text_length + 1);
  if (text == NULL) qa_range_error("QVM memory allocation failed");
  memcpy(text, file.terminated_bytes, text_length);
  text[text_length] = '\0';
  qa_common_parse_cursor cursor;
  if (!qa_common_parse_cursor_init(&cursor, text, text_length, false))
    qa_range_error("QVM memory allocation failed");
  qa_common_parse_state parser;
  qa_common_parse_state_init(&parser);
  int64_t count = 0;
  for (;;) {
    const char *segment = qa_common_parse_state_parse(&parser, &cursor, true);
    if (segment[0] == '\0') break;
    if (parse_hex(segment) != 0) {
      (void)qa_common_parse_state_parse(&parser, &cursor, true);
      (void)qa_common_parse_state_parse(&parser, &cursor, true);
      continue;
    }
    const char *address = qa_common_parse_state_parse(&parser, &cursor, true);
    if (address[0] == '\0') {
      if (options->print != NULL) options->print(options->print_ctx, "WARNING: incomplete line at end of file\n");
      break;
    }
    char address_copy[1025];
    snprintf(address_copy, sizeof address_copy, "%s", address);
    int32_t value = parse_hex(address_copy);
    const char *symbol_name = qa_common_parse_state_parse(&parser, &cursor, true);
    if (symbol_name[0] == '\0') {
      if (options->print != NULL) options->print(options->print_ctx, "WARNING: incomplete line at end of file\n");
      break;
    }
    char name_copy[1025];
    snprintf(name_copy, sizeof name_copy, "%s", symbol_name);
    size_t name_length = strlen(name_copy);
    uint8_t *bytes = symbols->allocate(symbols->allocate_ctx, 16 + name_length, path);
    record *records = symbols->records;
    if (count == 0) {
      for (size_t i = 0; i < symbols->record_count; i++) free(records[i].name);
      symbols->record_count = 0;
    }
    if (symbols->record_count == symbols->record_capacity) {
      symbols->record_capacity = symbols->record_capacity == 0 ? 16 : symbols->record_capacity * 2;
      records = realloc(records, symbols->record_capacity * sizeof *records);
      if (records == NULL) qa_range_error("QVM memory allocation failed");
      symbols->records = records;
    }
    record *entry = &records[symbols->record_count++];
    entry->name = malloc(name_length + 1);
    if (entry->name == NULL) qa_range_error("QVM memory allocation failed");
    memcpy(entry->name, name_copy, name_length + 1);
    entry->data = bytes;
    entry->data_length = 16 + name_length;
    if (value >= 0 && (size_t)value < symbols->instruction_pointer_count)
      value = symbols->instruction_pointers[value];
    /* TS allocate returns a zeroed Uint8Array. */
    memset(bytes, 0, 16 + name_length);
    data_set_i32(bytes, 4, value);
    for (size_t i = 0; i < name_length; i++) bytes[12 + i] = (uint8_t)name_copy[i];
    count++;
  }
  qa_common_parse_cursor_destroy(&cursor);
  free(text);
  symbols->parsed_count = count;
  if (options->print != NULL) {
    char message[256];
    snprintf(message, sizeof message, "%lld symbols parsed from %s\n", (long long)count, path);
    options->print(options->print_ctx, message);
  }
  options->files->free_file(options->files->ctx, &file);
}

typedef struct sorted_entry {
  record *entry;
  size_t index; /* TS Array.prototype.sort is stable */
} sorted_entry;

static int compare_profile(const void *left, const void *right) {
  const sorted_entry *a = left;
  const sorted_entry *b = right;
  int32_t av = data_i32(a->entry->data, 8);
  int32_t bv = data_i32(b->entry->data, 8);
  if (av != bv) return av < bv ? -1 : 1;
  return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

void qa_qvm_symbols_print_profile(qa_qvm_symbols *symbols, qa_qvm_symbol_print_fn print,
                                  void *print_ctx, bool debug_enabled) {
  assert_live(symbols);
  if (symbols->parsed_count == 0) return;
  record *records = symbols->records;
  size_t count = (size_t)symbols->parsed_count;
  if (count > symbols->record_count) count = symbols->record_count;
  sorted_entry *sorted = malloc((count == 0 ? 1 : count) * sizeof *sorted);
  if (sorted == NULL) qa_range_error("QVM memory allocation failed");
  int64_t total = 0;
  for (size_t i = 0; i < count; i++) {
    sorted[i].entry = &records[i];
    sorted[i].index = i;
    total += data_i32(records[i].data, 8);
  }
  qa_js_sort(sorted, count, sizeof *sorted, compare_profile);
  if (total == 0 && print != NULL) {
    print(print_ctx, debug_enabled
        ? "vmprofile: percentages are undefined with zero total instructions.\n"
        : "vmprofile: percentages are undefined with zero total instructions; DEBUG_VM is disabled.\n");
  }
  for (size_t i = 0; i < count; i++) {
    assert_live(symbols);
    int32_t entry_count = data_i32(sorted[i].entry->data, 8);
    char prefix[16];
    if (total == 0) memcpy(prefix, "    ", 5);
    else {
      /* trunc(fround(100 * fround(count)) / total), as in the TS. */
      double percent = trunc((double)(float)(100.0 * (double)(float)entry_count) / (double)total);
      snprintf(prefix, sizeof prefix, "%2lld%% ", (long long)percent);
    }
    if (print != NULL) {
      char line[1200];
      snprintf(line, sizeof line, "%s%9d %s\n", prefix, (int)entry_count, sorted[i].entry->name);
      print(print_ctx, line);
    }
    assert_live(symbols);
    data_set_i32(sorted[i].entry->data, 8, 0);
  }
  if (print != NULL) {
    char line[64];
    snprintf(line, sizeof line, "    %9lld total\n", (long long)total);
    print(print_ctx, line);
  }
  free(sorted);
}
