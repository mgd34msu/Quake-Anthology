#ifndef QA_NETWORK_RUNTIME_Q1_MESSAGES_H
#define QA_NETWORK_RUNTIME_Q1_MESSAGES_H

#include "qa/pool.h"
#include <string.h>

typedef struct q1_message {
    struct q1_message *next;
    qa_buffer bytes;
    size_t slot;
    uint64_t serial;
} q1_message;

typedef struct q1_messages {
    qa_arena storage;
    qa_pool records;
    q1_message *first, *last;
    uint8_t *payload;
    size_t capacity, end, bytes, count;
} q1_messages;

static inline bool q1_messages_prepare(q1_messages *queue, size_t bytes,
    size_t records, qa_error *error)
{
    if (!qa_pool_prepare(&queue->records, &queue->storage, records,
            sizeof(q1_message), _Alignof(q1_message), error)) return false;
    queue->payload = qa_arena_alloc(&queue->storage, bytes, 1, error);
    if (!queue->payload) return false;
    queue->capacity = bytes;
    qa_arena_seal(&queue->storage);
    return true;
}

static inline void q1_messages_clear(q1_messages *queue)
{
    queue->first = queue->last = NULL;
    queue->end = queue->bytes = queue->count = 0;
    qa_pool_reset(&queue->records);
}

static inline void q1_messages_destroy(q1_messages *queue)
{ qa_arena_destroy(&queue->storage); }

/* The channel copies a head message before pop. Only queued messages borrow
 * this packed backing, so compaction cannot invalidate an in-flight packet. */
static inline uint8_t *q1_messages_tail(q1_messages *queue, size_t bytes)
{
    if (bytes > queue->capacity - queue->end) {
        size_t offset = 0;
        for (q1_message *row = queue->first; row; row = row->next) {
            memmove(queue->payload + offset, row->bytes.data, row->bytes.size);
            row->bytes.data = queue->payload + offset;
            offset += row->bytes.size;
        }
        queue->end = offset;
    }
    uint8_t *result = queue->payload + queue->end;
    queue->end += bytes;
    queue->bytes += bytes;
    return result;
}

static inline q1_message *q1_messages_append(q1_messages *queue, qa_bytes bytes,
    qa_error *error)
{
    size_t slot;
    q1_message *row = qa_pool_take(&queue->records, &slot);
    if (!row) {
        qa_error_set(error, QA_ERROR_CAPACITY, 0, "Q1 reliable queue is full");
        return NULL;
    }
    *row = (q1_message){.slot = slot, .bytes = {q1_messages_tail(queue, bytes.size), bytes.size}};
    memcpy(row->bytes.data, bytes.data, bytes.size);
    if (queue->last) queue->last->next = row; else queue->first = row;
    queue->last = row;
    ++queue->count;
    return row;
}

static inline void q1_messages_extend(q1_messages *queue, qa_bytes bytes)
{
    uint8_t *tail = q1_messages_tail(queue, bytes.size);
    memcpy(tail, bytes.data, bytes.size);
    queue->last->bytes.size += bytes.size;
}

static inline void q1_messages_pop(q1_messages *queue)
{
    q1_message *row = queue->first;
    queue->first = row->next;
    queue->bytes -= row->bytes.size;
    --queue->count;
    qa_pool_release(&queue->records, row->slot);
    if (!queue->first) {
        queue->last = NULL;
        queue->end = 0;
    }
}

#endif
