/*
 * Port of src/content/mounts/paths.ts: resource-path normalization,
 * root confinement, missing-file classification, and case-resolving
 * content lookup. C accommodations:
 *  - node:path resolve/relative are lexical absolute normalization
 *    (see paths.c); findContentPath's async walk is synchronous and
 *    returns false for the TS null, with *out_path malloc'd on true.
 *  - isMissingFile takes an errno value (the TS inspects error.code).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MOUNTS_PATHS_H
#define QA_CONTENT_MOUNTS_PATHS_H

#include <stdbool.h>

/* TS "exact" | "case-insensitive" (findContentPath comparison and
 * OpenMountOptions.looseComparison). */
typedef enum qa_content_path_comparison {
  QA_CONTENT_PATH_CASE_INSENSITIVE,
  QA_CONTENT_PATH_EXACT
} qa_content_path_comparison;

/* Malloc'd normalized path; NULL only on OOM. Invalid paths abort
 * with the exact TS RangeError. */
char *qa_normalize_resource_path(const char *path);
/* Malloc'd absolute destination inside root; NULL only on OOM. */
char *qa_path_within_root(const char *root, const char *path);

bool qa_is_missing_file(int error_number);

/* False is the TS null (missing). Ambiguous spellings and symlink
 * escapes abort with the exact TS Error messages. */
bool qa_find_content_path(const char *root, const char *path,
                          qa_content_path_comparison comparison,
                          char **out_path);

#endif
