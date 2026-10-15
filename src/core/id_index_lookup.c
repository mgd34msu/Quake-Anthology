#include "id_index_internal.h"
#include <string.h>

size_t qa_id_index_capacity_for(size_t required)
{
    size_t maximum = SIZE_MAX / sizeof(qa_id_index_entry);
    if (maximum > SIZE_MAX / 2 / sizeof(size_t)) maximum = SIZE_MAX / 2 / sizeof(size_t);
    if (required > maximum) return 0;
    size_t capacity = 32;
    while (capacity < required) {
        if (capacity > maximum / 2) return 0;
        capacity *= 2;
    }
    return capacity;
}

void qa_id_index_insert(qa_id_index *index, qa_string_id key, uint64_t discriminator, size_t value)
{
    size_t slot = qa_id_index_bucket(key, discriminator, index->capacity);
    index->entries[index->count] = (qa_id_index_entry){key, discriminator, value, index->buckets[slot]};
    index->buckets[slot] = ++index->count;
}
qa_id_index_cursor qa_id_index_begin(const qa_id_index *index, qa_string_id key, uint64_t discriminator)
{
    return (qa_id_index_cursor){key, discriminator,
        index->capacity ? index->buckets[qa_id_index_bucket(key, discriminator, index->capacity)] : 0};
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
void qa_id_index_clear(qa_id_index *index)
{
    index->count = 0;
    if (index->capacity) memset(index->buckets, 0, index->capacity * 2 * sizeof(*index->buckets));
}
