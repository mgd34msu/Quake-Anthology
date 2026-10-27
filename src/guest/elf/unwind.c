/* Port of src/guest/elf/unwind.ts. GPL-2.0-or-later. */
#include "qa/guest/elf/unwind.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

const char *qa_guest_unwind_format_name(qa_guest_unwind_format format) {
  switch (format) {
    case QA_UNWIND_PE_X64: return "pe-x64-unwind";
    case QA_UNWIND_ELF_EH_FRAME: return "elf-eh-frame";
    default: return "elf-debug-frame";
  }
}
/* parse.ts ElfError: message gains the "ELF: " prefix. */
static void elf_error(const char *message) { qa_range_error("ELF: %s", message); }
/* parse.ts checkedNumber(). */
static size_t checked_number(uint64_t value, const char *description) {
  if (value > (uint64_t)QA_MAX_SAFE_INTEGER) qa_range_error("ELF: %s exceeds checked host indexing", description);
  return (size_t)value;
}
static size_t checked_number_i64(int64_t value, const char *description) {
  if (value < 0) qa_range_error("ELF: %s exceeds checked host indexing", description);
  return checked_number((uint64_t)value, description);
}
/* parse.ts elfFileOffset(). */
static size_t elf_file_offset(const qa_elf_inspection *elf, uint64_t address, size_t size) {
  for (size_t i = 0; i < elf->segment_count; i++) {
    const qa_elf_segment *segment = &elf->segments[i];
    if (segment->type != 1 || address < segment->address) continue;
    uint64_t displacement = address - segment->address;
    if (displacement + size <= segment->file_size)
      return segment->offset + checked_number(displacement, "segment displacement");
  }
  qa_range_error("ELF: virtual file range has no PT_LOAD backing");
  return 0;
}
/* relocate.ts elfAddress(). */
static qa_guest_address elf_address(qa_mapped_guest_memory *memory, uint64_t value) {
  qa_guest_address pointer;
  if (!qa_memory_pointer(memory, value, &pointer)) elf_error("required address is null");
  return pointer;
}

typedef struct dwarf_reader {
  const uint8_t *bytes;
  size_t len;
  uint64_t address;
  int pointer_bytes;
  bool has_data_relative_base;
  uint64_t data_relative_base;
} dwarf_reader;
typedef struct dwarf_cursor { size_t position; } dwarf_cursor;

static size_t reader_require(const dwarf_reader *r, dwarf_cursor *cursor, size_t bytes, size_t end) {
  size_t offset = cursor->position;
  if (offset > end || bytes > end - offset) elf_error("truncated DWARF unwind record");
  cursor->position += bytes;
  (void)r;
  return offset;
}
static uint8_t reader_byte(const dwarf_reader *r, dwarf_cursor *cursor) {
  return r->bytes[reader_require(r, cursor, 1, r->len)];
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p) { return (uint32_t)get_u16(p) | ((uint32_t)get_u16(p + 2) << 16); }
static uint64_t get_u64(const uint8_t *p) { return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32); }
static uint32_t reader_u32(const dwarf_reader *r, dwarf_cursor *cursor) {
  return get_u32(r->bytes + reader_require(r, cursor, 4, r->len));
}
static uint64_t reader_u64(const dwarf_reader *r, dwarf_cursor *cursor) {
  return get_u64(r->bytes + reader_require(r, cursor, 8, r->len));
}
static int64_t reader_leb(const dwarf_reader *r, dwarf_cursor *cursor, bool is_signed) {
  uint64_t value = 0;
  for (int shift = 0; shift < 70; shift += 7) {
    uint8_t byte = reader_byte(r, cursor);
    if (shift < 64) value |= (uint64_t)(byte & 127u) << shift;
    if ((byte & 128u) == 0) {
      if (is_signed && (byte & 64u) != 0) {
        /* value - 2^(shift+7): sign-extend within the consumed bits. */
        if (shift + 7 < 64) return (int64_t)(value - (1ull << (shift + 7)));
        return (int64_t)value;
      }
      return (int64_t)value;
    }
  }
  elf_error("oversized DWARF LEB128");
  return 0;
}
static uint64_t reader_encoded(const dwarf_reader *r, dwarf_cursor *cursor, int encoding, bool relative) {
  if (encoding == 0xff) elf_error("omitted DWARF pointer used as an address");
  if ((encoding & 0x70) == 0x50) {
    uint64_t absolute = r->address + cursor->position;
    cursor->position += (size_t)(((uint64_t)r->pointer_bytes - absolute % (uint64_t)r->pointer_bytes) % (uint64_t)r->pointer_bytes);
  }
  uint64_t address = r->address + cursor->position;
  int64_t value = 0;
  switch (encoding & 15) {
    case 0: value = (int64_t)(r->pointer_bytes == 8 ? reader_u64(r, cursor) : reader_u32(r, cursor)); break;
    case 1: value = reader_leb(r, cursor, false); break;
    case 2: value = get_u16(r->bytes + reader_require(r, cursor, 2, r->len)); break;
    case 3: value = reader_u32(r, cursor); break;
    case 4: value = (int64_t)reader_u64(r, cursor); break;
    case 9: value = reader_leb(r, cursor, true); break;
    case 10: value = (int16_t)get_u16(r->bytes + reader_require(r, cursor, 2, r->len)); break;
    case 11: value = (int32_t)reader_u32(r, cursor); break;
    case 12: value = (int64_t)get_u64(r->bytes + reader_require(r, cursor, 8, r->len)); break;
    default: qa_range_error("ELF: unsupported DWARF pointer encoding 0x%x", encoding);
  }
  uint64_t result = (uint64_t)value;
  if (relative) {
    switch (encoding & 0x70) {
      case 0: case 0x50: break;
      case 0x10: result += address; break;
      case 0x30:
        if (!r->has_data_relative_base) elf_error("DWARF data-relative pointer requires an explicit base");
        result += r->data_relative_base;
        break;
      default: qa_range_error("ELF: unsupported DWARF relative pointer base 0x%x", encoding);
    }
  }
  return result;
}

typedef struct cie_entry { size_t offset; int encoding; bool augmentation; } cie_entry;
typedef struct region_list { qa_guest_unwind_region *items; size_t count; size_t cap; } region_list;
static bool region_push(region_list *list, const qa_guest_unwind_region *region) {
  if (list->count == list->cap) {
    size_t next = list->cap == 0 ? 8 : list->cap * 2;
    qa_guest_unwind_region *items = (qa_guest_unwind_region *)realloc(list->items, next * sizeof *items);
    if (items == NULL) return false;
    list->items = items;
    list->cap = next;
  }
  list->items[list->count++] = *region;
  return true;
}
void qa_elf_unwind_regions_free(qa_guest_unwind_region *regions, size_t count) {
  if (regions == NULL) return;
  for (size_t i = 0; i < count; i++) free(regions[i].metadata);
  free(regions);
}
static bool read_frames(region_list *regions, const uint8_t *bytes, size_t len, uint64_t address,
                        const qa_elf_inspection *elf, qa_mapped_guest_memory *memory, uint64_t load_bias, bool debug) {
  dwarf_reader reader = {bytes, len, address, elf->pointer_bytes, false, 0};
  dwarf_cursor cursor = {0};
  cie_entry *cies = NULL;
  size_t cie_count = 0, cie_cap = 0;
  bool ok = true;
  while (cursor.position < len) {
    size_t start = cursor.position;
    if (len - start < 4) {
      bool all_zero = true;
      for (size_t i = start; i < len; i++) if (bytes[i] != 0) all_zero = false;
      if (all_zero) break;
    }
    uint32_t initial = reader_u32(&reader, &cursor);
    if (initial == 0) break;
    bool wide = initial == 0xffffffffu;
    size_t length = wide ? checked_number(reader_u64(&reader, &cursor), "DWARF record length") : initial;
    size_t end = cursor.position + length;
    if (end > len || length < (wide ? 8u : 4u)) elf_error("invalid DWARF record length");
    size_t id_position = cursor.position;
    uint64_t id = wide ? reader_u64(&reader, &cursor) : reader_u32(&reader, &cursor);
    bool is_cie = debug ? id == (wide ? UINT64_MAX : 0xffffffffull) : id == 0;
    if (is_cie) {
      uint8_t version = reader_byte(&reader, &cursor);
      if (version != 1 && version != 3 && version != 4) qa_range_error("ELF: unsupported CIE version %u", version);
      char augmentation[64];
      size_t aug_len = 0;
      for (;;) {
        uint8_t c = reader_byte(&reader, &cursor);
        if (c == 0) break;
        if (aug_len + 1 < sizeof augmentation) augmentation[aug_len++] = (char)c;
      }
      augmentation[aug_len] = 0;
      if (version == 4 && (reader_byte(&reader, &cursor) != elf->pointer_bytes || reader_byte(&reader, &cursor) != 0))
        elf_error("unsupported CIE address or segment size");
      (void)reader_leb(&reader, &cursor, false);
      (void)reader_leb(&reader, &cursor, true);
      if (version == 1) (void)reader_byte(&reader, &cursor);
      else (void)reader_leb(&reader, &cursor, false);
      int encoding = 0;
      bool has_augmentation = augmentation[0] == 'z';
      if (has_augmentation) {
        size_t size = checked_number_i64(reader_leb(&reader, &cursor, false), "CIE augmentation size");
        size_t augmentation_end = cursor.position + size;
        if (augmentation_end > end) elf_error("CIE augmentation exceeds record");
        for (size_t i = 1; i < aug_len; i++) {
          switch (augmentation[i]) {
            case 'R': encoding = reader_byte(&reader, &cursor); break;
            case 'L': (void)reader_byte(&reader, &cursor); break;
            case 'P': {
              int personality = reader_byte(&reader, &cursor);
              (void)reader_encoded(&reader, &cursor, personality, false);
              break;
            }
            case 'S': break;
            default: qa_range_error("ELF: unsupported CIE augmentation %c", augmentation[i]);
          }
        }
        if (cursor.position > augmentation_end) elf_error("CIE fields exceed augmentation size");
        cursor.position = augmentation_end;
      } else if (aug_len != 0) qa_range_error("ELF: unsupported CIE augmentation %s", augmentation);
      if (cie_count == cie_cap) {
        size_t next = cie_cap == 0 ? 8 : cie_cap * 2;
        cie_entry *grown = (cie_entry *)realloc(cies, next * sizeof *grown);
        if (grown == NULL) { ok = false; break; }
        cies = grown; cie_cap = next;
      }
      cies[cie_count].offset = start;
      cies[cie_count].encoding = encoding;
      cies[cie_count].augmentation = has_augmentation;
      cie_count++;
    } else {
      size_t cie_position = debug ? checked_number(id, "debug CIE offset")
                                  : id_position - checked_number(id, "CIE displacement");
      const cie_entry *cie = NULL;
      for (size_t i = 0; i < cie_count; i++) if (cies[i].offset == cie_position) cie = &cies[i];
      if (cie == NULL) elf_error("FDE references an unavailable CIE");
      uint64_t pc = reader_encoded(&reader, &cursor, cie->encoding, true);
      if ((cie->encoding & 0x80) != 0) {
        uint8_t slot[8];
        qa_memory_copy(memory, elf_address(memory, pc), slot, (size_t)elf->pointer_bytes);
        pc = elf->pointer_bytes == 8 ? get_u64(slot) : get_u32(slot);
      }
      int64_t range = (int64_t)reader_encoded(&reader, &cursor, cie->encoding & 15, false);
      if (debug) pc += load_bias;
      if (cie->augmentation) {
        size_t size = checked_number_i64(reader_leb(&reader, &cursor, false), "FDE augmentation size");
        (void)reader_require(&reader, &cursor, size, end);
      }
      if (range < 0) elf_error("negative FDE PC range");
      if (range != 0) {
        qa_guest_unwind_region region;
        region.start = elf_address(memory, pc);
        region.end = elf_address(memory, pc + (uint64_t)range);
        region.format = debug ? QA_UNWIND_ELF_DEBUG_FRAME : QA_UNWIND_ELF_EH_FRAME;
        region.metadata = (uint8_t *)malloc(len == 0 ? 1 : len);
        if (region.metadata == NULL) { ok = false; break; }
        memcpy(region.metadata, bytes, len);
        region.metadata_len = len;
        if (!region_push(regions, &region)) { free(region.metadata); ok = false; break; }
      }
    }
    if (cursor.position > end) elf_error("unwind fields exceed record");
    cursor.position = end;
  }
  free(cies);
  return ok;
}
bool qa_read_elf_unwind(const qa_elf_inspection *elf, const uint8_t *source, size_t source_len,
                        qa_mapped_guest_memory *memory, uint64_t load_bias,
                        qa_guest_unwind_region **out_regions, size_t *out_count) {
  region_list regions = {NULL, 0, 0};
  bool has_eh_frame_section = false;
  for (size_t i = 0; i < elf->section_count; i++) {
    const qa_elf_section *section = &elf->sections[i];
    bool eh = strcmp(section->name, ".eh_frame") == 0;
    bool debug = strcmp(section->name, ".debug_frame") == 0;
    if (!eh && !debug) continue;
    if (eh) has_eh_frame_section = true;
    bool allocated = (section->flags & 2u) != 0;
    uint8_t *bytes = (uint8_t *)malloc(section->size == 0 ? 1 : section->size);
    if (bytes == NULL) { qa_elf_unwind_regions_free(regions.items, regions.count); return false; }
    if (allocated) {
      qa_memory_copy(memory, elf_address(memory, load_bias + section->address), bytes, section->size);
    } else {
      /* TS source.slice() clamps at the buffer end. */
      size_t count = section->offset < source_len
        ? (source_len - section->offset < section->size ? source_len - section->offset : section->size) : 0;
      memcpy(bytes, source + (section->offset < source_len ? section->offset : source_len), count);
      if (count < section->size) memset(bytes + count, 0, section->size - count);
      if (!read_frames(&regions, bytes, count, section->address, elf, memory, load_bias, debug)) {
        free(bytes); qa_elf_unwind_regions_free(regions.items, regions.count); return false;
      }
      free(bytes);
      continue;
    }
    if (!read_frames(&regions, bytes, section->size, allocated ? load_bias + section->address : section->address,
                     elf, memory, load_bias, debug)) {
      free(bytes); qa_elf_unwind_regions_free(regions.items, regions.count); return false;
    }
    free(bytes);
  }
  if (!has_eh_frame_section) {
    const qa_elf_segment *header = NULL;
    for (size_t i = 0; i < elf->segment_count; i++)
      if (elf->segments[i].type == 0x6474e550u) { header = &elf->segments[i]; break; }
    if (header != NULL) {
      uint8_t *bytes = (uint8_t *)malloc(header->file_size == 0 ? 1 : header->file_size);
      if (bytes == NULL) { qa_elf_unwind_regions_free(regions.items, regions.count); return false; }
      qa_memory_copy(memory, elf_address(memory, load_bias + header->address), bytes, header->file_size);
      dwarf_reader reader = {bytes, header->file_size, load_bias + header->address, elf->pointer_bytes, true, load_bias + header->address};
      dwarf_cursor cursor = {0};
      if (reader_byte(&reader, &cursor) != 1) elf_error("unsupported GNU EH frame header version");
      int encoding = reader_byte(&reader, &cursor);
      (void)reader_byte(&reader, &cursor);
      (void)reader_byte(&reader, &cursor);
      if ((encoding & 0x80) != 0) elf_error("indirect GNU EH frame section pointer is unsupported");
      uint64_t address = reader_encoded(&reader, &cursor, encoding, true);
      uint64_t original = address - load_bias;
      const qa_elf_segment *segment = NULL;
      for (size_t i = 0; i < elf->segment_count; i++) {
        const qa_elf_segment *item = &elf->segments[i];
        if (item->type == 1 && original >= item->address && original < item->address + item->file_size) { segment = item; break; }
      }
      if (segment == NULL) elf_error("GNU EH frame pointer has no load segment");
      size_t available = segment->file_size - checked_number(original - segment->address, "EH frame displacement");
      (void)elf_file_offset(elf, original, available);
      uint8_t *frame = (uint8_t *)malloc(available == 0 ? 1 : available);
      if (frame == NULL) { free(bytes); qa_elf_unwind_regions_free(regions.items, regions.count); return false; }
      qa_memory_copy(memory, elf_address(memory, address), frame, available);
      bool ok = read_frames(&regions, frame, available, address, elf, memory, load_bias, false);
      free(frame);
      free(bytes);
      if (!ok) { qa_elf_unwind_regions_free(regions.items, regions.count); return false; }
    }
  }
  *out_regions = regions.items;
  *out_count = regions.count;
  return true;
}
