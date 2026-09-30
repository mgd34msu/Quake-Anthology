#ifndef QA_PLAYER_SERVICES_SAVE_IO_H
#define QA_PLAYER_SERVICES_SAVE_IO_H
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

static inline bool ps_fail(qa_source_save_io *io, qa_status status, const char *message) {
    if (!io->failed) qa_error_set(io->error, status, io->offset, "%s", message);
    io->failed = true;
    return false;
}
static inline size_t ps_remaining(const qa_source_save_io *io) {
    return io->direction == QA_SOURCE_SAVE_READ && io->offset <= io->input.size
               ? io->input.size - io->offset : SIZE_MAX;
}
static inline bool ps_count(qa_source_save_io *io, size_t *count, size_t minimum,
                            size_t element) {
    uint64_t n = *count;
    if (!qa_source_save_u64(io, &n) || n > SIZE_MAX / element ||
        (io->direction == QA_SOURCE_SAVE_READ && n > ps_remaining(io) / minimum))
        return ps_fail(io, QA_ERROR_FORMAT, "Player service allocation exceeds its record");
    *count = (size_t)n;
    return true;
}
static inline bool ps_text(qa_source_save_io *io, char **text) {
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *text = NULL; return true; }
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!ps_count(io, &length, 1, 1) || length == SIZE_MAX)
        return ps_fail(io, QA_ERROR_FORMAT, "Player service text exceeds its record");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length && memchr(io->input.data + io->offset, 0, length))
            return ps_fail(io, QA_ERROR_FORMAT, "Player service text contains NUL");
        *text = malloc(length + 1);
        if (!*text) return ps_fail(io, QA_ERROR_MEMORY, "Allocating player service text");
        (*text)[length] = 0;
    }
    return qa_source_save_bytes(io, *text, length);
}
static inline bool ps_blob(qa_source_save_io *io, qa_buffer *buffer) {
    size_t length = buffer->size;
    if (!ps_count(io, &length, 1, 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        buffer->data = length ? malloc(length) : NULL;
        buffer->size = length;
        if (length && !buffer->data)
            return ps_fail(io, QA_ERROR_MEMORY, "Allocating player service binding");
    }
    return qa_source_save_bytes(io, buffer->data, length);
}
static inline bool ps_magic(qa_source_save_io *io, const uint8_t expected[8]) {
    uint8_t magic[8];
    memcpy(magic, expected, sizeof(magic));
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
           (!memcmp(magic, expected, sizeof(magic)) ||
            ps_fail(io, QA_ERROR_FORMAT, "Invalid player service checkpoint signature"));
}
#endif
