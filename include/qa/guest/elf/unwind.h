/*
 * Port of src/guest/elf/unwind.ts: CIE/FDE indexing of .eh_frame/.debug_frame
 * and the GNU EH frame header fallback. ElfInspection/ElfSegment/ElfSection
 * come from src/guest/elf/parse.ts (unported); the minimal fields unwind.ts
 * reads are mirrored here. checkedNumber/elfFileOffset (parse.ts) and
 * elfAddress (relocate.ts) are mirrored locally in unwind.c. TS ElfError
 * throws abort with the same "ELF: ..." message via qa_range_error (the TS
 * loader surface treating them as fatal load failures). GuestUnwindRegion
 * comes from src/guest/core/contracts.ts. Each returned region owns a copy
 * of the section metadata bytes (TS shares one Uint8Array per section).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_ELF_UNWIND_H
#define QA_GUEST_ELF_UNWIND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/core/registers.h"

typedef struct qa_elf_segment {
  uint32_t type;
  uint64_t address;
  size_t offset;
  size_t file_size;
} qa_elf_segment;

typedef struct qa_elf_section {
  const char *name;
  uint64_t flags;
  uint64_t address;
  size_t offset;
  size_t size;
} qa_elf_section;

typedef struct qa_elf_inspection {
  int pointer_bytes; /* abi.pointerBytes: 4|8 */
  const qa_elf_segment *segments;
  size_t segment_count;
  const qa_elf_section *sections;
  size_t section_count;
} qa_elf_inspection;

typedef enum qa_guest_unwind_format {
  QA_UNWIND_PE_X64 = 0, QA_UNWIND_ELF_EH_FRAME, QA_UNWIND_ELF_DEBUG_FRAME
} qa_guest_unwind_format;
const char *qa_guest_unwind_format_name(qa_guest_unwind_format format);

typedef struct qa_guest_unwind_region {
  qa_guest_address start;
  qa_guest_address end;
  qa_guest_unwind_format format;
  uint8_t *metadata; /* owned */
  size_t metadata_len;
} qa_guest_unwind_region;

/* Returns false only on allocation failure; malformed DWARF aborts. */
bool qa_read_elf_unwind(const qa_elf_inspection *elf, const uint8_t *source, size_t source_len,
                        qa_mapped_guest_memory *memory, uint64_t load_bias,
                        qa_guest_unwind_region **out_regions, size_t *out_count);
void qa_elf_unwind_regions_free(qa_guest_unwind_region *regions, size_t count);

#endif
