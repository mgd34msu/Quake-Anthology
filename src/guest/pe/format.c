/* Port of src/guest/pe/format.ts. GPL-2.0-or-later. */
#include "qa/guest/pe/format.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("PE headers: out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("PE headers: out of memory");
  return p;
}

const char *qa_pe_stage_name(qa_pe_stage stage) {
  switch (stage) {
    case QA_PE_STAGE_HEADERS: return "headers";
    case QA_PE_STAGE_MAPPING: return "mapping";
    case QA_PE_STAGE_RELOCATION: return "relocation";
    case QA_PE_STAGE_IMPORTS: return "imports";
    case QA_PE_STAGE_EXPORTS: return "exports";
    case QA_PE_STAGE_TLS: return "tls";
    case QA_PE_STAGE_UNWIND: return "unwind";
    case QA_PE_STAGE_LOAD_CONFIG: return "load-config";
  }
  return "";
}
void qa_pe_error(qa_pe_stage stage, const char *format, ...) {
  char detail[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(detail, sizeof detail, format, args);
  va_end(args);
  qa_range_error("PE %s: %s", qa_pe_stage_name(stage), detail);
}

void qa_pe_checked_range(size_t offset, size_t length, size_t limit, qa_pe_stage stage) {
  if (offset > limit || length > limit - offset)
    qa_pe_error(stage, "range %zu+%zu exceeds %zu", offset, length, limit);
}

uint16_t qa_pe_reader_u16(const qa_pe_reader *reader, size_t offset) {
  qa_pe_checked_range(offset, 2, reader->len, reader->stage);
  return (uint16_t)(reader->bytes[offset] | (reader->bytes[offset + 1] << 8));
}
uint32_t qa_pe_reader_u32(const qa_pe_reader *reader, size_t offset) {
  qa_pe_checked_range(offset, 4, reader->len, reader->stage);
  uint32_t value = 0;
  for (int i = 3; i >= 0; i--) value = (value << 8) | reader->bytes[offset + (size_t)i];
  return value;
}
uint64_t qa_pe_reader_u64(const qa_pe_reader *reader, size_t offset) {
  qa_pe_checked_range(offset, 8, reader->len, reader->stage);
  uint64_t value = 0;
  for (int i = 7; i >= 0; i--) value = (value << 8) | reader->bytes[offset + (size_t)i];
  return value;
}
void qa_pe_reader_set_u16(qa_pe_reader *reader, size_t offset, uint16_t value) {
  qa_pe_checked_range(offset, 2, reader->len, reader->stage);
  reader->bytes[offset] = (uint8_t)value;
  reader->bytes[offset + 1] = (uint8_t)(value >> 8);
}
void qa_pe_reader_set_u32(qa_pe_reader *reader, size_t offset, uint32_t value) {
  qa_pe_checked_range(offset, 4, reader->len, reader->stage);
  for (int i = 0; i < 4; i++) reader->bytes[offset + (size_t)i] = (uint8_t)(value >> (i * 8));
}
void qa_pe_reader_set_u64(qa_pe_reader *reader, size_t offset, uint64_t value) {
  qa_pe_checked_range(offset, 8, reader->len, reader->stage);
  for (int i = 0; i < 8; i++) reader->bytes[offset + (size_t)i] = (uint8_t)(value >> (i * 8));
}
char *qa_pe_reader_text(const qa_pe_reader *reader, size_t offset, size_t limit) {
  qa_pe_checked_range(offset, limit, reader->len, reader->stage);
  size_t count = 0;
  for (size_t i = offset; i < offset + limit; i++) {
    uint8_t value = reader->bytes[i];
    if (value == 0) {
      char *text = xmalloc(count + 1);
      memcpy(text, reader->bytes + offset, count);
      text[count] = '\0';
      return text;
    }
    if (value > 127) qa_pe_error(reader->stage, "invalid ASCII string");
    count++;
  }
  qa_pe_error(reader->stage, "unterminated ASCII string");
  return NULL;
}

static bool power_of_two(uint32_t value) {
  return value > 0 && floor(log2((double)value)) == log2((double)value);
}
static uint32_t align32(uint32_t value, uint32_t alignment) {
  return (uint32_t)(ceil((double)value / (double)alignment) * (double)alignment);
}
static qa_guest_permissions section_permissions(uint32_t flags) {
  bool read = (flags & 0x40000000u) != 0;
  bool write = (flags & 0x80000000u) != 0;
  bool execute = (flags & 0x20000000u) != 0;
  if (write) return execute ? QA_PERM_READ_WRITE_EXECUTE : QA_PERM_READ_WRITE;
  if (execute) return read ? QA_PERM_READ_EXECUTE : QA_PERM_EXECUTE;
  return read ? QA_PERM_READ : QA_PERM_NONE;
}

void qa_parse_pe(qa_pe_file *out, const uint8_t *bytes, size_t len) {
  memset(out, 0, sizeof *out);
  qa_pe_reader reader = {(uint8_t *)bytes, len, QA_PE_STAGE_HEADERS};
  if (qa_pe_reader_u16(&reader, 0) != 0x5a4d) qa_pe_error(QA_PE_STAGE_HEADERS, "missing MZ signature");
  uint32_t pe = qa_pe_reader_u32(&reader, 0x3c);
  if (pe < 64 || qa_pe_reader_u32(&reader, pe) != 0x4550)
    qa_pe_error(QA_PE_STAGE_HEADERS, "invalid PE signature offset");
  uint16_t machine = qa_pe_reader_u16(&reader, pe + 4);
  uint16_t section_count = qa_pe_reader_u16(&reader, pe + 6);
  uint16_t optional_size = qa_pe_reader_u16(&reader, pe + 20);
  uint16_t characteristics = qa_pe_reader_u16(&reader, pe + 22);
  if (section_count < 1 || section_count > 96 || (characteristics & 2) == 0)
    qa_pe_error(QA_PE_STAGE_HEADERS, "invalid executable section count/characteristics");
  size_t optional = (size_t)pe + 24;
  qa_pe_checked_range(optional, optional_size, len, QA_PE_STAGE_HEADERS);
  uint16_t magic = qa_pe_reader_u16(&reader, optional);
  qa_guest_native_abi abi;
  size_t directory_offset;
  if (machine == 0x14c && magic == 0x10b) {
    abi = (qa_guest_native_abi){"windows-i386", "pe32", 4, "cdecl"};
    directory_offset = 96;
  } else if (machine == 0x8664 && magic == 0x20b) {
    abi = (qa_guest_native_abi){"windows-x86-64", "pe32+", 8, "microsoft-x64"};
    directory_offset = 112;
  } else {
    qa_pe_error(QA_PE_STAGE_HEADERS, "unsupported machine/magic 0x%x/0x%x", machine, magic);
    return;
  }
  if (optional_size < directory_offset) qa_pe_error(QA_PE_STAGE_HEADERS, "truncated optional header");
  uint64_t preferred_base = abi.pointer_bytes == 4 ? qa_pe_reader_u32(&reader, optional + 28)
                                                   : qa_pe_reader_u64(&reader, optional + 24);
  uint32_t entry_point_rva = qa_pe_reader_u32(&reader, optional + 16);
  uint32_t section_alignment = qa_pe_reader_u32(&reader, optional + 32);
  uint32_t file_alignment = qa_pe_reader_u32(&reader, optional + 36);
  uint32_t image_size = qa_pe_reader_u32(&reader, optional + 56);
  uint32_t header_size = qa_pe_reader_u32(&reader, optional + 60);
  if (!power_of_two(section_alignment) || !power_of_two(file_alignment) || file_alignment > 65536
      || section_alignment < file_alignment
      || (section_alignment < 4096 ? file_alignment != section_alignment : file_alignment < 512))
    qa_pe_error(QA_PE_STAGE_HEADERS, "invalid section/file alignment");
  uint64_t width_limit = abi.pointer_bytes == 4 ? 0x100000000ULL : UINT64_MAX;
  bool base_exceeds = abi.pointer_bytes == 8
      ? (preferred_base != 0 && image_size > (uint64_t)(0ULL - preferred_base))
      : preferred_base + image_size > width_limit;
  if (preferred_base == 0 || preferred_base % 65536 != 0 || image_size == 0
      || image_size % section_alignment != 0 || header_size == 0
      || header_size % file_alignment != 0 || header_size > image_size || base_exceeds)
    qa_pe_error(QA_PE_STAGE_HEADERS, "invalid image base/size or header size");
  qa_pe_checked_range(0, header_size, len, QA_PE_STAGE_HEADERS);
  uint32_t directory_count = qa_pe_reader_u32(&reader, optional + directory_offset - 4);
  if (directory_count > 16 || directory_offset + (size_t)directory_count * 8 > optional_size)
    qa_pe_error(QA_PE_STAGE_HEADERS, "unsupported/truncated data directory array");
  for (int i = 0; i < 16; i++) {
    uint32_t rva = (uint32_t)i < directory_count
        ? qa_pe_reader_u32(&reader, optional + directory_offset + (size_t)i * 8) : 0;
    uint32_t byte_length = (uint32_t)i < directory_count
        ? qa_pe_reader_u32(&reader, optional + directory_offset + (size_t)i * 8 + 4) : 0;
    if ((rva == 0) != (byte_length == 0) && i != 8)
      qa_pe_error(QA_PE_STAGE_HEADERS, "inconsistent data directory %d", i);
    qa_pe_checked_range(rva, byte_length, i == 4 ? len : image_size, QA_PE_STAGE_HEADERS);
    out->directories[i] = (qa_pe_directory_entry){rva, byte_length};
  }
  qa_pe_section *sections = xcalloc(section_count, sizeof(qa_pe_section));
  size_t table = optional + optional_size;
  qa_pe_checked_range(table, (size_t)section_count * 40, header_size, QA_PE_STAGE_HEADERS);
  uint32_t end = align32(header_size, section_alignment);
  for (int i = 0; i < section_count; i++) {
    size_t at = table + (size_t)i * 40;
    size_t name_len = 0;
    while (name_len < 8 && bytes[at + name_len] != 0) name_len++;
    char *name = xmalloc(name_len + 1);
    memcpy(name, bytes + at, name_len);
    name[name_len] = '\0';
    uint32_t virtual_size = qa_pe_reader_u32(&reader, at + 8);
    uint32_t rva = qa_pe_reader_u32(&reader, at + 12);
    uint32_t raw_size = qa_pe_reader_u32(&reader, at + 16);
    uint32_t raw_offset = qa_pe_reader_u32(&reader, at + 20);
    uint32_t flags = qa_pe_reader_u32(&reader, at + 36);
    uint32_t mapped_size = align32(virtual_size > raw_size ? virtual_size : raw_size, section_alignment);
    if (rva % section_alignment != 0 || rva < end
        || (raw_size > 0 && (raw_offset < header_size || raw_offset % file_alignment != 0
                             || raw_size % file_alignment != 0)))
      qa_pe_error(QA_PE_STAGE_HEADERS, "overlapping/misaligned section %s", name);
    qa_pe_checked_range(raw_offset, raw_size, len, QA_PE_STAGE_HEADERS);
    qa_pe_checked_range(rva, mapped_size, image_size, QA_PE_STAGE_HEADERS);
    end = rva + mapped_size;
    sections[i] = (qa_pe_section){name, rva, virtual_size, mapped_size, raw_offset, raw_size,
                                   flags, section_permissions(flags)};
  }
  out->abi = abi;
  out->preferred_base = preferred_base;
  out->entry_point_rva = entry_point_rva;
  out->image_size = image_size;
  out->header_size = header_size;
  out->section_alignment = section_alignment;
  out->file_alignment = file_alignment;
  out->characteristics = characteristics;
  out->dll_characteristics = qa_pe_reader_u16(&reader, optional + 70);
  out->sections = sections;
  out->section_count = section_count;
}

void qa_pe_file_free(qa_pe_file *file) {
  if (file == NULL) return;
  for (size_t i = 0; i < file->section_count; i++) free(file->sections[i].name);
  free(file->sections);
  memset(file, 0, sizeof *file);
}

qa_pe_directory_entry qa_pe_directory(const qa_pe_file *file, int index) {
  if (index < 0 || index >= 16) qa_pe_error(QA_PE_STAGE_HEADERS, "invalid directory index %d", index);
  return file->directories[index];
}
