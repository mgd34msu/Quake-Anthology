/*
 * Port of src/guest/elf/relocate.ts: eager ELF relocation and symbol
 * resolution. Closures/callbacks become function pointers + ctx; the
 * GNU-unique Map and the mutable imports array become small owned lists.
 * TS bigint addresses are uint64_t with wrap arithmetic. The TS address-
 * space identity check has no C counterpart (qa_guest_address carries no
 * space token). ElfError aborts via qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ELF_RELOCATE_H
#define QA_GUEST_ELF_RELOCATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/elf/parse.h"

typedef struct qa_elf_tls_module {
  uint64_t module_id;
  bool has_thread_pointer_offset; /* false: TS null */
  int64_t thread_pointer_offset;
} qa_elf_tls_module;
typedef struct qa_elf_tls_resolution {
  qa_elf_tls_module module;
  int64_t offset;
} qa_elf_tls_resolution;
typedef struct qa_elf_tls_bindings {
  qa_elf_tls_module current;
  void *ctx;
  bool (*resolve)(void *ctx, const qa_guest_import *import_, const qa_guest_image *requesting,
                  qa_elf_tls_resolution *out); /* false: TS null */
} qa_elf_tls_bindings;

typedef struct qa_elf_unique_symbols {
  struct { char *name; qa_guest_address address; } *entries; /* names owned */
  size_t count, cap;
} qa_elf_unique_symbols;
void qa_elf_unique_symbols_init(qa_elf_unique_symbols *map);
void qa_elf_unique_symbols_free(qa_elf_unique_symbols *map);
bool qa_elf_unique_symbols_get(const qa_elf_unique_symbols *map, const char *name, qa_guest_address *out);
void qa_elf_unique_symbols_set(qa_elf_unique_symbols *map, const char *name, qa_guest_address address);
bool qa_elf_unique_symbols_has(const qa_elf_unique_symbols *map, const char *name);
void qa_elf_unique_symbols_delete(qa_elf_unique_symbols *map, const char *name);

typedef struct qa_elf_import_list {
  qa_guest_import *items;
  size_t count, cap;
} qa_elf_import_list;

typedef struct qa_elf_relocation_context {
  const qa_elf_inspection_full *elf;
  qa_mapped_guest_memory *memory;
  const qa_guest_image *image;
  uint64_t load_bias;
  qa_guest_import_resolver resolver;
  const qa_elf_tls_bindings *tls; /* NULL: TS null */
  void *resolve_ctx;
  qa_guest_address (*resolve_indirect)(void *ctx, qa_guest_address resolver_address); /* NULL: TS null */
  bool (*resolve_symbol_size)(void *ctx, const qa_guest_import *import_,
                              const qa_guest_image *requesting, size_t *out); /* NULL: TS null */
  qa_elf_unique_symbols *unique_symbols; /* NULL: TS null */
  qa_elf_import_list *imports;
} qa_elf_relocation_context;

qa_guest_address qa_elf_address(qa_mapped_guest_memory *memory, uint64_t value);
qa_guest_import qa_elf_symbol_import(const qa_elf_symbol *symbol, qa_guest_address slot);
void qa_relocate_elf(qa_elf_relocation_context *context);
uint64_t qa_elf_defined_symbol_address(uint64_t load_bias, qa_mapped_guest_memory *memory,
                                       qa_guest_address (*resolve_indirect)(void *ctx, qa_guest_address),
                                       void *resolve_ctx, const qa_elf_symbol *symbol);
uint64_t qa_elf_resolve_symbol(qa_elf_relocation_context *context, const qa_elf_symbol *symbol,
                               qa_guest_address slot, bool copy);

#endif
