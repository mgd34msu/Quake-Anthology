#ifndef QA_PERSISTENCE_INTERNAL_H
#define QA_PERSISTENCE_INTERNAL_H

#include "qa/save.h"
#include "qa/network.h"
#include <stdlib.h>
#include <string.h>

struct qa_save_image {
    qa_save_metadata metadata;
    qa_save_record *records;
    size_t count;
};

static inline bool persistence_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

bool persistence_owner_valid(const qa_save_owner *, qa_error *);
bool persistence_owner_set(const qa_save_record *, size_t, qa_error *);

#endif
