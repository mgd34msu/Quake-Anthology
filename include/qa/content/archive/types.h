/*
 * Port of src/content/archive/types.ts: archive entry metadata, the
 * ArchiveHandle/MemoryArchiveHandle shapes (declared in archive.h),
 * ArchiveError, checkRange, and entry-path normalization/comparison.
 * C accommodations:
 *  - PakEntry | ZipEntry becomes one qa_archive_entry record; the
 *    format field (qa_archive_format, contracts) discriminates, and
 *    pak-only/zip-only fields are zero for the other format.
 *  - ArchiveError aborts printing "ArchiveError: <source>:<offset>:
 *    <message>", the TS Error string form (name "ArchiveError").
 *  - Paths are byte strings (the TS byte-string archives decode each
 *    byte as one code point). "case-insensitive" lowercases ASCII and
 *    Latin-1 uppercase bytes, reproducing toLowerCase for the byte
 *    range the archives carry; UTF-8 text is not case-folded.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_TYPES_H
#define QA_CONTENT_ARCHIVE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/contracts/content.h"

typedef struct qa_archive_entry {
  qa_archive_format format;
  int64_t ordinal;
  char *path;     /* owned */
  char *raw_path; /* owned */
  int64_t byte_length;
  int64_t compressed_size;
  bool is_directory;
  /* pak */
  int64_t compression_method; /* 0 for pak; 0|8 for zip */
  int64_t data_offset;        /* pak */
  /* zip */
  int64_t flags;
  uint32_t crc32;
  int64_t local_header_offset;
} qa_archive_entry;

/* TS ArchivePathComparison = "exact" | "ascii-insensitive" |
 * "case-insensitive". */
typedef enum qa_archive_path_comparison {
  QA_ARCHIVE_PATH_EXACT,
  QA_ARCHIVE_PATH_ASCII_INSENSITIVE,
  QA_ARCHIVE_PATH_CASE_INSENSITIVE
} qa_archive_path_comparison;

noreturn void qa_archive_error(const char *source, int64_t offset,
                               const char *format, ...);

/* TS checkRange: aborts with ArchiveError when the range is invalid. */
void qa_archive_check_range(const char *source, int64_t size, int64_t offset,
                            int64_t length);

/* Malloc'd normalized path; NULL only on OOM. Unsafe paths abort with
 * the exact TS RangeError (`Unsafe archive member path: <JSON>`). */
char *qa_normalize_entry_path(const char *path);
/* Malloc'd comparison key (normalized, then folded); NULL only on OOM. */
char *qa_compare_entry_path(const char *path,
                            qa_archive_path_comparison comparison);

#endif
