/*
 * Port of src/compat/qvm/symbols.ts. C accommodations:
 *  - The allocate/assertLive/print callbacks are fn + ctx.
 *  - Symbol records keep the TS 16-byte vmSymbol_t release32 layout in
 *    allocator-provided storage; value/profileCount read and write it.
 *  - entries fills a caller array; valueToFunctionSymbol yields a
 *    handle whose profile count is got/set through functions so the
 *    TS live getter/setter semantics hold.
 *  - valueToSymbol writes into a caller buffer (>= 1024 bytes for the
 *    full TS truncation behavior).
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_SYMBOLS_H
#define QA_COMPAT_QVM_SYMBOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t *(*qa_qvm_symbol_allocate_fn)(void *ctx, size_t bytes, const char *resource);
typedef void (*qa_qvm_assert_live_fn)(void *ctx);
typedef void (*qa_qvm_symbol_print_fn)(void *ctx, const char *text);

/* TS QvmSymbolFile / QvmSymbolFiles. read returns false for TS
 * undefined; the file handle is an opaque token the caller frees. */
typedef struct qa_qvm_symbol_file {
  const uint8_t *terminated_bytes;
  size_t terminated_length;
  void *handle;
} qa_qvm_symbol_file;

typedef struct qa_qvm_symbol_files {
  void *ctx;
  bool (*read_file_retained_sync)(void *ctx, const char *path, qa_qvm_symbol_file *out);
  void (*free_file)(void *ctx, qa_qvm_symbol_file *file);
} qa_qvm_symbol_files;

/* TS QvmSymbolLoadOptions. */
typedef struct qa_qvm_symbol_load_options {
  const char *name;
  int32_t developer;
  const qa_qvm_symbol_files *files;
  qa_qvm_symbol_print_fn print;
  void *print_ctx;
} qa_qvm_symbol_load_options;

/* TS QvmSymbol. */
typedef struct qa_qvm_symbol {
  int32_t value;
  const char *name; /* borrowed */
  int32_t profile_count;
} qa_qvm_symbol;

/* TS QvmFunctionSymbol handle (record index, or the null symbol). */
typedef struct qa_qvm_function_symbol {
  int32_t value;
  const char *name;
  struct qa_qvm_symbols *owner; /* NULL for the null symbol */
  size_t index;
} qa_qvm_function_symbol;

typedef struct qa_qvm_symbols {
  const int32_t *instruction_pointers; /* borrowed */
  size_t instruction_pointer_count;
  qa_qvm_symbol_allocate_fn allocate;
  void *allocate_ctx;
  qa_qvm_assert_live_fn assert_live;
  void *assert_live_ctx;
  void *records; /* opaque */
  size_t record_count;
  size_t record_capacity;
  int64_t parsed_count;
  qa_qvm_symbol_print_fn print;
  void *print_ctx;
} qa_qvm_symbols;

void qa_qvm_symbols_init(qa_qvm_symbols *symbols, const int32_t *instruction_pointers,
                         size_t instruction_pointer_count, qa_qvm_symbol_allocate_fn allocate,
                         void *allocate_ctx, qa_qvm_assert_live_fn assert_live, void *assert_live_ctx);
void qa_qvm_symbols_destroy(qa_qvm_symbols *symbols);
int64_t qa_qvm_symbols_get_count(qa_qvm_symbols *symbols);
/* Fills up to capacity entries; returns the total entry count. */
size_t qa_qvm_symbols_entries(qa_qvm_symbols *symbols, qa_qvm_symbol *out, size_t capacity);
qa_qvm_function_symbol qa_qvm_symbols_value_to_function_symbol(qa_qvm_symbols *symbols, int32_t value);
int32_t qa_qvm_function_symbol_get_profile_count(qa_qvm_function_symbol *symbol);
void qa_qvm_function_symbol_set_profile_count(qa_qvm_function_symbol *symbol, int32_t count);
void qa_qvm_symbols_value_to_symbol(qa_qvm_symbols *symbols, int32_t value, char *out, size_t out_size);
int32_t qa_qvm_symbols_symbol_to_value(qa_qvm_symbols *symbols, const char *name);
void qa_qvm_symbols_load(qa_qvm_symbols *symbols, const qa_qvm_symbol_load_options *options);
void qa_qvm_symbols_print_profile(qa_qvm_symbols *symbols, qa_qvm_symbol_print_fn print,
                                  void *print_ctx, bool debug_enabled);

#endif
