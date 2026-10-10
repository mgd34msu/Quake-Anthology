#ifndef QA_APPLICATION_UNIFIED_FRAME_PRIVATE_H
#define QA_APPLICATION_UNIFIED_FRAME_PRIVATE_H

#include "internal.h"
#include "qa/network_unified_frame_pool.h"
#include "qa/network_unified_frame.h"
#include <stdlib.h>
#include <string.h>

bool application_unified_provider_state(qa_unified_provider_state *, const application_provider *, qa_error *);

static inline void *application_unified_frame_alloc(qa_unified_frame_lease *lease,
    size_t count, size_t stride, qa_error *error)
{
    return lease ? qa_unified_frame_lease_alloc(lease, count, stride, _Alignof(max_align_t), error) : calloc(count, stride);
}

static inline bool application_unified_frame_string(qa_unified_frame_lease *lease, char **out, const char *value,
    qa_error *error)
{
    if (!value) return true;
    size_t length = strlen(value);
    char *copy = lease ? application_unified_frame_alloc(lease, length + 1, 1, error) : malloc(length + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining Unified Source string");
    memcpy(copy, value, length + 1);
    *out = copy;
    return true;
}

#endif
