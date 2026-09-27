#include "qa/common.h"

#include <stdlib.h>
#include <string.h>

bool qa_file_read_all(const char *path, qa_buffer *out, qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "missing file buffer output");
        return false;
    }
    qa_file_mapping *mapping;
    if (!qa_file_map(path, &mapping, error))
        return false;
    qa_bytes source = qa_file_mapping_bytes(mapping);
    qa_buffer copy = {.size = source.size};
    if (source.size) {
        copy.data = malloc(source.size);
        if (!copy.data) {
            qa_file_mapping_close(mapping);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating file buffer");
            return false;
        }
        memcpy(copy.data, source.data, source.size);
    }
    qa_file_mapping_close(mapping);
    *out = copy;
    return true;
}
