#ifndef QA_COMMON_H
#define QA_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_status {
    QA_OK = 0,
    QA_ERROR_ARGUMENT,
    QA_ERROR_IO,
    QA_ERROR_MEMORY,
    QA_ERROR_FORMAT,
    QA_ERROR_UNSUPPORTED,
    QA_ERROR_NOT_FOUND
} qa_status;

typedef struct qa_error {
    qa_status code;
    size_t offset;
    char message[256];
} qa_error;

typedef struct qa_bytes {
    const uint8_t *data;
    size_t size;
} qa_bytes;

typedef struct qa_buffer {
    uint8_t *data;
    size_t size;
} qa_buffer;

void qa_error_set(qa_error *error, qa_status code, size_t offset,
                  const char *format, ...);
/* Frees owned data and clears the buffer. Passing NULL is permitted. */
void qa_buffer_free(qa_buffer *buffer);
/* Publishes an owned buffer on success; leaves out unchanged on failure.
 * Release any previous out buffer before replacing it. Empty files use NULL. */
bool qa_file_read_all(const char *path, qa_buffer *out, qa_error *error);

typedef struct qa_file_mapping qa_file_mapping;
/* Immutable installed content may borrow mapped bytes through close. The file
 * must not be modified or truncated while retained; replacement by rename is
 * allowed. No heap copy of the file is made, and pages are loaded on demand. */
bool qa_file_map(const char *path, qa_file_mapping **out, qa_error *error);
qa_bytes qa_file_mapping_bytes(const qa_file_mapping *);
void qa_file_mapping_close(qa_file_mapping *);

#endif
