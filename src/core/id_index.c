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
        size_t slot = bucket(entries[i].key, entries[i].discriminator, capacity);
        entries[i].next = buckets[slot]; buckets[slot] = i + 1;
    }
    if (!index->storage) { free(index->entries); free(index->buckets); }
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
static const qa_id_index_entry *next_entry(const qa_id_index *index, qa_id_index_cursor *cursor)
{
    while (cursor->next) {
        const qa_id_index_entry *entry = index->entries + cursor->next - 1;
        cursor->next = entry->next;
        if (entry->key == cursor->key && entry->discriminator == cursor->discriminator) {
            return entry;
        }
    }
    return NULL;
}
bool qa_id_index_next(const qa_id_index *index, qa_id_index_cursor *cursor, size_t *value)
{
    const qa_id_index_entry *entry = next_entry(index, cursor);
    if (!entry) return false;
    *value = entry->value;
    return true;
}
qa_id_index_entry *qa_id_index_find(qa_id_index *index, qa_string_id key, uint64_t discriminator)
{
    qa_id_index_cursor cursor = qa_id_index_begin(index, key, discriminator);
    return (qa_id_index_entry *)next_entry(index, &cursor);
}
static bool remove_entries(qa_id_index *index, qa_string_id key, uint64_t discriminator,
                           const size_t *value)
{
    qa_id_index_cursor cursor = qa_id_index_begin(index, key, discriminator);
    size_t candidate;
    bool found = false;
    while (qa_id_index_next(index, &cursor, &candidate))
        if (!value || candidate == *value) { found = true; break; }
    if (!found) return false;
    size_t count = 0;
    memset(index->buckets, 0, index->capacity * 2 * sizeof(*index->buckets));
    for (size_t i = 0; i < index->count; ++i) {
        qa_id_index_entry entry = index->entries[i];
        if (entry.key == key && entry.discriminator == discriminator &&
            (!value || entry.value == *value)) continue;
        size_t slot = bucket(entry.key, entry.discriminator, index->capacity);
        entry.next = index->buckets[slot];
        index->entries[count] = entry;
        index->buckets[slot] = ++count;
    }
    index->count = count;
    return true;
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
void qa_id_index_clear(qa_id_index *index)
{
    index->count = 0;
    if (index->capacity) memset(index->buckets, 0, index->capacity * 2 * sizeof(*index->buckets));
}
void qa_id_index_destroy(qa_id_index *index)
{
    if (!index->storage) { free(index->entries); free(index->buckets); }
    *index = (qa_id_index){0};
}
