#include "internal.h"
#include <stdlib.h>

void qac_document_free(const qa_console_documentation *doc) {
    if (!doc)
        return;
    free((char *)doc->usage);
    for (size_t i = 0; i < doc->example_count; ++i)
        free((char *)doc->examples[i]);
    for (size_t i = 0; i < doc->allowed_count; ++i)
        free((char *)doc->allowed_values[i]);
    free((void *)doc->examples);
    free((void *)doc->allowed_values);
    free((void *)doc);
}
static bool copy_list(const char *const *source, size_t count, const char *const **out,
                      qa_error *error) {
    if (!count) {
        *out = NULL;
        return true;
    }
    if (!source || count > SIZE_MAX / sizeof(char *))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid documentation list");
    char **list = calloc(count, sizeof(*list));
    if (!list)
        return qac_fail(error, QA_ERROR_MEMORY, "allocating documentation list");
    for (size_t i = 0; i < count; ++i) {
        if (!source[i] || !(list[i] = qac_copy(source[i], error))) {
            if (!source[i])
                qac_fail(error, QA_ERROR_ARGUMENT, "null documentation text");
            for (size_t j = 0; j < i; ++j)
                free(list[j]);
            free(list);
            return false;
        }
    }
    *out = (const char *const *)list;
    return true;
}
bool qac_document_replace(const qa_console_documentation **out,
                          const qa_console_documentation *source, qa_error *error) {
    if (!source) {
        qac_document_free(*out);
        *out = NULL;
        return true;
    }
    qa_console_documentation *doc = calloc(1, sizeof(*doc));
    if (!doc)
        return qac_fail(error, QA_ERROR_MEMORY, "allocating command documentation");
    if (source->usage && !(doc->usage = qac_copy(source->usage, error)))
        goto fail;
    if (!copy_list(source->examples, source->example_count, &doc->examples, error))
        goto fail;
    doc->example_count = source->example_count;
    if (!copy_list(source->allowed_values, source->allowed_count, &doc->allowed_values, error))
        goto fail;
    doc->allowed_count = source->allowed_count;
    doc->has_allowed_values = source->has_allowed_values;
    qac_document_free(*out);
    *out = doc;
    return true;
fail:
    qac_document_free(doc);
    return false;
}
