#ifndef QA_PERSISTENCE_LEASE_SERIALS_H
#define QA_PERSISTENCE_LEASE_SERIALS_H
#include "qa/common.h"
#include <stdlib.h>

static int persistence_serial_compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return a < b ? -1 : a > b;
}

static bool persistence_serial_append(uint64_t **values, size_t *count, size_t *capacity,
                                       uint64_t serial, uint64_t last, qa_error *error)
{
    if (!serial || serial > last) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Saved lease serial exceeds its owner namespace"); return false;
    }
    if (*count == *capacity) {
        size_t limit = SIZE_MAX / sizeof(**values);
        size_t next = *capacity > limit / 2 ? limit : *capacity ? *capacity * 2 : 16;
        if (next > limit || next <= *count) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Saved lease serial extent overflow"); return false;
        }
        uint64_t *grown = realloc(*values, next * sizeof(**values));
        if (!grown) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating saved lease identities"); return false; }
        *values = grown; *capacity = next;
    }
    (*values)[(*count)++] = serial; return true;
}

static bool persistence_serial_unique(uint64_t *values, size_t count, qa_error *error)
{
    if (count > 1) qsort(values, count, sizeof(*values), persistence_serial_compare);
    for (size_t i = 1; i < count; ++i) if (values[i - 1] == values[i]) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Saved owner has duplicate lease identities"); return false;
    }
    return true;
}
#endif
