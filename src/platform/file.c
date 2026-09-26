#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "qa/common.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool file_stat(int descriptor, struct stat *info)
{
    int result;
    do {
        result = fstat(descriptor, info);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

static bool file_unchanged(const struct stat *before, const struct stat *after)
{
    return before->st_dev == after->st_dev && before->st_ino == after->st_ino &&
           before->st_size == after->st_size &&
           before->st_mtim.tv_sec == after->st_mtim.tv_sec &&
           before->st_mtim.tv_nsec == after->st_mtim.tv_nsec &&
           before->st_ctim.tv_sec == after->st_ctim.tv_sec &&
           before->st_ctim.tv_nsec == after->st_ctim.tv_nsec;
}

bool qa_file_read_all(const char *path, qa_buffer *out, qa_error *error)
{
    if (path == NULL || path[0] == '\0' || out == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "file read requires a path and output buffer");
        return false;
    }
    int descriptor;
    do {
        /* Avoid blocking on a FIFO before the regular-file check. */
        descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        int code = errno;
        qa_error_set(error, code == ENOENT || code == ENOTDIR ? QA_ERROR_NOT_FOUND : QA_ERROR_IO,
                     0, "cannot open %s: %s", path, strerror(code));
        return false;
    }

    qa_buffer buffer = {0};
    struct stat before;
    if (!file_stat(descriptor, &before)) {
        int code = errno;
        qa_error_set(error, QA_ERROR_IO, 0, "cannot inspect %s: %s", path, strerror(code));
        goto fail;
    }
    if (!S_ISREG(before.st_mode)) {
        qa_error_set(error, QA_ERROR_IO, 0, "%s is not a regular file", path);
        goto fail;
    }
    if (before.st_size < 0 || (uintmax_t)before.st_size > (uintmax_t)PTRDIFF_MAX ||
        (uintmax_t)before.st_size > (uintmax_t)SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "%s is too large for a memory buffer", path);
        goto fail;
    }
    buffer.size = (size_t)before.st_size;
    if (buffer.size != 0) {
        buffer.data = malloc(buffer.size);
        if (buffer.data == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate %zu bytes for %s", buffer.size, path);
            goto fail;
        }
    }
    size_t total = 0;
    while (total < buffer.size) {
        size_t count = buffer.size - total;
        if (count > (size_t)SSIZE_MAX) {
            count = (size_t)SSIZE_MAX;
        }
        ssize_t received = read(descriptor, buffer.data + total, count);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            int code = errno;
            qa_error_set(error, QA_ERROR_IO, total, "cannot read %s: %s", path, strerror(code));
            goto fail;
        }
        if (received == 0) {
            qa_error_set(error, QA_ERROR_IO, total,
                         "short read from %s: expected %zu bytes, got %zu", path, buffer.size, total);
            goto fail;
        }
        total += (size_t)received;
    }
    struct stat after;
    if (!file_stat(descriptor, &after)) {
        int code = errno;
        qa_error_set(error, QA_ERROR_IO, total, "cannot inspect %s: %s", path, strerror(code));
        goto fail;
    }
    if (!file_unchanged(&before, &after)) {
        qa_error_set(error, QA_ERROR_IO, total, "%s changed while being read", path);
        goto fail;
    }
    if (close(descriptor) < 0) {
        int code = errno;
        qa_error_set(error, QA_ERROR_IO, total, "cannot close %s: %s", path, strerror(code));
        qa_buffer_free(&buffer);
        return false;
    }
    *out = buffer;
    return true;

fail:
    (void)close(descriptor);
    qa_buffer_free(&buffer);
    return false;
}
