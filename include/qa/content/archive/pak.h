/*
 * Port of src/content/archive/pak.ts: id Software PACK header and
 * ordered 64-byte directory. Directory results are malloc'd entry
 * arrays (caller frees each path/raw_path and the array, or hands
 * them to a qa_archive_handle). False only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_PAK_H
#define QA_CONTENT_ARCHIVE_PAK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/archive/types.h"

typedef struct qa_pak_header {
  int64_t offset;
  int64_t byte_length;
} qa_pak_header;

qa_pak_header qa_read_pak_header(const uint8_t *bytes, size_t length,
                                 const char *source, int64_t file_size);
bool qa_read_pak_directory(const uint8_t *bytes, size_t length,
                           const char *source, int64_t file_size,
                           qa_archive_entry **out_entries, size_t *out_count);

#endif
