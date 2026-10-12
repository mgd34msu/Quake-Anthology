#ifndef QA_ID_INDEX_H
#define QA_ID_INDEX_H
#include "qa/strings.h"
#include "qa/arena.h"

typedef struct qa_id_index_entry {
    qa_string_id key;
    uint64_t discriminator;
    size_t value, next;
} qa_id_index_entry;
typedef struct qa_id_index {
    qa_id_index_entry *entries;
    size_t *buckets;
    size_t count, capacity;
    qa_arena *storage;
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
/* Removes matching entries, preserving remaining admission order. Mutating
 * the index invalidates cursors and pointers into its entries. */
bool qa_id_index_remove(qa_id_index *, qa_string_id, uint64_t discriminator);
bool qa_id_index_remove_value(qa_id_index *, qa_string_id, uint64_t discriminator, size_t value);
/* Keeps selected entries in admission order, rebuilding buckets once without
 * allocating. Invalidates cursors and entry pointers. Returns entries removed. */
size_t qa_id_index_retain(qa_id_index *,
    bool (*keep)(void *, const qa_id_index_entry *), void *context);
void qa_id_index_clear(qa_id_index *);
void qa_id_index_destroy(qa_id_index *);
#endif
