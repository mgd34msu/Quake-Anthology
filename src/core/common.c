#include "qa/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void qa_error_set(qa_error *error, qa_status code, size_t offset,
                  const char *format, ...)
{
    if (error == NULL) {
        return;
    }
    error->code = code;
    error->offset = offset;
    error->message[0] = '\0';
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        int written = vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
        if (written < 0) {
            error->message[0] = '\0';
        }
    }
}

void qa_buffer_free(qa_buffer *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        *buffer = (qa_buffer){0};
    }
}
