/*
 * Port of src/content/archive/index.ts (barrel + OpenArchive /
 * DecodedArchive, decodeArchive, openArchive, openArchiveSource,
 * readEntry). C accommodations:
 *  - ArchiveHandle and MemoryArchiveHandle are one heap
 *    qa_archive_handle; read_entry_sync works on either source kind
 *    (the TS exposes it only on memory handles).
 *  - Async reads are synchronous. Returned bytes are malloc'd;
 *    NULL only on OOM (0-length may return NULL). *out_length set.
 *  - findEntries fills a caller buffer of entry pointers and returns
 *    the total match count (the TS lazily indexed Map is a linear
 *    scan here; results and order are identical).
 *  - The optional TS format argument becomes has_format + format.
 *    close() closes the retained source; destroy also frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_H
#define QA_CONTENT_ARCHIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/archive/loose.h"
#include "qa/content/archive/pak.h"
#include "qa/content/archive/source.h"
#include "qa/content/archive/types.h"
#include "qa/content/archive/zip.h"

typedef struct qa_archive_handle {
  qa_archive_format format;
  const char *source; /* borrowed from storage */
  int64_t byte_length;
  qa_archive_entry *entries; /* owned */
  size_t entry_count;
  qa_archive_source storage; /* retained source */
  int64_t central_offset;
} qa_archive_handle;
typedef qa_archive_handle qa_memory_archive_handle;

/* Copies the input. `source` NULL means "<memory>". */
qa_archive_handle *qa_decode_archive(const uint8_t *bytes, size_t length,
                                     bool has_format, qa_archive_format format,
                                     const char *source);
/* Reads only header/tail and directory; payloads on demand. */
qa_archive_handle *qa_open_archive(const char *path, bool has_format,
                                   qa_archive_format format);
/* Takes ownership of *storage, including on parse failure. */
qa_archive_handle *qa_open_archive_source(qa_archive_source *storage,
                                          bool has_format,
                                          qa_archive_format format);

size_t qa_archive_find_entries(const qa_archive_handle *archive,
                               const char *path,
                               qa_archive_path_comparison comparison,
                               const qa_archive_entry **out, size_t capacity);

uint8_t *qa_archive_read_entry_ordinal(qa_archive_handle *archive,
                                       int64_t ordinal, size_t *out_length);
uint8_t *qa_archive_read_entry_entry(qa_archive_handle *archive,
                                     const qa_archive_entry *entry,
                                     size_t *out_length);
uint8_t *qa_archive_read_entry_sync_ordinal(qa_archive_handle *archive,
                                            int64_t ordinal, size_t *out_length);
uint8_t *qa_archive_read_entry_sync_entry(qa_archive_handle *archive,
                                          const qa_archive_entry *entry,
                                          size_t *out_length);
/* TS readEntry(archive, entry) free function (ordinal form). */
uint8_t *qa_read_entry(qa_archive_handle *archive, int64_t ordinal,
                       size_t *out_length);

void qa_archive_close(qa_archive_handle *archive);
void qa_archive_destroy(qa_archive_handle *archive);

#endif
