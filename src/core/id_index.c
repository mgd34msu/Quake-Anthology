#include "id_index_internal.h"
#include <stdlib.h>
#include <string.h>

bool qa_id_index_reserve(qa_id_index *index, size_t additional, qa_error *error)
{
    if (additional > SIZE_MAX - index->count) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "ID index exceeds storage"); return false;
    }
    size_t required = index->count + additional;
    if (required <= index->capacity) return true;
    size_t capacity = qa_id_index_capacity_for(required);
    if (!capacity) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "ID index exceeds storage"); return false;
    }
    qa_id_index_entry *entries = index->storage ?
        qa_arena_alloc(index->storage, capacity * sizeof(*entries), _Alignof(qa_id_index_entry), error) :
        malloc(capacity * sizeof(*entries));
    size_t *buckets = index->storage ?
        qa_arena_alloc(index->storage, capacity * 2 * sizeof(*buckets), _Alignof(size_t), error) :
        calloc(capacity * 2, sizeof(*buckets));
    if (!entries || !buckets) {
        if (!index->storage) {
            free(entries); free(buckets);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating ID index");
        }
        return false;
    }
    if (index->storage) memset(buckets, 0, capacity * 2 * sizeof(*buckets));
    if (index->count) memcpy(entries, index->entries, index->count * sizeof(*entries));
    for (size_t i = 0; i < index->count; ++i) {
        size_t slot = qa_id_index_bucket(entries[i].key, entries[i].discriminator, capacity);
        entries[i].next = buckets[slot]; buckets[slot] = i + 1;
    }
    if (!index->storage) { free(index->entries); free(index->buckets); }
    index->entries = entries; index->buckets = buckets; index->capacity = capacity;
    return true;
}
typedef struct index_remove {
    qa_string_id key;
    uint64_t discriminator;
    const size_t *value;
} index_remove;
static bool keep_unmatched(void *opaque, const qa_id_index_entry *entry)
{
    const index_remove *remove = opaque;
    return entry->key != remove->key || entry->discriminator != remove->discriminator ||
        (remove->value && entry->value != *remove->value);
}
static bool remove_entries(qa_id_index *index, qa_string_id key, uint64_t discriminator,
                           const size_t *value)
{
    qa_id_index_cursor cursor = qa_id_index_begin(index, key, discriminator);
    size_t candidate;
    while (qa_id_index_next(index, &cursor, &candidate)) {
        if (value && candidate != *value) continue;
        index_remove remove = {key, discriminator, value};
        return qa_id_index_retain(index, keep_unmatched, &remove) != 0;
    }
    return false;
}
bool qa_id_index_remove(qa_id_index *index, qa_string_id key, uint64_t discriminator)
{
    return remove_entries(index, key, discriminator, NULL);
}
bool qa_id_index_remove_value(qa_id_index *index, qa_string_id key, uint64_t discriminator,
                              size_t value)
{
    return remove_entries(index, key, discriminator, &value);
}
void qa_id_index_destroy(qa_id_index *index)
{
    if (!index->storage) { free(index->entries); free(index->buckets); }
    *index = (qa_id_index){0};
}
