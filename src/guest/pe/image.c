/* Port of src/guest/pe/image.ts. GPL-2.0-or-later. */
#include "qa/guest/pe/image.h"

#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_pe_error(QA_PE_STAGE_HEADERS, "out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_pe_error(QA_PE_STAGE_HEADERS, "out of memory");
  return next;
}

char *qa_pe_own_string(qa_pe_owned_strings *pool, char *text) {
  if (pool->count == pool->cap) {
    pool->cap = pool->cap == 0 ? 16 : pool->cap * 2;
    pool->items = xrealloc(pool->items, pool->cap * sizeof *pool->items);
  }
  pool->items[pool->count++] = text;
  return text;
}
void qa_pe_owned_strings_free(qa_pe_owned_strings *pool) {
  for (size_t i = 0; i < pool->count; i++) free(pool->items[i]);
  free(pool->items);
  memset(pool, 0, sizeof *pool);
}

static void unwind_record_free(qa_pe_unwind_record *record) {
  if (record->chained != NULL) {
    unwind_record_free(record->chained);
    free(record->chained);
  }
  free(record->metadata);
}
void qa_pe_unwind_records_free(qa_pe_unwind_record *records, size_t count) {
  for (size_t i = 0; i < count; i++) unwind_record_free(&records[i]);
  free(records);
}

void qa_pe_image_free(qa_pe_image *image) {
  if (image == NULL) return;
  qa_guest_image_free(&image->image);
  qa_pe_file_free(&image->pe);
  free(image->load_configuration.bytes);
  qa_pe_unwind_records_free(image->unwind_records, image->unwind_record_count);
  qa_pe_owned_strings_free(&image->strings);
  free(image->mapped_bytes);
  memset(image, 0, sizeof *image);
}

void qa_pe_image_reader_init(qa_pe_image_reader *reader, uint8_t *bytes, size_t len,
                             const qa_pe_file *pe, uint64_t base,
                             qa_mapped_guest_memory *memory, qa_guest_module_identity module,
                             qa_pe_stage stage) {
  reader->reader = (qa_pe_reader){bytes, len, stage};
  reader->bytes = bytes; reader->len = len;
  reader->pe = pe; reader->base = base;
  reader->memory = memory; reader->module = module; reader->stage = stage;
}
qa_pe_image_reader qa_pe_image_reader_for_stage(const qa_pe_image_reader *reader, qa_pe_stage stage) {
  qa_pe_image_reader copy = *reader;
  copy.stage = stage;
  copy.reader.stage = stage;
  return copy;
}
void qa_pe_image_reader_range(const qa_pe_image_reader *reader, uint32_t rva, size_t size) {
  qa_pe_checked_range(rva, size, reader->len, reader->stage);
  if (size == 0) return;
  uint64_t at = rva, finish = (uint64_t)rva + size;
  if (at < reader->pe->header_size)
    at = finish < reader->pe->header_size ? finish : reader->pe->header_size;
  for (size_t i = 0; i < reader->pe->section_count; i++) {
    const qa_pe_section *section = &reader->pe->sections[i];
    uint64_t section_end = (uint64_t)section->rva + section->mapped_size;
    if (at >= section->rva && at < section_end) at = finish < section_end ? finish : section_end;
    if (at == finish) return;
  }
  if (at != finish)
    qa_pe_error(reader->stage, "RVA 0x%x crosses an unmapped image gap", rva);
}
qa_guest_address qa_pe_image_reader_address(const qa_pe_image_reader *reader, uint32_t rva, size_t size) {
  qa_pe_image_reader_range(reader, rva, size);
  qa_guest_address address = {0};
  if (!qa_memory_pointer(reader->memory, reader->base + rva, &address))
    qa_pe_error(reader->stage, "null image address");
  return address;
}
uint32_t qa_pe_image_reader_rva(const qa_pe_image_reader *reader, uint64_t va, size_t size) {
  if (va < reader->base || va - reader->base > reader->pe->image_size)
    qa_pe_error(reader->stage, "VA 0x%llx is outside the image", (unsigned long long)va);
  uint32_t rva = (uint32_t)(va - reader->base);
  qa_pe_image_reader_range(reader, rva, size);
  return rva;
}
qa_guest_address qa_pe_image_reader_executable(const qa_pe_image_reader *reader, uint32_t rva) {
  const qa_pe_section *found = NULL;
  for (size_t i = 0; i < reader->pe->section_count; i++) {
    const qa_pe_section *section = &reader->pe->sections[i];
    if (rva >= section->rva && (uint64_t)rva < (uint64_t)section->rva + section->mapped_size) found = section;
  }
  if (found == NULL || !qa_guest_permissions_has_execute(found->permissions))
    qa_pe_error(reader->stage, "RVA 0x%x is not executable", rva);
  return qa_pe_image_reader_address(reader, rva, 1);
}
uint16_t qa_pe_image_reader_u16(const qa_pe_image_reader *reader, uint32_t rva) {
  qa_pe_image_reader_range(reader, rva, 2);
  return qa_pe_reader_u16(&reader->reader, rva);
}
uint32_t qa_pe_image_reader_u32(const qa_pe_image_reader *reader, uint32_t rva) {
  qa_pe_image_reader_range(reader, rva, 4);
  return qa_pe_reader_u32(&reader->reader, rva);
}
uint64_t qa_pe_image_reader_pointer(const qa_pe_image_reader *reader, uint32_t rva) {
  qa_pe_image_reader_range(reader, rva, (size_t)reader->pe->abi.pointer_bytes);
  return reader->pe->abi.pointer_bytes == 4 ? qa_pe_reader_u32(&reader->reader, rva)
                                             : qa_pe_reader_u64(&reader->reader, rva);
}
char *qa_pe_image_reader_text(const qa_pe_image_reader *reader, uint32_t rva, uint32_t end) {
  qa_pe_image_reader_range(reader, rva, 1);
  const qa_pe_section *found = NULL;
  for (size_t i = 0; i < reader->pe->section_count; i++) {
    const qa_pe_section *section = &reader->pe->sections[i];
    if (rva >= section->rva && (uint64_t)rva < (uint64_t)section->rva + section->mapped_size) found = section;
  }
  uint32_t bound = end;
  uint32_t section_bound = found == NULL ? reader->pe->header_size : found->rva + found->mapped_size;
  if (section_bound < bound) bound = section_bound;
  return qa_pe_reader_text(&reader->reader, rva, bound - rva);
}
uint8_t *qa_pe_image_reader_copy(const qa_pe_image_reader *reader, uint32_t rva, size_t size) {
  qa_pe_image_reader_range(reader, rva, size);
  uint8_t *copy = xmalloc(size);
  memcpy(copy, reader->bytes + rva, size);
  return copy;
}
