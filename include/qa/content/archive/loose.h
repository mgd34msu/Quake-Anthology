/*
 * Port of src/content/archive/loose.ts: loose file reads confined to
 * a root (realpath-resolved, as the TS realpathSync). The TS async
 * read is synchronous in C. Open returns false only on OOM; unsafe
 * paths, escapes, and fs errors abort as the TS throws.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ARCHIVE_LOOSE_H
#define QA_CONTENT_ARCHIVE_LOOSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/archive/source.h"

typedef struct qa_loose_entry_handle {
  char *path;   /* owned: normalized member path */
  char *source; /* owned: resolved native path */
  int64_t byte_length;
  qa_archive_source storage; /* retained descriptor */
} qa_loose_entry_handle;

bool qa_open_loose_entry(const char *root_path, const char *member_path,
                         qa_loose_entry_handle *out);
/* Malloc'd full file bytes; NULL only on OOM (0-length may be NULL). */
uint8_t *qa_loose_entry_read(qa_loose_entry_handle *entry, size_t *out_length);
void qa_loose_entry_close(qa_loose_entry_handle *entry);
/* Close plus free owned strings. */
void qa_loose_entry_destroy(qa_loose_entry_handle *entry);

/* TS readLooseEntry: open, read all, close. */
uint8_t *qa_read_loose_entry(const char *root_path, const char *member_path,
                             size_t *out_length);

#endif
