#ifndef QA_ID_INDEX_INTERNAL_H
#define QA_ID_INDEX_INTERNAL_H

#include "qa/id_index.h"

static inline size_t qa_id_index_bucket(qa_string_id id, uint64_t discriminator, size_t capacity)
{
    return ((size_t)id * UINT32_C(2654435761) + (size_t)discriminator) & (capacity * 2 - 1);
}

#endif
