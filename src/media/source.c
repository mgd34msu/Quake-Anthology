#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "qa/media.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct qa_media_input {
    size_t references;
    uint64_t size;
    int descriptor;
    qa_bytes memory;
    void *lease;
    void (*release)(void *);
};

bool qa_media_input_file(const char *path, qa_media_input **out, qa_error *error) {
    if (!path || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing media path or output"); return false; }
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) { qa_error_set(error, QA_ERROR_IO, 0, "Opening media: %s", strerror(errno)); return false; }
    struct stat info;
    if (fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
        close(descriptor); qa_error_set(error, QA_ERROR_IO, 0, "Media source is not a readable regular file"); return false;
    }
    qa_media_input *input = calloc(1, sizeof(*input));
    if (!input) { close(descriptor); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating media source"); return false; }
    input->references = 1; input->descriptor = descriptor; input->size = (uint64_t)info.st_size;
    *out = input; return true;
}

bool qa_media_input_memory(qa_bytes bytes, void *lease, void (*release)(void *),
                            qa_media_input **out, qa_error *error) {
    if (!out || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid media bytes"); return false;
    }
    qa_media_input *input = calloc(1, sizeof(*input));
    if (!input) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating media source"); return false; }
    input->references = 1; input->descriptor = -1; input->size = bytes.size;
    input->memory = bytes; input->lease = lease; input->release = release;
    *out = input; return true;
}
static void resource_release(void *resource) { qa_resource_release(resource); }
bool qa_media_input_resource(qa_resource *resource, qa_media_input **out, qa_error *error) {
    if (!resource || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing media resource or output"); return false; }
    qa_media_input *input;
    if (!qa_media_input_memory(qa_resource_bytes(resource), resource, resource_release, &input, error)) return false;
    qa_resource_retain(resource);
    *out = input; return true;
}
void qa_media_input_retain(qa_media_input *input) { if (input) ++input->references; }
void qa_media_input_release(qa_media_input *input) {
    if (!input || --input->references) return;
    if (input->descriptor >= 0) close(input->descriptor);
    if (input->release) input->release(input->lease);
    free(input);
}
uint64_t qa_media_input_size(const qa_media_input *input) { return input->size; }
bool qa_media_input_read(qa_media_input *input, uint64_t offset, void *destination, size_t length, qa_error *error) {
    if (!input || (length && !destination) || offset > input->size || length > input->size - offset) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Media read exceeds source"); return false;
    }
    if (!length) return true;
    if (input->descriptor < 0) { memcpy(destination, input->memory.data + (size_t)offset, length); return true; }
    size_t read_bytes = 0;
    while (read_bytes < length) {
        size_t wanted = length - read_bytes;
        if (wanted > (size_t)SSIZE_MAX) wanted = (size_t)SSIZE_MAX;
        ssize_t count = pread(input->descriptor, (uint8_t *)destination + read_bytes, wanted, (off_t)(offset + read_bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { qa_error_set(error, QA_ERROR_IO, 0, "Short media read"); return false; }
        read_bytes += (size_t)count;
    }
    return true;
}
