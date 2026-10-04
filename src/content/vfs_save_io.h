#ifndef QA_CONTENT_VFS_SAVE_IO_H
#define QA_CONTENT_VFS_SAVE_IO_H
#include "qa/filesystem.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

static inline bool vfs_save_fail(qa_source_save_io *io, qa_status status, const char *message)
{
    qa_error_set(io->error, status, io->offset, "%s", message);
    return false;
}
static inline bool vfs_save_size(qa_source_save_io *io, size_t *value)
{
    return qa_source_save_count(io, value, SIZE_MAX);
}
static inline bool vfs_save_count(qa_source_save_io *io, size_t *count,
                                  size_t minimum_wire, size_t native_width)
{
    if (!vfs_save_size(io, count)) return false;
    if (*count > SIZE_MAX / native_width ||
        ((io->direction == QA_SOURCE_SAVE_READ) && *count > (io->input.size - io->offset) / minimum_wire))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS checkpoint count");
    return true;
}
static inline bool vfs_save_buffer(qa_source_save_io *io, qa_buffer *buffer)
{
    size_t size = buffer->size;
    if (!vfs_save_count(io, &size, 1, 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        buffer->data = size ? malloc(size) : NULL;
        if (size && !buffer->data) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS retained bytes");
        buffer->size = size;
    } else if (size && !buffer->data) {
        return vfs_save_fail(io, QA_ERROR_FORMAT, "missing VFS retained bytes");
    }
    return qa_source_save_bytes(io, buffer->data, size);
}
static inline bool vfs_save_text(qa_source_save_io *io, char **text)
{
    size_t length = (io->direction == QA_SOURCE_SAVE_READ) ? 0 : (*text ? strlen(*text) : 0);
    if ((io->direction != QA_SOURCE_SAVE_READ && !*text) || !vfs_save_count(io, &length, 1, 1) || length == SIZE_MAX)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS retained text");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *text = malloc(length + 1);
        if (!*text) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS retained text");
    }
    if (!qa_source_save_bytes(io, *text, length)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        (*text)[length] = 0;
        if (memchr(*text, 0, length)) return vfs_save_fail(io, QA_ERROR_FORMAT, "embedded NUL in VFS retained text");
    }
    return true;
}
static inline bool vfs_save_identity(qa_source_save_io *io, qa_fs_identity *identity)
{
    for (size_t i = 0; i < QA_FS_IDENTITY_WORDS; ++i)
        if (!qa_source_save_u64(io, &identity->words[i])) return false;
    return true;
}
static inline bool vfs_save_magic(qa_source_save_io *io, const uint8_t expected[4])
{
    uint8_t bytes[4];
    if (io->direction != QA_SOURCE_SAVE_READ) memcpy(bytes, expected, sizeof(bytes));
    return qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (memcmp(bytes, expected, sizeof(bytes)) == 0 ||
         vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS checkpoint magic"));
}
#endif
