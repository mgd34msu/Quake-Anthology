/*
 * Port of src/guest/elf/parse.ts: checked ELF32/ELF64 inspection (headers,
 * program/section tables, dynamic tags, symbols, versions, relocations).
 * ElfError aborts with the same "ELF: ..." message via qa_range_error.
 * The unwind module's qa_elf_inspection/qa_elf_segment/qa_elf_section
 * (unwind.h) are field subsets used by unwind.ts; the full parse.ts shapes
 * use _info/_full names here, with qa_elf_unwind_view() adapting a full
 * inspection for qa_read_elf_unwind(). All strings/arrays in a full
 * inspection are owned copies; release with qa_elf_inspection_full_free().
 * Allocation failure aborts (no TS counterpart). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ELF_PARSE_H
#define QA_GUEST_ELF_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/elf/unwind.h"

typedef struct qa_elf_segment_info {
  uint32_t type;
  uint32_t flags;
  size_t offset;
  uint64_t address;
  size_t file_size;
  size_t memory_size;
  uint64_t alignment;
} qa_elf_segment_info;

typedef struct qa_elf_section_info {
  char *name; /* owned */
  uint32_t type;
  uint64_t flags;
  uint64_t address;
  size_t offset;
  size_t size;
  uint32_t link;
  uint32_t info;
  size_t entry_size;
} qa_elf_section_info;

typedef struct qa_elf_version {
  char *name; /* owned */
  char *library; /* owned */
  bool hidden;
  bool weak;
} qa_elf_version;

typedef struct qa_elf_symbol {
  size_t index;
  char *name; /* owned */
  uint64_t value;
  size_t size;
  int binding;
  int type;
  int visibility;
  int section;
  bool has_version;
  qa_elf_version version;
} qa_elf_symbol;

typedef enum qa_elf_relocation_table {
  QA_ELF_RELOC_REL = 0, QA_ELF_RELOC_RELA, QA_ELF_RELOC_PLT_REL, QA_ELF_RELOC_PLT_RELA, QA_ELF_RELOC_RELR
} qa_elf_relocation_table;
const char *qa_elf_relocation_table_name(qa_elf_relocation_table table);

typedef struct qa_elf_relocation {
  uint64_t address;
  uint32_t type;
  uint64_t symbol_index;
  bool has_addend; /* false: addend lives in the mapped slot */
  int64_t addend;
  qa_elf_relocation_table table;
} qa_elf_relocation;

typedef struct qa_elf_dynamic_entry {
  uint64_t tag;
  uint64_t *values; /* owned */
  size_t value_count;
} qa_elf_dynamic_entry;

typedef struct qa_elf_inspection_full {
  qa_guest_native_abi abi; /* image elf32|elf64 */
  bool is_shared; /* false: executable */
  uint64_t entry_point;
  qa_elf_segment_info *segments;
  size_t segment_count;
  qa_elf_section_info *sections;
  size_t section_count;
  qa_elf_dynamic_entry *dynamic;
  size_t dynamic_count;
  char **needed_libraries; /* owned strings */
  size_t needed_library_count;
  char *soname; /* owned, NULL = TS null */
  char *interpreter;
  char *runpath;
  char *rpath;
  qa_elf_symbol *symbols;
  size_t symbol_count;
  qa_elf_relocation *relocations;
  size_t relocation_count;
} qa_elf_inspection_full;

size_t qa_elf_checked_number(uint64_t value, const char *description);
bool qa_elf_dynamic_value(const qa_elf_inspection_full *elf, uint64_t tag, uint64_t *out);
bool qa_elf_dynamic_has(const qa_elf_inspection_full *elf, uint64_t tag);
size_t qa_elf_file_offset(const qa_elf_segment_info *segments, size_t segment_count,
                           uint64_t address, size_t size);
void qa_inspect_elf(qa_elf_inspection_full *out, const uint8_t *bytes, size_t len);
void qa_elf_inspection_full_free(qa_elf_inspection_full *elf);

/* Adapters to the unwind.h subset view; caller frees the two arrays. */
typedef struct qa_elf_unwind_view {
  qa_elf_inspection inspection;
  qa_elf_segment *segments;
  qa_elf_section *sections;
} qa_elf_unwind_view;
void qa_elf_unwind_view_init(qa_elf_unwind_view *view, const qa_elf_inspection_full *elf);
void qa_elf_unwind_view_free(qa_elf_unwind_view *view);

#endif
