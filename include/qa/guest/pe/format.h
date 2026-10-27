/*
 * Port of src/guest/pe/format.ts: checked PE32/PE32+ header parsing.
 * PeError aborts with the same "PE <stage>: <detail>" message via
 * qa_range_error. Section names are copied bytes (the TS fatal UTF-8
 * decode of the 8-byte name field is not re-validated). A parsed file
 * owns its section names; release with qa_pe_file_free(). Allocation
 * failure aborts (no TS counterpart). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_PE_FORMAT_H
#define QA_GUEST_PE_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"

typedef enum qa_pe_stage {
  QA_PE_STAGE_HEADERS = 0, QA_PE_STAGE_MAPPING, QA_PE_STAGE_RELOCATION, QA_PE_STAGE_IMPORTS,
  QA_PE_STAGE_EXPORTS, QA_PE_STAGE_TLS, QA_PE_STAGE_UNWIND, QA_PE_STAGE_LOAD_CONFIG
} qa_pe_stage;
const char *qa_pe_stage_name(qa_pe_stage stage);
void qa_pe_error(qa_pe_stage stage, const char *format, ...);

typedef struct qa_pe_directory_entry { uint32_t rva; uint32_t byte_length; } qa_pe_directory_entry;
typedef struct qa_pe_section {
  char *name; /* owned */
  uint32_t rva;
  uint32_t virtual_size;
  uint32_t mapped_size;
  uint32_t raw_offset;
  uint32_t raw_size;
  uint32_t characteristics;
  qa_guest_permissions permissions;
} qa_pe_section;
typedef struct qa_pe_file {
  qa_guest_native_abi abi;
  uint64_t preferred_base;
  uint32_t entry_point_rva;
  uint32_t image_size;
  uint32_t header_size;
  uint32_t section_alignment;
  uint32_t file_alignment;
  uint32_t characteristics;
  uint32_t dll_characteristics;
  qa_pe_section *sections; /* owned */
  size_t section_count;
  qa_pe_directory_entry directories[16];
} qa_pe_file;

void qa_pe_checked_range(size_t offset, size_t length, size_t limit, qa_pe_stage stage);

typedef struct qa_pe_reader {
  uint8_t *bytes; /* mutable: loader relocation patches through it */
  size_t len;
  qa_pe_stage stage;
} qa_pe_reader;
uint16_t qa_pe_reader_u16(const qa_pe_reader *reader, size_t offset);
uint32_t qa_pe_reader_u32(const qa_pe_reader *reader, size_t offset);
uint64_t qa_pe_reader_u64(const qa_pe_reader *reader, size_t offset);
void qa_pe_reader_set_u16(qa_pe_reader *reader, size_t offset, uint16_t value);
void qa_pe_reader_set_u32(qa_pe_reader *reader, size_t offset, uint32_t value);
void qa_pe_reader_set_u64(qa_pe_reader *reader, size_t offset, uint64_t value);
/* Malloc'd ASCII string; caller frees. */
char *qa_pe_reader_text(const qa_pe_reader *reader, size_t offset, size_t limit);

void qa_parse_pe(qa_pe_file *out, const uint8_t *bytes, size_t len);
void qa_pe_file_free(qa_pe_file *file);
qa_pe_directory_entry qa_pe_directory(const qa_pe_file *file, int index);

#endif
