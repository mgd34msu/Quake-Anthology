#include "qa/id_index.h"
#include <stdlib.h>
#include <string.h>

static size_t bucket(qa_string_id id, uint64_t discriminator, size_t capacity)
{
    return ((size_t)id * UINT32_C(2654435761) + (size_t)discriminator) & (capacity * 2 - 1);
}
bool qa_id_index_reserve(qa_id_index *index, size_t additional, qa_error *error)
{
    size_t maximum = SIZE_MAX / sizeof(*index->entries);
    if (maximum > SIZE_MAX / 2 / sizeof(*index->buckets)) maximum = SIZE_MAX / 2 / sizeof(*index->buckets);
    if (additional > maximum || index->count > maximum - additional) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "ID index exceeds storage"); return false;
    }
    size_t required = index->count + additional;
    if (required <= index->capacity) return true;
    size_t capacity = index->capacity ? index->capacity : 32;
    while (capacity < required) {
        if (capacity > maximum / 2) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "ID index exceeds storage"); return false;
        }
        capacity *= 2;
    }
    qa_id_index_entry *entries = malloc(capacity * sizeof(*entries));
    size_t *buckets = calloc(capacity * 2, sizeof(*buckets));
    if (!entries || !buckets) {
        free(entries); free(buckets);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating ID index"); return false;
    }
    if (index->count) memcpy(entries, index->entries, index->count * sizeof(*entries));
    for (size_t i = 0; i < index->count; ++i) {
        size_t slot = bucket(entries[i].key, entries[i].discriminator, capacity);
        entries[i].next = buckets[slot]; buckets[slot] = i + 1;
    }
    free(index->entries); free(index->buckets);
    index->entries = entries; index->buckets = buckets; index->capacity = capacity;
    return true;
}
void qa_id_index_insert(qa_id_index *index, qa_string_id key, uint64_t discriminator, size_t value)
{
    size_t slot = bucket(key, discriminator, index->capacity);
    index->entries[index->count] = (qa_id_index_entry){key, discriminator, value, index->buckets[slot]};
    index->buckets[slot] = ++index->count;
}
qa_id_index_cursor qa_id_index_begin(const qa_id_index *index, qa_string_id key, uint64_t discriminator)
{
    return (qa_id_index_cursor){key, discriminator,
        index->capacity ? index->buckets[bucket(key, discriminator, index->capacity)] : 0};
}
bool qa_id_index_next(const qa_id_index *index, qa_id_index_cursor *cursor, size_t *value)
{
    while (cursor->next) {
        const qa_id_index_entry *entry = index->entries + cursor->next - 1;
        cursor->next = entry->next;
        if (entry->key == cursor->key && entry->discriminator == cursor->discriminator) {
            *value = entry->value; return true;
        }
    }
    return false;
}
void qa_id_index_clear(qa_id_index *index)
{
    index->count = 0;
    if (index->capacity) memset(index->buckets, 0, index->capacity * 2 * sizeof(*index->buckets));
}
void qa_id_index_destroy(qa_id_index *index)
{
    free(index->entries); free(index->buckets); *index = (qa_id_index){0};
}
