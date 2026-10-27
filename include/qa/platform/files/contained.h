/*
 * Port of src/platform/files/contained.ts: relative paths that stay
 * inside one storage directory, opened through anchored directory
 * descriptors. The TypeScript walks /proc/self/fd paths; openat here
 * anchors each child the same way. Linux only, as in the source.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_FILES_CONTAINED_H
#define QA_PLATFORM_FILES_CONTAINED_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Splits name on '/', aborting with the TypeScript RangeError when it
 * is empty or any part is empty, "." or "..", or the name contains a
 * backslash or colon. (A C string cannot contain the NUL the
 * TypeScript also rejects.) Parts are malloc'd; free with
 * qa_contained_file_parts_free. NULL only on OOM.
 */
char **qa_contained_file_parts(const char *name, size_t *out_count);
void qa_contained_file_parts_free(char **parts, size_t count);

/*
 * Opens the directory holding name under root, creating intermediate
 * directories when create is set. Returns the directory descriptor
 * (caller closes) and a malloc'd leaf name in *out_leaf. Failures
 * print and abort, as the TypeScript throw reaches the crash handler;
 * returns -1 only on OOM.
 */
int qa_open_contained_parent(const char *root, const char *name, bool create,
                             char **out_leaf);

#endif
