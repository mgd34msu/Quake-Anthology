#ifndef QA_FILESYSTEM_INTERNAL_H
#define QA_FILESYSTEM_INTERNAL_H

#include "qa/filesystem.h"

bool qa_fs_relative_valid(const char *path, bool allow_root, qa_error *error);
bool qa_fs_listing_add(qa_fs_listing *listing, const char *name,
                       qa_fs_entry_kind kind, qa_error *error);
/* One actual native read; the common reader owns partial progress and guards.
 * Its admitted span is nonempty and fits the signed native offset domain. */
bool qa_fs_file_read_at_native(qa_fs_file *, uint64_t offset, void *, size_t,
                                size_t *received, qa_error *);

#endif
