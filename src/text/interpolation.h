#ifndef QA_TEXT_INTERPOLATION_H
#define QA_TEXT_INTERPOLATION_H

#include "qa/localization.h"

enum { QA_INTERPOLATION_ARGUMENT_CAPACITY = 1024 };
typedef enum qa_interpolation_truncation {
    QA_INTERPOLATION_PREFIX_UTF8,
    QA_INTERPOLATION_PIECES_UTF8,
    QA_INTERPOLATION_PIECES_RAW
} qa_interpolation_truncation;
typedef struct qa_text_interpolation {
    const qa_localization_argument *spans;
    size_t span_count;
    const char *const *values;
    size_t value_count;
    void *context;
    qa_bytes (*resolve)(void *, const char *, char [QA_INTERPOLATION_ARGUMENT_CAPACITY]);
    qa_interpolation_truncation truncation;
    bool keep_missing;
} qa_text_interpolation;

/* NULL spans selects classic positional placeholders. Catalog spans retain
 * their load-parsed grammar. NULL output counts the complete result. */
bool qa_text_interpolate(const char *, const qa_text_interpolation *, char *, size_t,
                         size_t *full_size, size_t *written, qa_error *);

#endif
