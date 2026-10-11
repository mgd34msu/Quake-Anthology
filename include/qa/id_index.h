#ifndef QA_ID_INDEX_H
#define QA_ID_INDEX_H
#include "qa/strings.h"

typedef struct qa_id_index_entry {
    qa_string_id key;
    uint64_t discriminator;
    size_t value, next;
} qa_id_index_entry;
typedef struct qa_id_index {
    qa_id_index_entry *entries;
    size_t *buckets;
    size_t count, capacity;
} qa_id_index;
typedef struct qa_id_index_cursor {
    qa_string_id key;
    uint64_t discriminator;
    size_t next;
} qa_id_index_cursor;

bool qa_id_index_reserve(qa_id_index *, size_t additional, qa_error *);
/* Reserve before inserting. Duplicate keys retain all values for iteration. */
void qa_id_index_insert(qa_id_index *, qa_string_id, uint64_t discriminator, size_t value);
qa_id_index_cursor qa_id_index_begin(const qa_id_index *, qa_string_id, uint64_t discriminator);
qa_id_index_entry *qa_id_index_find(qa_id_index *, qa_string_id, uint64_t discriminator);
bool qa_id_index_next(const qa_id_index *, qa_id_index_cursor *, size_t *value);
void qa_id_index_clear(qa_id_index *);
void qa_id_index_destroy(qa_id_index *);
#endif
