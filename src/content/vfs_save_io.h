#ifndef QA_CONTENT_VFS_SAVE_IO_H
#define QA_CONTENT_VFS_SAVE_IO_H
#include "qa/binary.h"
#include "qa/filesystem.h"
#include <stdlib.h>
#include <string.h>

typedef struct vfs_save_io {
    bool reading;
    qa_bytes input;
    qa_buffer output;
    size_t position, capacity;
    qa_error *error;
} vfs_save_io;

static inline bool vfs_save_fail(vfs_save_io *io, qa_status status, const char *message)
{
    qa_error_set(io->error, status, io->position, "%s", message);
    return false;
}
static inline bool vfs_save_bytes(vfs_save_io *io, void *data, size_t size)
{
    if (io->reading) {
        if (size > io->input.size - io->position)
            return vfs_save_fail(io, QA_ERROR_FORMAT, "truncated VFS checkpoint");
        if (size) memcpy(data, io->input.data + io->position, size);
    } else {
        if (size > SIZE_MAX - io->position)
            return vfs_save_fail(io, QA_ERROR_MEMORY, "VFS checkpoint size overflow");
        size_t required = io->position + size;
        if (required > io->capacity) {
            size_t capacity = io->capacity ? io->capacity : 256;
            while (capacity < required) {
                if (capacity > SIZE_MAX / 2) { capacity = required; break; }
                capacity *= 2;
            }
            void *grown = realloc(io->output.data, capacity);
            if (!grown) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS checkpoint");
            io->output.data = grown;
            io->capacity = capacity;
        }
        if (size) memcpy(io->output.data + io->position, data, size);
        io->output.size = required;
    }
    io->position += size;
    return true;
}
static inline bool vfs_save_u64(vfs_save_io *io, uint64_t *value)
{
    uint8_t bytes[8];
    if (!io->reading) qa_store_u64le(bytes, *value);
    if (!vfs_save_bytes(io, bytes, sizeof(bytes))) return false;
    if (io->reading) *value = qa_load_u64le(bytes);
    return true;
}
static inline bool vfs_save_size(vfs_save_io *io, size_t *value)
{
    uint64_t wide = *value;
    if (!vfs_save_u64(io, &wide)) return false;
    if (wide > SIZE_MAX) return vfs_save_fail(io, QA_ERROR_FORMAT, "VFS checkpoint extent overflow");
    *value = (size_t)wide;
    return true;
}
static inline bool vfs_save_bool(vfs_save_io *io, bool *value)
{
    uint8_t bit = *value;
    if (!vfs_save_bytes(io, &bit, 1)) return false;
    if (bit > 1) return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS checkpoint boolean");
    *value = bit != 0;
    return true;
}
static inline bool vfs_save_count(vfs_save_io *io, size_t *count,
                                  size_t minimum_wire, size_t native_width)
{
    if (!vfs_save_size(io, count)) return false;
    if (*count > SIZE_MAX / native_width ||
        (io->reading && *count > (io->input.size - io->position) / minimum_wire))
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS checkpoint count");
    return true;
}
static inline bool vfs_save_buffer(vfs_save_io *io, qa_buffer *buffer)
{
    size_t size = buffer->size;
    if (!vfs_save_count(io, &size, 1, 1)) return false;
    if (io->reading) {
        buffer->data = size ? malloc(size) : NULL;
        if (size && !buffer->data) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS retained bytes");
        buffer->size = size;
    } else if (size && !buffer->data) {
        return vfs_save_fail(io, QA_ERROR_FORMAT, "missing VFS retained bytes");
    }
    return vfs_save_bytes(io, buffer->data, size);
}
static inline bool vfs_save_text(vfs_save_io *io, char **text)
{
    size_t length = io->reading ? 0 : (*text ? strlen(*text) : 0);
    if ((!io->reading && !*text) || !vfs_save_count(io, &length, 1, 1) || length == SIZE_MAX)
        return vfs_save_fail(io, QA_ERROR_FORMAT, "invalid VFS retained text");
    if (io->reading) {
        *text = malloc(length + 1);
        if (!*text) return vfs_save_fail(io, QA_ERROR_MEMORY, "allocating VFS retained text");
    }
    if (!vfs_save_bytes(io, *text, length)) return false;
    if (io->reading) {
        (*text)[length] = 0;
        if (memchr(*text, 0, length)) return vfs_save_fail(io, QA_ERROR_FORMAT, "embedded NUL in VFS retained text");
    }
    return true;
}
static inline bool vfs_save_identity(vfs_save_io *io, qa_fs_identity *identity)
{
    for (size_t i = 0; i < QA_FS_IDENTITY_WORDS; ++i)
        if (!vfs_save_u64(io, &identity->words[i])) return false;
    return true;
}
static inline bool vfs_save_magic(vfs_save_io *io, const uint8_t expected[8])
{
    uint8_t bytes[8];
    if (!io->reading) memcpy(bytes, expected, sizeof(bytes));
    return vfs_save_bytes(io, bytes, sizeof(bytes)) &&
        (memcmp(bytes, expected, sizeof(bytes)) == 0 ||
         vfs_save_fail(io, QA_ERROR_FORMAT, "unsupported VFS checkpoint schema"));
}
static inline bool vfs_save_finish(vfs_save_io *io)
{
    return !io->reading || io->position == io->input.size ||
        vfs_save_fail(io, QA_ERROR_FORMAT, "trailing VFS checkpoint bytes");
}
#endif
