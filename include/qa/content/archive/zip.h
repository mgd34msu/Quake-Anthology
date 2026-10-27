/*
 * Port of src/content/archive/zip.ts: ZIP central/local header
 * validation and STORE/DEFLATE extraction. DEFLATE uses zlib raw
 * inflate (-lz, already in LDLIBS); failure messages come from zlib,
 * approximating Node's wrapped message inside "DEFLATE failed: ...".
 * CRC32 is zlib's, matching Bun.hash.crc32. Directory results are
 * malloc'd entry arrays; false/NULL only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_ZIP_H
#define QA_CONTENT_ARCHIVE_ZIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/archive/types.h"

#define QA_ZIP_TAIL_LENGTH (22 + 0xffff)

typedef struct qa_zip_directory {
  int64_t offset;
  int64_t byte_length;
  int64_t entry_count;
  int64_t prefix_length;
} qa_zip_directory;

typedef struct qa_zip_local_header {
  int64_t name_length;
  int64_t byte_length; /* name + extra */
} qa_zip_local_header;

qa_zip_directory qa_read_zip_tail(const uint8_t *tail, size_t tail_length,
                                  const char *source, int64_t file_size);
/* `format` is any ArchiveFormat except "pak" (TS Exclude). */
bool qa_read_zip_directory(const uint8_t *bytes, size_t length,
                           const char *source, qa_zip_directory directory,
                           qa_archive_format format,
                           qa_archive_entry **out_entries, size_t *out_count);
qa_zip_local_header qa_read_zip_local_header(const uint8_t *bytes,
                                             size_t length, const char *source,
                                             const qa_archive_entry *entry);
int64_t qa_zip_data_offset(const uint8_t *variable, size_t variable_length,
                           int64_t name_length, const char *source,
                           int64_t central_offset,
                           const qa_archive_entry *entry);
/* Malloc'd decoded bytes (a copy even for stored entries); NULL only
 * on OOM (decoded length 0 may return NULL). *out_length set. */
uint8_t *qa_decode_zip_entry(const uint8_t *compressed, size_t compressed_length,
                             const char *source, int64_t data_offset,
                             const qa_archive_entry *entry, size_t *out_length);

#endif
