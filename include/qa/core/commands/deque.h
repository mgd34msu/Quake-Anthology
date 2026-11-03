/*
 * Port of src/core/commands/deque.ts: an insertion-ordered queue with cheap
 * front removal and front insertion for pending command chunks. Unbounded
 * (it replaced the 32768-slot EventRing). Records are element-size bytes
 * copied with memcpy; the deque never owns what a record points to.
 * Allocation failure aborts (no TypeScript counterpart).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMANDS_DEQUE_H
#define QA_CORE_COMMANDS_DEQUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_deque {
  uint8_t *items;
  size_t elem_size;
  size_t start;    /* TS start */
  size_t length;   /* TS items.length */
  size_t capacity;
} qa_deque;

void qa_deque_init(qa_deque *deque, size_t elem_size);
void qa_deque_destroy(qa_deque *deque);
/* TS live. */
size_t qa_deque_live(const qa_deque *deque);
/* TS admit (push). */
void qa_deque_admit(qa_deque *deque, const void *record);
/* TS head / at / atOffset: NULL is TS null. */
const void *qa_deque_head(const qa_deque *deque);
const void *qa_deque_at(const qa_deque *deque, int64_t index);
/* TS replaceAtOffset / removeAtOffset: an offset outside the live records
 * is a RangeError("Deque offset outside live records"). */
void qa_deque_replace_at_offset(qa_deque *deque, int64_t index, const void *record);
void qa_deque_remove_at_offset(qa_deque *deque, int64_t index);
/* TS shift: false is TS null; the record is copied to *out. */
bool qa_deque_shift(qa_deque *deque, void *out);
/* TS prepend. */
void qa_deque_prepend(qa_deque *deque, const void *records, size_t count);
/* TS snapshot: a malloc'd copy of the live records (NULL when empty). */
void *qa_deque_snapshot(const qa_deque *deque, size_t *out_count);
/* Drops every record (the caller releases what they point to first). */
void qa_deque_clear(qa_deque *deque);

#endif
