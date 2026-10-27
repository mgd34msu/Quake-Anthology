/*
 * Port of src/guest/pe/image.ts: PeImage shapes and the RVA ImageReader
 * over a mapped image buffer. Directory-read strings and TLS/callback
 * arrays are pooled in qa_pe_owned_strings / the image so the loader can
 * release them together. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_PE_IMAGE_H
#define QA_GUEST_PE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/pe/format.h"

typedef struct qa_pe_owned_strings {
  char **items;
  size_t count, cap;
} qa_pe_owned_strings;
char *qa_pe_own_string(qa_pe_owned_strings *pool, char *text);
void qa_pe_owned_strings_free(qa_pe_owned_strings *pool);

typedef struct qa_pe_unwind_record {
  uint32_t begin_rva;
  uint32_t end_rva;
  uint32_t unwind_info_rva;
  int version;
  int flags;
  bool has_handler_rva;
  uint32_t handler_rva;
  bool has_handler_data_rva;
  uint32_t handler_data_rva;
  struct qa_pe_unwind_record *chained; /* owned, NULL = TS null */
  uint8_t *metadata; /* owned */
  size_t metadata_len;
} qa_pe_unwind_record;
void qa_pe_unwind_records_free(qa_pe_unwind_record *records, size_t count);

typedef struct qa_pe_load_configuration {
  qa_guest_address address;
  uint8_t *bytes; /* owned */
  size_t bytes_len;
  bool has_security_cookie, has_guard_check, has_guard_dispatch;
  qa_guest_address security_cookie_address, guard_check_slot, guard_dispatch_slot;
  uint32_t guard_flags;
} qa_pe_load_configuration;

typedef struct qa_pe_image {
  qa_guest_image image;
  qa_pe_file pe; /* owned */
  bool has_tls_index;
  qa_guest_address tls_index_address;
  bool has_load_configuration;
  qa_pe_load_configuration load_configuration;
  qa_pe_unwind_record *unwind_records; /* owned */
  size_t unwind_record_count;
  qa_pe_owned_strings strings; /* import/export/label text */
  uint8_t *mapped_bytes; /* loader image buffer copy */
} qa_pe_image;
void qa_pe_image_free(qa_pe_image *image);

typedef struct qa_pe_image_reader {
  qa_pe_reader reader;
  uint8_t *bytes;
  size_t len;
  const qa_pe_file *pe;
  uint64_t base;
  qa_mapped_guest_memory *memory;
  qa_guest_module_identity module;
  qa_pe_stage stage;
} qa_pe_image_reader;

void qa_pe_image_reader_init(qa_pe_image_reader *reader, uint8_t *bytes, size_t len,
                             const qa_pe_file *pe, uint64_t base,
                             qa_mapped_guest_memory *memory, qa_guest_module_identity module,
                             qa_pe_stage stage);
qa_pe_image_reader qa_pe_image_reader_for_stage(const qa_pe_image_reader *reader, qa_pe_stage stage);
void qa_pe_image_reader_range(const qa_pe_image_reader *reader, uint32_t rva, size_t size);
qa_guest_address qa_pe_image_reader_address(const qa_pe_image_reader *reader, uint32_t rva, size_t size);
uint32_t qa_pe_image_reader_rva(const qa_pe_image_reader *reader, uint64_t va, size_t size);
qa_guest_address qa_pe_image_reader_executable(const qa_pe_image_reader *reader, uint32_t rva);
uint16_t qa_pe_image_reader_u16(const qa_pe_image_reader *reader, uint32_t rva);
uint32_t qa_pe_image_reader_u32(const qa_pe_image_reader *reader, uint32_t rva);
uint64_t qa_pe_image_reader_pointer(const qa_pe_image_reader *reader, uint32_t rva);
/* Malloc'd; caller frees (or pools). end defaults to imageSize via UINT32_MAX+1 sentinel:
 * pass image_size explicitly at TS default call sites. */
char *qa_pe_image_reader_text(const qa_pe_image_reader *reader, uint32_t rva, uint32_t end);
/* Malloc'd copy; caller frees. */
uint8_t *qa_pe_image_reader_copy(const qa_pe_image_reader *reader, uint32_t rva, size_t size);

#endif
