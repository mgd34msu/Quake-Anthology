#include "internal.h"

char *native_strdup(const char *text, qa_error *error) {
    if (!text)
        text = "";
    size_t length = strlen(text);
    if (length == SIZE_MAX)
        return native_fail(error, QA_ERROR_MEMORY, 0, "native string length overflow"), NULL;
    char *copy = malloc(length + 1u);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native string");
        return NULL;
    }
    memcpy(copy, text, length + 1u);
    return copy;
}

bool native_size_add(size_t left, size_t right, size_t *out) {
    if (right > SIZE_MAX - left)
        return false;
    *out = left + right;
    return true;
}

bool native_size_multiply(size_t left, size_t right, size_t *out) {
    if (left && right > SIZE_MAX / left)
        return false;
    *out = left * right;
    return true;
}

bool native_copy_bytes(qa_bytes source, qa_buffer *out, qa_error *error) {
    if ((!source.data && source.size) || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native byte source and output are required");
    qa_buffer copy = {0};
    if (source.size) {
        copy.data = malloc(source.size);
        if (!copy.data)
            return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native byte copy");
        memcpy(copy.data, source.data, source.size);
        copy.size = source.size;
    }
    *out = copy;
    return true;
}

bool qa_native_module_load(qa_bytes image, const char *source, qa_native_profile profile,
                           qa_native_module **out, qa_error *error) {
    if (!out || !source || (!image.data && image.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native module source, bytes and output are required");
    qa_native_image_info inspected;
    if (!qa_native_inspect(image, &inspected, error))
        return false;
    const native_profile_spec *spec = native_profile(profile);
    if (!spec)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "unknown native API profile");
    if (!native_profile_accepts(spec, &inspected, error))
        return false;
    qa_native_module *module = calloc(1, sizeof(*module));
    if (!module)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native module");
    module->bytes = malloc(image.size ? image.size : 1u);
    module->source = native_strdup(source, error);
    if (!module->bytes || !module->source) {
        if (!module->bytes && module->source)
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating native artifact bytes");
        free(module->bytes);
        free(module->source);
        free(module);
        return false;
    }
    if (image.size)
        memcpy(module->bytes, image.data, image.size);
    module->size = image.size;
    module->references = 1;
    module->info =
        (qa_native_module_info){.profile = profile, .image = inspected, .source = module->source};
    *out = module;
    return true;
}

void qa_native_module_retain(qa_native_module *module) {
    if (module && module->references != SIZE_MAX)
        ++module->references;
}

void qa_native_module_release(qa_native_module *module) {
    if (!module || !module->references || module->references == SIZE_MAX)
        return;
    if (--module->references)
        return;
    free(module->bytes);
    free(module->source);
    memset(module, 0, sizeof(*module));
    free(module);
}

qa_native_module_info qa_native_module_describe(const qa_native_module *module) {
    return module ? module->info : (qa_native_module_info){0};
}

qa_bytes qa_native_module_bytes(const qa_native_module *module) {
    return module ? (qa_bytes){module->bytes, module->size} : (qa_bytes){0};
}
