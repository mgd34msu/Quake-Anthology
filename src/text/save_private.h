#ifndef QA_TEXT_SAVE_PRIVATE_H
#define QA_TEXT_SAVE_PRIVATE_H
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
static inline bool qa_text_save_owned(qa_source_save_io *io, char **text)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *text = NULL; return true; }
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, *text, size);
    if (size == SIZE_MAX || size > io->input.size - io->offset || memchr(io->input.data + io->offset, 0, size)) return false;
    char *value = malloc(size + 1);
    if (!value) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring owned caption text"); return false; }
    if (!qa_source_save_bytes(io, value, size)) { free(value); return false; }
    value[size] = 0; *text = value; return true;
}
static inline bool qa_text_save_header(qa_source_save_io *io, const char name[4])
{
    uint8_t magic[4]; memcpy(magic, name, 4);
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, name, 4);
}
#endif
