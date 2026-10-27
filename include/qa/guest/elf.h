/*
 * Port of src/guest/elf/index.ts: loadElf maps and eagerly relocates one
 * ELF image. The returned qa_elf_guest_image owns its inspection, label
 * strings, export/import name copies and arrays; release with
 * qa_elf_guest_image_free(). The file bytes are copied (TS bytes.slice()).
 * ElfError conditions abort via qa_range_error, so the TS catch-and-unmap
 * rollback (including GNU-unique removal) is unobservable and omitted.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ELF_H
#define QA_GUEST_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/elf/parse.h"
#include "qa/guest/elf/relocate.h"
#include "qa/guest/elf/unwind.h"

typedef struct qa_elf_load_options {
  const uint8_t *bytes;
  size_t bytes_len;
  qa_guest_module_identity module;
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  uint64_t load_bias;
  qa_guest_import_resolver resolver;
  const char *const *dependencies; /* provided library names */
  size_t dependency_count;
  const qa_elf_tls_bindings *tls; /* NULL: absent */
  /* resolveIndirect / resolveSymbolSize share relocate's ctx fields. */
  void *resolve_ctx;
  qa_guest_address (*resolve_indirect)(void *ctx, qa_guest_address resolver_address);
  bool (*resolve_symbol_size)(void *ctx, const qa_guest_import *import_,
                              const qa_guest_image *requesting, size_t *out);
  qa_elf_unique_symbols *unique_symbols; /* NULL: absent */
} qa_elf_load_options;

typedef struct qa_elf_tls_export {
  qa_guest_symbol_name symbol;
  uint64_t offset;
  size_t byte_length;
} qa_elf_tls_export;

typedef struct qa_elf_guest_image {
  qa_guest_image image;
  qa_elf_inspection_full elf; /* owned */
  uint64_t load_bias;
  /* neededLibraries/soname borrow from elf. */
  bool has_executable_stack; /* false: TS null */
  bool executable_stack;
  qa_guest_address *preinitializers;
  size_t preinitializer_count;
  qa_elf_tls_export *tls_exports;
  size_t tls_export_count;
  /* Owned scratch keeping borrowed strings alive (labels, symbol copies). */
  char **owned_strings;
  size_t owned_string_count, owned_string_cap;
  uint8_t *bytes_copy;
} qa_elf_guest_image;

bool qa_load_elf(qa_elf_guest_image *out, const qa_elf_load_options *options);
void qa_elf_guest_image_free(qa_elf_guest_image *image);

#endif
