/*
 * Port of src/content/archive/source.ts: MemorySource and FileSource.
 * One qa_archive_source record holds either kind ("memory"/"file").
 * The TS async read becomes synchronous (pread at explicit positions,
 * retaining the descriptor and verifying size/mtime/ctime, as the TS
 * verifies with bigint fstat). Init returns false only on OOM; the
 * TS constructor's fs errors and ArchiveError abort, as elsewhere.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_SOURCE_H
#define QA_CONTENT_ARCHIVE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_archive_source_kind {
  QA_ARCHIVE_SOURCE_MEMORY, /* TS kind "memory" */
  QA_ARCHIVE_SOURCE_FILE    /* TS kind "file" */
} qa_archive_source_kind;

typedef struct qa_archive_source {
  qa_archive_source_kind kind;
  char *source; /* owned */
  int64_t byte_length;
  uint8_t *bytes; /* owned copy (memory) */
  int descriptor; /* file; -1 when closed/unopened */
  int64_t modified_ns;
  int64_t changed_ns;
  bool closed;
  int active_reads;
} qa_archive_source;

/* TS ArchiveSource = MemorySource | FileSource. */
typedef qa_archive_source qa_archive_storage;

/* `source` NULL means "<memory>". Copies the bytes. */
bool qa_memory_source_init(qa_archive_source *out, const uint8_t *bytes,
                           size_t length, const char *source);
/* Opens and retains the descriptor (see header note on errors). */
bool qa_file_source_init(qa_archive_source *out, const char *source);

/* Malloc'd copy of the range; NULL only on OOM (length 0 may return
 * NULL). *out_length set when non-NULL. Closed/range/changed errors
 * abort with the exact TS ArchiveError messages. */
uint8_t *qa_archive_source_read_sync(qa_archive_source *storage,
                                     int64_t offset, int64_t length,
                                     size_t *out_length);
/* TS async read, synchronous in C: identical to read_sync. */
uint8_t *qa_archive_source_read(qa_archive_source *storage, int64_t offset,
                                int64_t length, size_t *out_length);

void qa_archive_source_close(qa_archive_source *storage);
/* Frees owned storage after close (safe to call once; closes first). */
void qa_archive_source_destroy(qa_archive_source *storage);

#endif
